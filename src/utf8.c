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
        if (n_bytes < 0) {
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
            // A TAB or LF right before it can void '\b', what about others?
            logged_return(false);  // TODO: '\b' and the likes, isatty()?
        }

        // TODO: \r \v \f
        if (is_linefeed(codepoint)) {
            offset = 0;
        }
    }
    *line_offset = new_offset;
    return true;
}

size_t utf8_sanitize(uint8_t* bytes, size_t size, bool ascii)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if ((ascii && bytes[i] > 0x7F) || utf8_valid_length(bytes[i]) == 0) {
            bytes[i] = '?';
            n_bytes = 1;
            continue;
        }

        // NOTE: surrogates are invalid
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
