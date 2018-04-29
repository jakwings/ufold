#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utf8.h"
#include "utils.h"
#include "vm.h"

#define SLOT_SIZE 4

//\ I/O State of VM
typedef enum vm_state {
    VM_WORD,
    VM_LINE,
    VM_WRAP,
    VM_FULL,
} vm_state_t;

//\ Virtual State Machine
struct ufold_vm_struct {
    // Configuration
    ufold_vm_config_t config;
    // Accumulation
    uint8_t* line;
    size_t max_size;  // capacity of line buffer (excluding extra area)
    uint8_t* slots;
    size_t slot_used;
    uint8_t* indent;
    size_t indent_size;
    size_t indent_width;
    size_t offset;  // (width) if buffer start is not at line start
    size_t line_size;
    size_t line_width;
    // Switches
    vm_state_t state;
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

static bool vm_line_update_width(ufold_vm_t* vm);

static bool vm_feed(ufold_vm_t* vm, const uint8_t* input, size_t size);

static bool vm_flush(ufold_vm_t* vm);

static bool vm_indent(ufold_vm_t* vm);

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
        return NULL;
    }

    assert(SLOT_SIZE >= sizeof(uint32_t));
    assert(MAX_WIDTH >= 0);

    // |<------------- LINE AREA ------------->|< EXTRA >|
    // [QUADRUPED QUADRUPED QUADRUPED ......... QUADRUPED]
    size_t width = (config.max_width > 0) ? config.max_width : MAX_WIDTH;
    size_t size = sizeof(uint32_t) * width + SLOT_SIZE;

    vm->line = config.realloc(NULL, size);

    if (vm->line == NULL) {
        vm_free(vm, vm);
        return NULL;
    }
    vm->max_size = size - SLOT_SIZE;

    vm->slots = config.realloc(NULL, SLOT_SIZE);

    if (vm->slots == NULL) {
        vm_free(vm, vm->line);
        vm_free(vm, vm);
        return NULL;
    }
    vm->slot_used = 0;

    vm->config = config;
    vm->indent = NULL;
    vm->indent_size = 0;
    vm->indent_width = 0;
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
        vm_free(vm, vm->slots);
        vm_free(vm, vm->indent);
        vm_free(vm, vm);
    }
}

bool ufold_vm_stop(ufold_vm_t* vm)
{
    if (!vm->stopped) {
        vm->stopped = true;

        if (vm->slot_used > 0) {
            memset(vm->slots, '?', vm->slot_used);
        }
        if (!vm_flush(vm) || !vm->config.write(vm->slots, vm->slot_used)) {
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

    for (size_t i = 0; i < size; i++) {
        const uint8_t* input = (const uint8_t*)bytes + i;
        size_t n = 0;

        do {
            uint8_t output[SLOT_SIZE] = {0};

            n = vm_slot(vm, input, output);
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
 /   Push a byte into the available slots and pop a valid UTF-8 byte sequence.
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

    const uint8_t* taint = utf8_validate(vm->slots, n);

    if (taint != NULL) {
        assert(vm->slots <= taint && taint < vm->slots + vm->slot_used);

        size_t size = 1 + (taint - vm->slots);

        if (utf8_valid_length(*taint) > 0) {
            assert(taint != vm->slots);

            size -= 1;
        }

        memset(output, '?', size);
        vm_slot_shift(vm, size);

        return size;
    }

    size_t len = utf8_valid_length(vm->slots[0]);

    if (len == n) {
        memcpy(output, vm->slots, len);
        vm_slot_shift(vm, len);
        return len;
    }

    if (n == SLOT_SIZE) {
        memset(output, '?', SLOT_SIZE);
        vm_slot_shift(vm, SLOT_SIZE);
        return SLOT_SIZE;
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

    if (vm->slot_used >= n) {
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

    if (vm->line_size >= size && vm->line_width >= width) {
        memmove(vm->line, vm->line + size, vm->line_size - size);
        vm->line_size -= size;
        vm->line_width -= width;
    }
}

/*\
 / DESCRIPTION
 /   Update line width, especially for TABs.
\*/
static bool vm_line_update_width(ufold_vm_t* vm)
{
    // recalculate TAB width
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
    bool done = true;

#ifdef NDEBUG
    // inharmonious logic
    if (vm->config.max_width == 0) {
        memcpy(vm->line + vm->line_size, bytes, size);
        vm->line_size += size;

        if (vm->line_size > vm->max_size || has_linefeed(bytes, size)) {
            done = vm_flush(vm);
        }
        goto RESIZE;
    }
#endif

    size_t width = vm->offset + vm->line_width;

    if (!utf8_calc_width(bytes, size, vm->config.tab_width, &width)) {
        logged_return(false);
    }
    memcpy(vm->line + vm->line_size, bytes, size);
    vm->line_size += size;
    vm->line_width += width - vm->offset - vm->line_width;

#ifdef NDEBUG
    if (vm->line_size > vm->max_size || has_linefeed(bytes, size)) {
        done = vm_flush(vm);
    }
#else
    if (vm->line_width >= vm->config.max_width ||
            vm->line_size > vm->max_size || has_linefeed(bytes, size)) {
        done = vm_flush(vm);
    }
#endif

#ifdef NDEBUG
RESIZE:
#endif
    if (vm->line_size > vm->max_size) {
        // may be looking for the word boundary
        size_t max_size = vm->line_size * 2;
        // LINE AREA + OVERFLOW AREA
        size_t buf_size = max_size + SLOT_SIZE;
        uint8_t* buf = vm_realloc(vm, vm->line, buf_size);

        if (buf == NULL) {
            logged_return(false);
        }
        vm->line = buf;
        vm->max_size = max_size;
    }

    return done;
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

    while (vm->line_size > 0) {
        if (vm->config.keep_indentation) {
            // may change vm->state
            if (!vm_indent(vm)) {
                logged_return(false);
            }
            if (vm->line_size == 0) {
                assert(vm->state == VM_LINE || vm->state == VM_FULL);
                return true;
            }
        } else {
            if (vm->config.max_width == 0) {
                vm->state = VM_FULL;
            }
        }
        assert(vm->line_size > 0);

        if (vm->state == VM_FULL) {
            const uint8_t* end = NULL;
            size_t new_offset = vm->offset;
            bool found = false;

            if (!find_eol(vm->line, vm->line_size, vm->config.tab_width,
                          &end, &new_offset, &found)) {
                logged_return(false);
            }
            size_t size = end - vm->line;
            size_t width = new_offset - vm->offset;

            if (!vm->config.write(vm->line, size)) {
                logged_return(false);
            }

            if (found) {
                vm->line_size = 0;
                vm->line_width = 0;
                vm->offset = 0;
                vm->indent_size = 0;
                vm->indent_width = 0;

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
#ifdef NDEBUG
            if (end - vm->line == vm->line_size) {
                vm->line_size = 0;
                vm->line_width = 0;
            } else {
                vm_line_shift(vm, size, width);
            }
#else
            vm_line_shift(vm, size, width);
#endif
            vm->offset = 0;
            vm->indent_size = 0;
            vm->indent_width = 0;
            // no need to recalculate TAB width
            vm->state = VM_LINE;
        } else if (word_end != NULL) {
            // maximum line width exceeded and break at whitespace
            assert(new_offset <= vm->config.max_width);

            // end, not word_end, so as to write the trailing spaces as well
            size = end - vm->line;

            if (!vm->config.write(vm->line, size) ||
                    !vm->config.write("\n", 1)) {
                logged_return(false);
            }
            vm_line_shift(vm, size, width);
            vm->offset = vm->indent_width;
            // recalculate TAB width
            vm_line_update_width(vm);

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

                // TODO: don't break ansi escape sequence?
                if (!vm->config.write(vm->line, size) ||
                        !vm->config.write("\n", 1)) {
                    logged_return(false);
                }
                vm_line_shift(vm, size, width);
                vm->offset = vm->indent_width;
                // recalculate TAB width
                vm_line_update_width(vm);

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

        if (!skip_space(vm->line, vm->line_size, vm->config.tab_width,
                        &indent_end, &width)) {
            logged_return(false);
        }
        width = width - vm->indent_width;

        size_t size = indent_end - vm->line;

        if (size > 0) {
            uint8_t* buf = vm_realloc(vm, vm->indent, vm->indent_size + size);

            if (buf == NULL) {
                logged_return(false);
            }
            vm->indent = buf;

            memcpy(vm->indent + vm->indent_size, vm->line, size);
            vm->indent_size += size;
            vm->indent_width += width;

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
