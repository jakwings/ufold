#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "vm.h"

//\ I/O State of VM
typedef enum vm_state {
    VM_LINE,  // processing a new line
    VM_WORD,  // processing a fragment
    VM_WRAP,  // processing a new wrapped line
    VM_FULL,  // maximum line width exceeded
} vm_state_t;

//\ Virtual State Machine
struct ufold_vm_struct {
    //\ Configuration
    ufold_vm_config_t config;
    //\ Accumulation
    uint8_t* buf;
    size_t buf_size;
    uint8_t* line;
    size_t line_size;
    size_t max_size;  // capacity of line buffer (without extra area; variable)
    size_t cursor;  // position of last processing byte
    size_t cursor_offset;  // (width) if line start is not at line start
    size_t eow;  // position of last end of word from line start
    size_t eow_ss;  // byte size of whitespace between breakpoints
    size_t eow_ww;  // width of non-whitespace between breakpoints
#define SLOT_SIZE 256
    uint8_t* slots;
    size_t slot_used;
    size_t slot_cursor;  // position of validation
    uint8_t* indent;
    size_t indent_size;
    size_t indent_width;
    size_t indent_bufsize;
    //\ Switches
    vm_state_t state;
    bool slot_crlf;  // whether the previously processed codepoint is CR
    bool indent_hanging;  // hanging punctuation
    bool cursor_at_word;  // processing byte of word
    bool stopped;
};
//typedef struct ufold_vm_struct ufold_vm_t;

static bool default_write(const void* ptr, size_t size);

static void* default_realloc(void* ptr, size_t size);

static void* vm_realloc(ufold_vm_t* vm, void* ptr, size_t size);

static void vm_free(ufold_vm_t* vm, void* ptr);

static size_t vm_slot(ufold_vm_t* vm, uint8_t byte);

static void vm_slot_shift(ufold_vm_t* vm, size_t n);

static void vm_line_shift(ufold_vm_t* vm, size_t n);

static bool vm_line_update_capacity(ufold_vm_t* vm);

static bool vm_feed(ufold_vm_t* vm, const uint8_t* bytes, size_t size);

static bool vm_feed_ascii(ufold_vm_t* vm, const uint8_t* bytes, size_t size);

static bool vm_flush(ufold_vm_t* vm);

static bool vm_indent(ufold_vm_t* vm);

static bool vm_indent_feed(ufold_vm_t* vm,
                           const void* bytes, size_t size, size_t width);

static void vm_indent_reset(ufold_vm_t* vm);

static void vm_eow_reset(ufold_vm_t* vm);

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

void ufold_vm_config_init(ufold_vm_config_t* config)
{
    memset(config, 0, sizeof(ufold_vm_config_t));
}

ufold_vm_t* ufold_vm_new(const ufold_vm_config_t* config)
{
#if SLOT_SIZE < 4
#error "SLOT_SIZE must be no smaller than 4"
#endif

#if SIZE_MAX < 1
#error "SIZE_MAX must be no smaller than 1"
#endif

    // |<------------- LINE AREA ------------->|< OVERFLOW AREA >|
    // [QUADRUPED QUADRUPED QUADRUPED ......... QUADRUPEDS & NUL ]
    size_t width = (config->max_width > 0) ? config->max_width : 0;
    // check overflow
    if (width > 0 && (SIZE_MAX - 1) / width / 4 < sizeof(uint8_t)) {
        logged_return(NULL);
    }
    size_t size = sizeof(uint8_t) * 4 * width + SLOT_SIZE + 1;
    // check overflow
    if (size <= SLOT_SIZE) {
        logged_return(NULL);
    }

    ufold_vm_config_t conf = *config;

    if (conf.write == NULL) {
        conf.write = default_write;
    }
    if (conf.realloc == NULL) {
        conf.realloc = default_realloc;
    }

    ufold_vm_t* vm = conf.realloc(NULL, sizeof(ufold_vm_t));

    if (vm == NULL) {
        // TODO: inform error type?
        logged_return(NULL);
    }
    memset(vm, 0, sizeof(ufold_vm_t));

    vm->config = conf;
    vm->config.punctuation = NULL;

    if (conf.punctuation != NULL) {
        size_t len = strlen(conf.punctuation) + 1;

        if ((vm->config.punctuation = conf.realloc(NULL, len)) == NULL) {
            ufold_vm_free(vm);
            logged_return(NULL);
        }
        memcpy(vm->config.punctuation, conf.punctuation, len - 1);
        vm->config.punctuation[len - 1] = '\0';
    }

    if ((vm->slots = conf.realloc(NULL, SLOT_SIZE)) == NULL) {
        ufold_vm_free(vm);
        logged_return(NULL);
    }

#ifndef UFOLD_DEBUG
    // inharmonious logic
    if (conf.max_width > 0) {
#endif
        if ((vm->buf = conf.realloc(NULL, size)) == NULL) {
            ufold_vm_free(vm);
            logged_return(NULL);
        }
        vm->line = vm->buf;
        vm->buf_size = size;
        vm->line_size = 0;
        vm->max_size = vm->buf_size - SLOT_SIZE - 1;
#ifndef UFOLD_DEBUG
    } else {
        vm->line = NULL;
        vm->buf_size = 0;
        vm->line_size = 0;
        vm->max_size = 0;
    }
#endif

    vm->cursor = 0;
    vm->cursor_offset = 0;
    vm->cursor_at_word = false;
    vm->eow = 0;
    vm->eow_ss = 0;
    vm->eow_ww = 0;
    vm->slot_used = 0;
    vm->slot_cursor = 0;
    vm->slot_crlf = false;
    vm->indent = NULL;
    vm->indent_size = 0;
    vm->indent_width = 0;
    vm->indent_bufsize = 0;
    vm->indent_hanging = false;
    vm->state = VM_LINE;
    vm->stopped = false;

    return vm;
}

void ufold_vm_free(ufold_vm_t* vm)
{
    if (vm != NULL) {
        vm_free(vm, vm->config.punctuation);
        vm_free(vm, vm->buf);
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
            debug_assert(vm->slot_cursor <= vm->slot_used);
            debug_assert(vm->slot_used - vm->slot_cursor <= 4);

            // sanitize invalid/incomplete byte sequence
            size_t n = 0;

            if (vm->config.ascii_mode) {
                for (size_t i = 0; i < vm->slot_used; ++i) {
                    vm->slots[i] = ascii_sanitize(vm->slots[i]);
                }
                n = vm->slot_used;
            } else {
                n = utf8_sanitize(vm->slots, vm->slot_used);
            }

            if (!vm_feed(vm, vm->slots, n)) {
                logged_return(false);
            }
            vm->slot_cursor = vm->slot_used;
            vm_slot_shift(vm, vm->slot_used);
        }
        if (!vm_flush(vm)) {
            logged_return(false);
        }
        debug_assert(vm->line_size <= 0);
    }
    return true;
}

bool ufold_vm_flush(ufold_vm_t* vm)
{
    if (!vm->stopped) {
        if (!vm_flush(vm)) {
            vm->stopped = true;
            logged_return(false);
        }
        return true;
    }
    logged_return(false);
}

bool ufold_vm_feed(ufold_vm_t* vm, const void* input, size_t size)
{
    if (vm->stopped) {
        logged_return(false);
    }

    if (vm->config.ascii_mode) {
        if (!vm_feed_ascii(vm, (const uint8_t*)input, size)) {
            vm->stopped = true;
            logged_return(false);
        }
        return true;
    }

    for (size_t i = 0; i < size; ++i) {
        size_t n = vm_slot(vm, ((const uint8_t*)input)[i]);
        if (n > 0) {
            // TODO: new option for interpreting ANSI color codes
            size_t k = utf8_sanitize(vm->slots, n);

            if (!vm_feed(vm, vm->slots, k)) {
                vm->stopped = true;
                logged_return(false);
            }
            vm_slot_shift(vm, n);
        }
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Push bytes into the available slots and pull back a valid byte sequence.
 /   A potentially well-formed UTF-8 byte sequence is not useful yet.
 /
 / RETURN
 /   N :: N bytes in slots are ready for use
 /   0 :: queuing, retry later
\*/
static size_t vm_slot(ufold_vm_t* vm, uint8_t byte)
{
    debug_assert(vm->slot_used < SLOT_SIZE);
    debug_assert(vm->slot_cursor <= vm->slot_used);

    uint8_t c = byte;

    // NOTE: ASCII Normalization: CRLF, CR -> LF
    if (c == '\r') {
        vm->slot_crlf = true;
        c = '\n';
    } else {
        bool crlf_combined = (c == '\n' && vm->slot_crlf);

        vm->slot_crlf = false;

        if (crlf_combined) {
            return 0;
        }
    }
    vm->slots[vm->slot_used++] = c;

    while (vm->slot_cursor < vm->slot_used) {
        size_t k = utf8_valid_length(vm->slots[vm->slot_cursor]);

        if (k <= 0) {
            // skip an invalid starting byte
            vm->slot_cursor += 1;
            continue;
        }
        if (k > vm->slot_used - vm->slot_cursor) {
            // found an incomplete sequence; need the rest bytes
            break;
        }

        utf8proc_int32_t codepoint = -1;
        utf8proc_ssize_t n_bytes = -1;

        n_bytes = utf8proc_iterate(vm->slots + vm->slot_cursor, k, &codepoint);
        debug_assert(n_bytes != 0);
        debug_assert(n_bytes <= 4);

        if (n_bytes < 0) {
            // skip an invalid starting byte of an invalid sequence
            vm->slot_cursor += 1;
        } else {
            debug_assert(k == n_bytes);
            // NOTE: UTF-8 Normalization: U+2028, U+2029, U+0085 -> LF
            if (codepoint == 0x2028 || codepoint == 0x2029 ||
                    codepoint == 0x0085) {
                uint8_t* p = vm->slots + vm->slot_cursor;
                size_t remains = vm->slot_used - vm->slot_cursor;

                memmove(p, p + n_bytes, remains - n_bytes);
                *p = '\n';
                vm->slot_used -= n_bytes - 1;
                vm->slot_cursor += 1;

                if (vm->config.line_buffered) {
                    return vm->slot_cursor;
                }
            } else {
                vm->slot_cursor += n_bytes;

                if (vm->config.line_buffered
                        && is_linefeed(codepoint, vm->config.ascii_mode)) {
                    return vm->slot_cursor;
                }
            }
        }
    }

    return vm->slot_used == SLOT_SIZE ? vm->slot_cursor : 0;
}

/*\
 / DESCRIPTION
 /   Shift the VM's buffer queuing slots by N places.
\*/
static void vm_slot_shift(ufold_vm_t* vm, size_t n)
{
    debug_assert(vm->slot_used >= n);
    debug_assert(vm->slot_cursor >= n);

    if (vm->slot_used <= n) {
        vm->slot_used = 0;
        vm->slot_cursor = 0;
    } else {
        memmove(vm->slots, vm->slots + n, vm->slot_used - n);
        vm->slot_used -= n;
        vm->slot_cursor -= n;
    }
}

/*\
 / DESCRIPTION
 /   Shift the VM's line buffer by N places.
\*/
static void vm_line_shift(ufold_vm_t* vm, size_t n)
{
    debug_assert(vm->line_size >= n);

    if (vm->line_size <= n) {
        vm->line = vm->buf;
        vm->line_size = 0;
        vm->max_size = vm->buf_size - SLOT_SIZE - 1;
        vm->cursor = 0;
        vm_eow_reset(vm);
    } else {
        if (vm->max_size > n) {
            vm->max_size -= n;
        } else {
            vm->max_size = 0;
        }

        vm->line += n;
        vm->line_size -= n;

        debug_assert(vm->cursor <= vm->line_size);
        debug_assert(vm->eow <= vm->line_size);
        debug_assert(vm->eow + vm->eow_ss <= vm->line_size);
    }
}

static bool vm_line_update_capacity(ufold_vm_t* vm)
{
    // may be looking for the word boundary
    if (vm->line_size > vm->max_size) {
        size_t offset = vm->line - vm->buf;

        if (vm->line_size > vm->buf_size - SLOT_SIZE - 1) {
            // LINE AREA + OVERFLOW AREA
            size_t buf_size = vm->buf_size + SLOT_SIZE + 1;
            // check overflow
            if (buf_size <= SLOT_SIZE) {
                logged_return(false);
            }
            uint8_t* buf = vm_realloc(vm, vm->buf, buf_size);

            if (buf == NULL) {
                logged_return(false);
            }
            vm->buf = buf;
            vm->buf_size = buf_size;
        }
        memmove(vm->buf, vm->buf + offset, vm->line_size + 1);
        vm->line = vm->buf;
        vm->max_size = vm->buf_size - SLOT_SIZE - 1;
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Fill the line with new bytes and produce output.
\*/
static bool vm_feed(ufold_vm_t* vm, const uint8_t* bytes, size_t size)
{
#ifndef UFOLD_DEBUG
    // inharmonious logic
    if (vm->config.max_width == 0) {
        // write sanitized input
        if (size > 0 && !vm->config.write(bytes, size)) {
            logged_return(false);
        }
        return true;
    }
#endif
    debug_assert(vm->line_size <= vm->max_size);
    debug_assert(size <= SLOT_SIZE);
    debug_assert(vm->line_size + size < vm->buf_size);

    if (size > 0) {
        memcpy(vm->line + vm->line_size, bytes, size);
        vm->line_size += size;

        if (vm->line_size > vm->max_size && !vm_flush(vm)) {
            logged_return(false);
        }
        if (!vm_line_update_capacity(vm)) {
            logged_return(false);
        }
    }

    return true;
}

/*\
 / DESCRIPTION
 /   Fill the line with new bytes and produce output.
\*/
static bool vm_feed_ascii(ufold_vm_t* vm, const uint8_t* bytes, size_t size)
{
    debug_assert(vm->line_size <= vm->max_size);
    debug_assert(vm->slot_used == 0);

    for (size_t i = 0; i < size; ++i) {
        uint8_t c = bytes[i];

        // NOTE: ASCII Normalization: CRLF, CR -> LF
        if (c == '\r') {
            vm->slot_crlf = true;
            c = '\n';
        } else {
            bool crlf_combined = (c == '\n' && vm->slot_crlf);

            vm->slot_crlf = false;

            if (crlf_combined) {
                continue;
            }
        }
#ifndef UFOLD_DEBUG
        // inharmonious logic
        if (vm->config.max_width == 0) {
            size_t k = i % SLOT_SIZE;
            vm->slots[k] = ascii_sanitize(c);

            // write sanitized input
            if ((k + 1 == SLOT_SIZE || i + 1 == size)
                    && !vm->config.write(vm->slots, k + 1)) {
                logged_return(false);
            }
            continue;
        }
#endif
        vm->line[vm->line_size++] = ascii_sanitize(c);

        // TODO: provide a fast path for ASCII mode
        if (vm->line_size > vm->max_size && !vm_flush(vm)) {
            logged_return(false);
        }
        if (!vm_line_update_capacity(vm)) {
            logged_return(false);
        }
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Flush buffered content.
\*/
static bool vm_flush(ufold_vm_t* vm)
{
#ifndef UFOLD_DEBUG
    // inharmonious logic
    if (vm->config.max_width == 0) {
        return true;
    }
#endif
    const uint8_t* sol = vm->line;
    const uint8_t* bytes = vm->line + vm->cursor;
    const uint8_t* word_end = vm->cursor_at_word ? vm->line + vm->cursor : NULL;
    size_t cursor = vm->cursor;
    size_t offset = vm->cursor_offset;
    size_t tab_width = vm->config.tab_width;
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    debug_assert(vm->line_size < vm->buf_size);
    vm->line[vm->line_size] = '\0';

    for (size_t i = cursor; i < vm->line_size; i += n_bytes, bytes += n_bytes) {
        debug_assert(bytes == vm->line + i);

        if (vm->config.ascii_mode) {
            codepoint = *bytes;
            n_bytes = (codepoint >= 0 && codepoint <= 0x7F ? 1 : -1);
        } else {
            n_bytes = utf8proc_iterate(bytes, vm->line_size - i, &codepoint);
        }

        if (n_bytes == 0) {
            if (vm->line_size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        int width = 0;

        if (codepoint == '\t') {
            // TODO: any place for recalculation?
            width = calc_tab_width(tab_width, offset);
        } else {
            width = get_charwidth(codepoint, vm->config.ascii_mode);
        }

        if (width > 0) {
            // check overflow
            if (offset + width < offset) {
                logged_return(false);
            }
            offset += width;
        } else if (width < 0) {
            logged_return(false);
        }

        if (vm->config.max_width <= 0) {
            debug_assert(vm->state != VM_WORD);
            debug_assert(vm->state != VM_WRAP);
            debug_assert(vm->indent_size == 0);

            vm->state = VM_FULL;
        }

        bool eol_found = is_linefeed(codepoint, vm->config.ascii_mode);

        bool ws_found = !eol_found && is_whitespace(codepoint,
                                                    vm->config.ascii_mode);

        debug_assert(!eol_found || width == 0);

        if (vm->state != VM_FULL) {
            if (!eol_found && !ws_found) {
                if (vm->eow > 0) {
                    vm->eow_ww += width;
                }
                word_end = bytes + n_bytes;
            } else {
                if (word_end != NULL) {
                    // no break at zero-width text right after newline
                    if (offset > 0) {
                        vm->eow = word_end - vm->line;
                        vm->eow_ss = 0;
                        vm->eow_ww = 0;
                    }
                    word_end = NULL;
                }
                if (vm->eow > 0) {
                    vm->eow_ss += n_bytes;
                }
            }
        }

        if (vm->state == VM_WRAP)
        {
            if (vm->config.break_at_spaces) {
                if (ws_found && sol == bytes) {
                    // skip whitespace after breakpoint
                    sol = bytes + n_bytes;
                    offset = vm->indent_width;
                    continue;
                }
                if (eol_found && sol <= bytes - n_bytes) {
                    debug_assert(offset <= vm->config.max_width);

                    if (!vm->config.write("\n", 1)) {
                        logged_return(false);
                    }
                    if (vm->config.keep_indentation) {
                        if (!vm_indent(vm)) {
                            logged_return(false);
                        }
                        vm_indent_reset(vm);
                    }
                    if (!vm->config.write(sol, bytes - sol + n_bytes)) {
                        logged_return(false);
                    }
                    sol = bytes + n_bytes;
                    offset = 0;
                    vm_eow_reset(vm);
                    vm->state = VM_LINE;
                    continue;
                }
            }
            if (!vm->config.write("\n", 1)) {
                logged_return(false);
            }
            if (eol_found) {
                // hard break after a character right before line end
                if (vm->config.keep_indentation) {
                    vm_indent_reset(vm);
                }
                sol = bytes + n_bytes;
                offset = 0;
                vm_eow_reset(vm);
                vm->state = VM_LINE;
                continue;
            }
            if (vm->config.keep_indentation) {
                if (!vm_indent(vm)) {
                    logged_return(false);
                }
            }
            vm->state = VM_WORD;
        }
        else if (vm->state == VM_LINE)
        {
            if (vm->config.keep_indentation) {
                if (!vm->indent_hanging && ws_found) {
                    if (!vm_indent_feed(vm, bytes, n_bytes, width)) {
                        logged_return(false);
                    }
                    continue;
                } else if (vm->config.hang_punctuation) {
                    bool valid = false;

                    if (vm->config.punctuation == NULL) {
                        valid = is_punctuation(NULL, NULL, codepoint,
                                               vm->config.ascii_mode);
                    } else {
                        char buf[5];
                        memcpy(buf, bytes, n_bytes);
                        buf[n_bytes] = '\0';
                        valid = is_punctuation(vm->config.punctuation, buf, 0,
                                               vm->config.ascii_mode);
                    }
                    if (valid) {
                        // assert cancelled for Markdown? e.g. "*   list item"
                        //debug_assert(!ws_found);

                        for (size_t k = 0; k < width; ++k) {
                            if (!vm_indent_feed(vm, " ", 1, 1)) {
                                logged_return(false);
                            }
                        }
                        vm->indent_hanging = true;
                        word_end = NULL;
                        continue;
                    }
                }
                vm->state = VM_WORD;

                if (vm->indent_width >= vm->config.max_width) {
                    debug_assert(vm->state != VM_FULL);

                    vm->state = VM_FULL;
                    vm_indent_reset(vm);
                }
            } else {
                vm->state = VM_WORD;
            }
        }

        debug_assert(vm->state != VM_LINE);
        debug_assert(vm->state != VM_WRAP);
        debug_assert(sol <= bytes);

        if (vm->state == VM_FULL)
        {
            debug_assert(vm->indent_size == 0);

            if (eol_found) {
                if (!vm->config.write(sol, bytes - sol + n_bytes)) {
                    logged_return(false);
                }
                sol = bytes + n_bytes;
                offset = 0;
                vm_eow_reset(vm);
                vm->state = VM_LINE;
                continue;
            }
        }
        // TODO: https://www.unicode.org/reports/tr29/#Word_Boundaries
        else if (offset > vm->config.max_width)
        {
            // TODO: do not break before the long word; truncate it
            //     |    oh a_long_word_that_fits_|not_the_next_line
            //     |      ^                      |
            //     |oh a_long_word_that_fits_not_|the_next_line
            //     |  ^                          |
            //     :                             :
            //     |    oh a_long_word_that_fits_|
            //     |    not_the_next_line        |
            //     |oh a_long_word_that_fits_not_|
            //     |the_next_line                |
            if (vm->config.break_at_spaces && vm->eow > 0) {
                debug_assert(vm->eow > sol - vm->line);

                if (!vm->config.write(sol, vm->eow - (sol - vm->line))) {
                    logged_return(false);
                }
                sol = vm->line + vm->eow + vm->eow_ss;
                offset = vm->indent_width + vm->eow_ww;
                debug_assert(sol <= bytes + n_bytes);

                // because of zero-width whitespace like --tab=0
                if (offset > vm->config.max_width) {
                    debug_assert(vm->eow_ww > 0);
                    debug_assert(width > 0);
                    debug_assert(!ws_found && !eol_found);

                    n_bytes = 0;
                    offset -= width;
                    vm_eow_reset(vm);
                    vm->state = VM_WRAP;
                    continue;
                }

                if (!eol_found) {
                    vm_eow_reset(vm);  // next, skip whitespace
                    vm->state = VM_WRAP;
                } else {
                    debug_assert(vm->indent_width + vm->eow_ww
                                 <= vm->config.max_width);

                    if (!vm->config.write("\n", 1)) {
                        logged_return(false);
                    }
                    if (vm->config.keep_indentation) {
                        if (!vm_indent(vm)) {
                            logged_return(false);
                        }
                        vm_indent_reset(vm);
                    }
                    if (!vm->config.write(sol, bytes - sol + n_bytes)) {
                        logged_return(false);
                    }
                    sol = bytes + n_bytes;
                    offset = 0;
                    vm_eow_reset(vm);
                    vm->state = VM_LINE;
                }
                continue;
            } else {
                size_t advance = 0;
                bool t = ws_found && vm->config.break_at_spaces && sol != bytes;

                if (!eol_found && !t) {
                    // avoid infinite loop
                    if (sol == bytes ||
                            // character too wide
                            offset - width == vm->indent_width) {
                        advance = n_bytes;
                        word_end = NULL;
                    }
                    if (!ws_found) {
                        // TODO: continue to collect all zero-width characters
                    }
                }

                // TODO: break at grapheme clusters? anyway damn ligature
                if (!vm->config.write(sol, bytes - sol + advance)) {
                    logged_return(false);
                }
                // no need to recalculate tab width here if n_bytes=0
                n_bytes = advance;
                sol = bytes + advance;
                offset = vm->indent_width;
                vm_eow_reset(vm);  // next, skip whitespace
                vm->state = VM_WRAP;
                continue;
            }
        }
        else if (eol_found)
        {
            debug_assert(offset <= vm->config.max_width);

            if (!vm->config.write(sol, bytes - sol + n_bytes)) {
                logged_return(false);
            }
            if (vm->config.keep_indentation) {
                vm_indent_reset(vm);
            }
            sol = bytes + n_bytes;
            offset = 0;
            vm_eow_reset(vm);
            vm->state = VM_LINE;
            continue;
        }
        else debug_assert(offset <= vm->config.max_width);
    }

    if (vm->state == VM_FULL || vm->stopped) {
        if (vm->state == VM_WRAP && bytes > sol) {
            if (!vm->config.write("\n", 1)) {
                logged_return(false);
            }
            if (vm->config.keep_indentation && !vm_indent(vm)) {
                logged_return(false);
            }
        }
        if (!vm->config.write(sol, bytes - sol)) {
            logged_return(false);
        }
        vm->cursor = 0;
        vm->cursor_offset = offset;
        vm_line_shift(vm, bytes - vm->line);
        vm_eow_reset(vm);
    } else {
        if (vm->eow > sol - vm->line) {
            vm->eow -= sol - vm->line;
        } else {
            vm_eow_reset(vm);
        }
        vm->cursor = bytes - sol;
        vm->cursor_offset = offset;
        vm->cursor_at_word = word_end != NULL;
        vm_line_shift(vm, sol - vm->line);
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Write indent.
\*/
static bool vm_indent(ufold_vm_t* vm)
{
    debug_assert(vm->config.keep_indentation);

    if (vm->indent_size > 0) {
        debug_assert(vm->indent_width >= 0);  // zero-width tab?
        debug_assert(vm->indent != NULL);

        if (!vm->config.write(vm->indent, vm->indent_size)) {
            logged_return(false);
        }
    }
    return true;
}

/*\
 / DESCRIPTION
 /   Extend line indent.
\*/
static bool vm_indent_feed(ufold_vm_t* vm,
                           const void* bytes, size_t size, size_t width)
{
    debug_assert(vm->config.keep_indentation);

    // check overflow
    if (vm->indent_width + width < width) {
        logged_return(false);
    }
    if (vm->indent_bufsize - vm->indent_size <= size) {
        size_t bufsize = vm->indent_size + size + 1;

        // check overflow
        if (bufsize <= size) {
            logged_return(false);
        }
        debug_assert(try_align(bufsize) >= bufsize);

        bufsize = try_align(bufsize);

        uint8_t* buf = vm_realloc(vm, vm->indent, bufsize);

        if (buf == NULL) {
            logged_return(false);
        }
        vm->indent = buf;
        vm->indent_bufsize = bufsize;
    }

    memcpy(vm->indent + vm->indent_size, bytes, size);
    vm->indent_size += size;
    vm->indent_width += width;
    vm->indent[vm->indent_size] = '\0';
    return true;
}

/*\
 / DESCRIPTION
 /   Reset line indent.
\*/
static void vm_indent_reset(ufold_vm_t* vm)
{
    debug_assert(vm->config.keep_indentation);

    // keep vm->indent and vm->indent_bufsize intact
    vm->indent_size = 0;
    vm->indent_width = 0;
    vm->indent_hanging = false;
}

static void vm_eow_reset(ufold_vm_t* vm)
{
    vm->eow = 0;
    vm->eow_ss = 0;
    vm->eow_ww = 0;
}
