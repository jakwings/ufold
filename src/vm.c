#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../utf8proc/utf8proc.h"
#include "utf8.h"
#include "utils.h"
#include "vm.h"

#define SLOT_SIZE 4  // maximum length of a Unicode scalar in bytes

//\ I/O State of VM
typedef enum vm_state {
    VM_WORD,  // processing a fragment
    VM_LINE,  // processing a new line
    VM_WRAP,  // processing a new wrapped line
    VM_FULL,  // maximum line width exceeded
} vm_state_t;

//\ Virtual State Machine
struct ufold_vm_struct {
    // Configuration
    ufold_vm_config_t config;
    // Accumulation
    uint8_t* line;
    size_t max_size;  // capacity of line buffer (excluding extra area)
    uint8_t slots[SLOT_SIZE];
    size_t slot_used;
    uint8_t* indent;
    size_t indent_size;
    size_t indent_width;
    size_t indent_bufsize;
    size_t offset;  // (width) if buffer start is not at line start
    size_t line_size;
    size_t line_width;  // only updated when needed (in order to save time)
    // Switches
    vm_state_t state;
    bool indent_hanging;  // hanging punctuation
    bool stopped;
};
//typedef struct ufold_vm_struct ufold_vm_t;

static bool default_write(const void* ptr, size_t size);

static void* default_realloc(void* ptr, size_t size);

static void* vm_realloc(ufold_vm_t* vm, void* ptr, size_t size);

static void vm_free(ufold_vm_t* vm, void* ptr);

static size_t vm_slot(ufold_vm_t* vm, const uint8_t* byte, uint8_t* output);

static void vm_slot_shift(ufold_vm_t* vm, size_t n);

static void vm_line_shift(ufold_vm_t* vm, size_t size, size_t width);

static bool vm_line_update_width(ufold_vm_t* vm, bool need_tab);

static bool vm_feed(ufold_vm_t* vm, const uint8_t* input, size_t size);

static bool vm_flush(ufold_vm_t* vm);

static bool vm_indent(ufold_vm_t* vm);

static bool skip_punctuation(const uint8_t* bytes, size_t size,
                             size_t tab_width, const char* punctuation,
                             const uint8_t** index, size_t* line_width);

/*\
 / DESCRIPTION
 /   Default Writer for Output
\*/
static bool default_write(const void* ptr, size_t size)
{
    return (size > 0) ? (fwrite(ptr, size, 1, stdout) == 1) : true;
}

/*\
 / DESCRIPTION
 /   Default Memory Reallocator
\*/
static void* default_realloc(void* ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

/*\
 / DESCRIPTION
 /   VM's Own Memory Reallocator
\*/
static void* vm_realloc(ufold_vm_t* vm, void* ptr, size_t size)
{
    return vm->config.realloc(ptr, size);
}

/*\
 / DESCRIPTION
 /   VM's Own Memory Deallocator
\*/
static void vm_free(ufold_vm_t* vm, void* ptr)
{
    vm->config.realloc(ptr, 0);
}

ufold_vm_t* ufold_vm_new(ufold_vm_config_t config)
{
    if (config.write == NULL) {
        config.write = default_write;
    }
    if (config.realloc == NULL) {
        config.realloc = default_realloc;
    }

    ufold_vm_t* vm = config.realloc(NULL, sizeof(ufold_vm_t));

    if (vm == NULL) {
        // TODO: inform error type?
        return NULL;
    }
    vm->config = config;

    assert(MAX_WIDTH >= 0);

    // |<------------- LINE AREA ------------->|< EXTRA >|
    // [QUADRUPED QUADRUPED QUADRUPED ......... QUADRUPED]
    size_t width = (config.max_width > 0) ? config.max_width : MAX_WIDTH;
    // check overflow
    if (sizeof(uint8_t) <= 0 || SIZE_MAX / width / 4 < sizeof(uint8_t)) {
        vm_free(vm, vm);
        return NULL;
    }
    size_t size = sizeof(uint8_t) * 4 * width + SLOT_SIZE;
    // check overflow
    if (size < sizeof(uint8_t) * 4 * width) {
        vm_free(vm, vm);
        return NULL;
    }

    vm->line = config.realloc(NULL, size);

    if (vm->line == NULL) {
        vm_free(vm, vm);
        return NULL;
    }
    vm->max_size = size - SLOT_SIZE;

    vm->slot_used = 0;
    vm->indent = NULL;
    vm->indent_size = 0;
    vm->indent_width = 0;
    vm->indent_bufsize = 0;
    vm->indent_hanging = false;
    vm->offset = 0;
    vm->line_size = 0;
    vm->line_width = 0;
    vm->state = VM_LINE;
    vm->stopped = false;

    return vm;
}

void ufold_vm_free(ufold_vm_t* vm)
{
    if (vm != NULL) {
        vm_free(vm, vm->line);
        vm_free(vm, vm->indent);
        vm_free(vm, vm);
    }
}

bool ufold_vm_stop(ufold_vm_t* vm)
{
    if (!vm->stopped) {
        vm->stopped = true;

        if (vm->slot_used > 0) {
            vm->slot_used = utf8_sanitize(vm->slots, vm->slot_used,
                                          vm->config.truncate_bytes);

            if (!vm_feed(vm, vm->slots, vm->slot_used)) {
                logged_return(false);
            }
            vm->slot_used = 0;
        }
        if (!vm_flush(vm)) {
            logged_return(false);
        }
    }
    return true;
}

bool ufold_vm_flush(ufold_vm_t* vm)
{
    if (!vm->stopped) {
        logged_return(vm_flush(vm));
    }
    logged_return(false);
}

bool ufold_vm_feed(ufold_vm_t* vm, const void* bytes, size_t size)
{
    if (vm->stopped) {
        logged_return(false);
    }
    // TODO: provide a fast path for ASCII mode

    for (size_t i = 0; i < size; i++) {
        const uint8_t* input = (const uint8_t*)bytes + i;
        size_t n = 0;

        do {
            uint8_t output[SLOT_SIZE];

            n = vm_slot(vm, input, output);
            // TODO: new option for interpreting ANSI color codes
            n = utf8_sanitize(output, n, vm->config.truncate_bytes);
            input = NULL;

            if (n > 0 && !vm_feed(vm, output, n)) {
                vm->stopped = true;
                logged_return(false);
            }
        } while (n > 0);
    }

    return true;
}

/*\
 / DESCRIPTION
 /   Push a byte into the available slots and pop a useful byte sequence.
 /   A potentially well-formed UTF-8 byte sequence is not useful yet.
 /
 / PARAMETERS
 /    *byte --> optional input
 /   output <-- (1 to SLOT_SIZE) valid bytes after simple filtering
 /
 / RETURN
 /   + :: success, size of output
 /   0 :: queuing, retry later
\*/
static size_t vm_slot(ufold_vm_t* vm, const uint8_t* byte, uint8_t* output)
{
    assert(vm->slot_used < SLOT_SIZE);

    size_t n = vm->slot_used;

    if (byte != NULL) {
        vm->slots[n++] = *byte;
        vm->slot_used = n;
    }
    if (n == 0) {
        return 0;
    }

    size_t len = utf8_valid_length(vm->slots[0]);

    assert(len <= SLOT_SIZE);

    if (len > 0 && len <= n) {
        memcpy(output, vm->slots, len);
        vm_slot_shift(vm, len);
        return len;
    }
    if (n == SLOT_SIZE) {
        memcpy(output, vm->slots, n);
        vm_slot_shift(vm, n);
        return n;
    }

    return 0;
}

/*\
 / DESCRIPTION
 /   Shift the VM's buffer queuing slots by N places.
\*/
static void vm_slot_shift(ufold_vm_t* vm, size_t n)
{
    assert(vm->slot_used >= n);

    if (vm->slot_used <= n) {
        vm->slot_used = 0;
    } else {
        memmove(vm->slots, vm->slots + n, vm->slot_used - n);
        vm->slot_used -= n;
    }
}

/*\
 / DESCRIPTION
 /   Shift the VM's line buffer by N places.
\*/
static void vm_line_shift(ufold_vm_t* vm, size_t size, size_t width)
{
    assert(vm->line_size >= size && vm->line_width >= width);

    if (vm->line_size <= size || vm->line_width < width) {
        vm->line_size = 0;
        vm->line_width = 0;
    } else {
        memmove(vm->line, vm->line + size, vm->line_size - size);
        vm->line_size -= size;
        vm->line_width -= width;
    }
}

/*\
 / DESCRIPTION
 /   Update line width, especially for TABs.
\*/
static bool vm_line_update_width(ufold_vm_t* vm, bool need_tab)
{
    if (need_tab) {
        for (size_t i = 0, j = vm->line_size; i < j; i++) {
            uint8_t c = vm->line[i];

            if (is_linefeed(c)) {
                return true;
            } else if (c == '\t') {
                break;
            } else if (i + 1 == j) {
                return true;
            }
        }
    }

    size_t width = vm->offset;

    if (!utf8_calc_width(vm->line, vm->line_size, vm->config.tab_width,
                         &width)) {
        logged_return(false);
    }
    vm->line_width = width - vm->offset;
    return true;
}

/*\
 / DESCRIPTION
 /   Fill the line with new bytes and produce output.
\*/
static bool vm_feed(ufold_vm_t* vm, const uint8_t* bytes, const size_t size)
{
    assert(vm->line_size <= vm->max_size);
    assert(size <= SLOT_SIZE);

    memcpy(vm->line + vm->line_size, bytes, size);
    vm->line_size += size;

    if (vm->line_size > vm->max_size || has_linefeed(bytes, size)) {
        if (!vm_flush(vm)) {
            logged_return(false);
        }
    }

    if (vm->line_size > vm->max_size) {
        // may be looking for the word boundary
        size_t max_size = vm->line_size + SLOT_SIZE;
        // check overflow
        if (max_size < vm->line_size) {
            logged_return(false);
        }
        // LINE AREA + OVERFLOW AREA
        size_t buf_size = max_size + SLOT_SIZE;
        // check overflow
        if (buf_size < max_size) {
            logged_return(false);
        }
        uint8_t* buf = vm_realloc(vm, vm->line, buf_size);

        if (buf == NULL) {
            logged_return(false);
        }
        vm->line = buf;
        vm->max_size = max_size;
    }

    return true;
}

/*\
 / DESCRIPTION
 /   Flush buffered content.
\*/
static bool vm_flush(ufold_vm_t* vm)
{
#ifdef NDEBUG
    // inharmonious logic
    if (vm->config.max_width == 0) {
        // write sanitized input
        if (vm->line_size > 0 && !vm->config.write(vm->line, vm->line_size)) {
            logged_return(false);
        }
        vm->line_size = 0;
        return true;
    }
#endif

    if (!vm_line_update_width(vm, false)) {
        logged_return(false);
    }

    while (vm->line_size > 0) {
        // hard break inside a wide character right before line-end
        if (vm->state == VM_WRAP) {
            if (!vm->config.write("\n", 1)) {
                logged_return(false);
            }
            if (is_linefeed(vm->line[0])) {
                vm_line_shift(vm, 1, 0);
                vm->offset = 0;
                vm->indent_size = 0;
                vm->indent_width = 0;
                vm->indent_hanging = false;

                // no need to recalculate TAB width
                vm->state = VM_LINE;
                continue;
            }
        }

        if (vm->config.keep_indentation) {
            // may change vm->state
            if (!vm_indent(vm)) {
                logged_return(false);
            }
            if (vm->line_size == 0) {
                assert(vm->state == VM_LINE || vm->state == VM_FULL);
                return true;
            }
        }
        assert(vm->line_size > 0);

        if (vm->config.max_width == 0) {
            vm->state = VM_FULL;
        }

        if (vm->state == VM_FULL) {
            const uint8_t* end = NULL;
            size_t new_offset = vm->offset;
            bool eol_found = false;

            if (!find_eol(vm->line, vm->line_size, vm->config.tab_width,
                          &end, &new_offset, &eol_found)) {
                logged_return(false);
            }
            size_t size = end - vm->line;
            size_t width = new_offset - vm->offset;

            if (!vm->config.write(vm->line, size)) {
                logged_return(false);
            }

            if (eol_found) {
                vm->line_size = 0;
                vm->line_width = 0;
                vm->offset = 0;
                vm->indent_size = 0;
                vm->indent_width = 0;
                vm->indent_hanging = false;

                vm->state = VM_LINE;
            } else {
                vm_line_shift(vm, size, width);
                vm->offset += width;
            }

            continue;
        }

        const uint8_t* word_end = NULL;
        const uint8_t* linefeed = NULL;
        const uint8_t* end = NULL;
        size_t new_offset = vm->offset;

        if (!break_line(vm->line, vm->line_size, vm->config.break_at_spaces,
                        vm->config.tab_width, vm->config.max_width,
                        &word_end, &linefeed, &end, &new_offset)) {
            logged_return(false);
        }
        assert(new_offset >= vm->offset);

        size_t size = end - vm->line;
        size_t width = new_offset - vm->offset;

        if (linefeed != NULL) {
            assert(new_offset <= vm->config.max_width);

            if (!vm->config.write(vm->line, size)) {
                logged_return(false);
            }
            vm_line_shift(vm, size, width);
            vm->offset = 0;
            vm->indent_size = 0;
            vm->indent_width = 0;
            vm->indent_hanging = false;

            // no need to recalculate TAB width
            vm->state = VM_LINE;
        } else if (word_end != NULL) {
            // maximum line width exceeded and break at whitespace
            assert(new_offset <= vm->config.max_width);

            size = end - vm->line;

            // IDEA: keep the original behavior of fold(1), i.e. no trim?
            if (vm->config.break_at_spaces) {
                // trailing spaces will be trimmed
                if (!vm->config.write(vm->line, word_end - vm->line)) {
                    logged_return(false);
                }
            } else {
                // write the trailing spaces as well
                if (!vm->config.write(vm->line, size)) {
                    logged_return(false);
                }
            }
            vm_line_shift(vm, size, width);

            if (vm->config.break_at_spaces) {
                vm->offset += width;

                const uint8_t* word_start = vm->line;
                size_t skipped_width = vm->offset;

                if (!skip_space(vm->line, vm->line_size, vm->config.tab_width,
                                &word_start, &skipped_width)) {
                    logged_return(false);
                }
                skipped_width = skipped_width - vm->offset;

                // trim leading spaces from original text
                vm_line_shift(vm, word_start - vm->line, skipped_width);
            }

            vm->offset = vm->indent_width;
            // recalculate TAB width
            if (!vm_line_update_width(vm, true)) {
                logged_return(false);
            }

            vm->state = VM_WRAP;
        } else {
            if (vm->offset + vm->line_width <= vm->config.max_width) {
                if (vm->stopped) {
                    if (!vm->config.write(vm->line, vm->line_size)) {
                        logged_return(false);
                    }
                    vm->offset += vm->line_width;
                    vm->line_size = 0;
                    vm->line_width = 0;
                }

                vm->state = VM_WORD;
                // START->|indent word word ... word|<-MAX
                // don't break inside a possibly incomplete word
                return true;
            } else {
                end = NULL;
                new_offset = vm->offset;

                if (!skip_width(vm->line, vm->line_size,
                                vm->config.tab_width, vm->config.max_width,
                                &end, &new_offset)) {
                    logged_return(false);
                }
                size = end - vm->line;
                width = new_offset - vm->offset;
                assert(size > 0 && width > 0);

                if (!vm->config.write(vm->line, size)) {
                    logged_return(false);
                }
                vm_line_shift(vm, size, width);
                vm->offset = vm->indent_width;
                // recalculate TAB width
                if (!vm_line_update_width(vm, true)) {
                    logged_return(false);
                }

                vm->state = VM_WRAP;
            }
        }
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Update line indent and VM state.
\*/
static bool vm_indent(ufold_vm_t* vm)
{
    // update indent, may update twice if indent is too large
    if (vm->state == VM_LINE) {
        assert(vm->indent_width == vm->offset);

        const uint8_t* line_end = vm->line + vm->line_size;
        const uint8_t* indent_end = vm->line;
        size_t width = vm->indent_width;
        size_t size = 0;

        if (!(vm->config.hang_punctuation && vm->indent_hanging)) {
           if (!skip_space(vm->line, vm->line_size, vm->config.tab_width,
                           &indent_end, &width)) {
               logged_return(false);
           }
        }

        size_t hang_width = 0;
        size_t hang_size = 0;

        if (vm->config.hang_punctuation) {
            const uint8_t* old_indent_end = indent_end;
            size_t old_width = width;

            if (!skip_punctuation(indent_end, line_end - indent_end,
                                  vm->config.tab_width, vm->config.punctuation,
                                  &indent_end, &width)) {
                logged_return(false);
            }
            hang_width = width - old_width;
            hang_size = indent_end - old_indent_end;
            vm->indent_hanging = vm->indent_hanging || hang_size > 0;
        }

        width = width - vm->indent_width;
        size = indent_end - vm->line;

        if (size > 0) {
            size_t buf_size = vm->indent_size + size;
            if (hang_size > 0 && width > size) {
                // later, turn punctuation into spaces
                buf_size = vm->indent_size + width;
            }
            // check overflow
            if (buf_size < vm->indent_size) {
                logged_return(false);
            }
            if (buf_size > vm->indent_bufsize) {
                uint8_t* buf = vm_realloc(vm, vm->indent, buf_size);

                if (buf == NULL) {
                    logged_return(false);
                }
                vm->indent = buf;
                vm->indent_bufsize = buf_size;
            }

            memcpy(vm->indent + vm->indent_size, vm->line, size);
            vm->indent_size += size;
            vm->indent_width += width;
            if (hang_size > 0) {
                uint8_t* hang_e = vm->indent + vm->indent_size;
                uint8_t* hang_s = hang_e - hang_size;
                while (hang_s < hang_e) {
                    *hang_s = ' ';
                    ++hang_s;
                }
                vm->indent_size = vm->indent_size - hang_size + hang_width;
            }

            if (!vm->config.write(vm->line, size)) {
                logged_return(false);
            }
            vm->offset += width;
            vm_line_shift(vm, size, width);
        } else {
            assert(vm->line_size > 0);  // infinite loop otherwise
        }

        if (vm->indent_width >= vm->config.max_width) {
            vm->state = VM_FULL;
        } else if (indent_end != line_end) {
            // START [space] non-space ... END
            vm->state = VM_WORD;
        }
    } else if (vm->state == VM_WRAP) {
        assert(vm->indent_width == vm->offset);

        if (vm->indent_size > 0) {
            assert(vm->indent != NULL);

            if (!vm->config.write(vm->indent, vm->indent_size)) {
                logged_return(false);
            }
        }
    }
    return true;
}

static bool skip_punctuation(const uint8_t* bytes, size_t size,
                             size_t tab_width, const char* punctuation,
                             const uint8_t** index, size_t* line_width)
{
    bool use_preset = punctuation == NULL;
    char buf[5];

    const uint8_t* new_index = bytes;
    size_t new_width = *line_width;
    int w = -1;

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;
    utf8proc_category_t category;

    for (size_t i = 0; i < size; i += n_bytes) {
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        if (use_preset) {
            if (!(codepoint < 0x7F && strchr("\"`'([{", (int)codepoint))
                    // ‘ ’ “
                    && codepoint != 0x2018
                    && codepoint != 0x2019
                    && codepoint != 0x201C) {
                category = utf8proc_category(codepoint);
                if (category != UTF8PROC_CATEGORY_PI &&
                        category != UTF8PROC_CATEGORY_PS) {
                    break;
                }
            }
        } else {
            category = utf8proc_category(codepoint);
            // also disallow \t
            if (category == UTF8PROC_CATEGORY_ZS ||
                    category == UTF8PROC_CATEGORY_CC) {
                break;
            }
            memcpy(buf, bytes + i, n_bytes);
            buf[n_bytes] = '\0';
            if (strstr(punctuation, buf) == NULL) {
                break;
            }
        }

        if ((w = utf8proc_charwidth(codepoint)) < 0) {
            logged_return(false);
        }
        // check overflow
        if (new_width + w < new_width) {
            logged_return(false);
        }
        new_width += w;

        new_index = bytes + i + n_bytes;
    }

    *index = new_index;
    *line_width = new_width;
    return true;
}
