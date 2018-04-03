#include <string.h>
#include "../utf8proc/utf8proc.h"
#include "utils.h"
#include "utf8.h"

size_t utf8_valid_length(const uint8_t byte)
{
    static const uint8_t lengths[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0,
    };
    return lengths[byte >> 3];
}

// TODO: deal with surrogate? grapheme cluster?
bool utf8_calc_width(
        const uint8_t* bytes, const size_t size, const size_t tab_width,
        size_t* line_offset)
{
    size_t new_offset = *line_offset;
    size_t offset = new_offset;  // for current line

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0) {
            fail();
        }

        int width = 0;

        // FIXME: recalculate after wrapping
        if (codepoint == '\t') {
            // TODO: zero-width TAB may not be supported, isatty()?
            if (tab_width > 1) {
                width = tab_width - offset % tab_width;
            } else {
                width = tab_width;
            }
        } else {
            width = utf8proc_charwidth(codepoint);
        }

        if (width >= 0) {
            offset += width;
            new_offset += width;
        } else {
            // A TAB or LF right before it can void '\b', what about others?
            fail();  // TODO: '\b' and the likes, isatty()?
        }

        // TODO: \r \v \f
        if (is_linefeed(codepoint)) {
            offset = 0;
        }
    }
    *line_offset = new_offset;
    return true;
}

const uint8_t* utf8_validate(const uint8_t* bytes, const size_t size)
{
    static const uint8_t masks[] = {
        0x7F,  // [0b01111111]
        0xDF,  // [0b11011111, 0b10111111]
        0xEF,  // [0b11101111, 0b10111111, 0b10111111]
        0xF7,  // [0b11110111, 0b10111111, 0b10111111, 0b10111111]
    };
    const uint8_t* start = bytes;
    const uint8_t* end = bytes + size;

LOOP:
    while (start < end) {
        for (size_t i = 0; i < sizeof(masks)/sizeof(*masks); i++) {
            if (start[0] == (start[0] & masks[i])) {
                for (size_t j = 0; j < size - 1 && j < i; j++) {
                    if (start[1+j] != (start[1+j] & 0xBF)) {
                        return start + 1 + j;
                    }
                }
                // incomplete sequence?
                if (start + 1 + i < end) {
                    return NULL;
                }
                // skip one valid sequence
                start += 1 + i;
                goto LOOP;
            }
        }
        return start;
    }
    return NULL;
}

// TODO: deal with surrogate?
size_t utf8_sanitize(uint8_t* bytes, const size_t size, const bool ascii)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (ascii && bytes[i] > 0x7F) {
            bytes[i] = '?';
            n_bytes = 1;
            continue;
        }

        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0) {
            bytes[i] = '?';
            n_bytes = 1;
            continue;
        }

        // TODO: keep control chars except \b \e \f \r \v ?
        // TODO: strip '\b' and the likes, isatty()?
        if (is_controlchar(codepoint) || utf8proc_charwidth(codepoint) < 0) {
            memset(bytes + i, '?', n_bytes);
            continue;
        }
    }

    return size;
}
