#include <string.h>
#include "../utf8proc/utf8proc.h"
#include "utils.h"
#include "utf8.h"

size_t utf8_valid_length(uint8_t byte)
{
    static const uint8_t lengths[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0,
    };
    return lengths[byte >> 3];
}

// TODO: grapheme cluster?
bool utf8_calc_width(const uint8_t* bytes, size_t size, size_t tab_width,
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
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        int width = 0;

        if (codepoint == '\t') {
            // NOTE: zero-width TAB may not be supported.
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
            logged_return(false);
        }

        if (is_linefeed(codepoint)) {
            offset = 0;
        }
    }
    *line_offset = new_offset;
    return true;
}

uint8_t ascii_sanitize(uint8_t byte)
{
    return !(byte > 0x7F || is_controlchar(byte)) ? byte : '?';
}

size_t utf8_sanitize(uint8_t* bytes, size_t size, bool ascii)
{
    if (ascii) {
        for (size_t i = 0; i < size; ++i) {
            bytes[i] = ascii_sanitize(bytes[i]);
        }
        return size;
    }

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        // NOTE: surrogates are invalid
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            bytes[i] = '?';
            n_bytes = 1;
            continue;
        }

        if (is_controlchar(codepoint) || utf8proc_charwidth(codepoint) < 0) {
            memset(bytes + i, '?', n_bytes);
            continue;
        }
    }

    return size;
}
