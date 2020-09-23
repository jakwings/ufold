#include <string.h>
#include "../utf8proc/utf8proc.h"
#include "utils.h"

bool is_linefeed(utf8proc_int32_t codepoint, bool ascii_mode)
{
    (void)ascii_mode;

    return codepoint == '\n';
}

bool is_controlchar(utf8proc_int32_t codepoint, bool ascii_mode)
{
    if (codepoint == '\n' || codepoint == '\t' ||
            is_linefeed(codepoint, ascii_mode)) {
        return false;
    }
    return (codepoint >= 0 && codepoint <= 0x1F) || codepoint == 0x7F ||
        (!ascii_mode && codepoint > 0x7F && codepoint <= 0x10FFFF
         && utf8proc_category(codepoint) == UTF8PROC_CATEGORY_CC);
}

bool is_whitespace(utf8proc_int32_t codepoint, bool ascii_mode)
{
    return codepoint == ' ' || codepoint == '\t' ||
        (!ascii_mode && codepoint > 0x7F && codepoint <= 0x10FFFF
         && !is_linefeed(codepoint, ascii_mode)
         // TODO?
         //&& codepoint != 0xA0    // [ZS] NO-BREAK SPACE
         //&& codepoint != 0x202F  // [ZS] NARROW NO-BREAK SPACE
         && utf8proc_category(codepoint) == UTF8PROC_CATEGORY_ZS);
}

bool is_hanging_punctuation(utf8proc_int32_t codepoint, bool ascii_mode)
{
    if (ascii_mode || (codepoint >= 0 && codepoint <= 0x7F)) {
        static const char punct[] = "\"`'([{";

       return memchr(punct, codepoint, sizeof(punct) - 1) != NULL;
    }
    // ‘ ’ “
    if (codepoint == 0x2018 || codepoint == 0x2019 || codepoint == 0x201C) {
        return true;
    }

    utf8proc_category_t category = utf8proc_category(codepoint);

    if (category == UTF8PROC_CATEGORY_PI || category == UTF8PROC_CATEGORY_PS) {
        return true;
    }
    return false;
}

bool is_punctuation(const char* punctuation, const char* sequence,
                    utf8proc_int32_t codepoint, bool ascii_mode)
{
    if (punctuation == NULL) {
        return is_hanging_punctuation(codepoint, ascii_mode);
    } else if (sequence != NULL) {
        return *sequence != '\0' && strstr(punctuation, sequence) != NULL;
    } else if (codepoint > 0 && codepoint <= 0x10FFFF) {
        if (!ascii_mode) {
            utf8proc_uint8_t buf[5];
            utf8proc_ssize_t size = utf8proc_encode_char(codepoint, buf);

            if (size > 0 && size <= 4) {
                debug_assert(buf[0] != '\0');

                buf[size] = '\0';
                return strstr(punctuation, (char*)buf) != NULL;
            }
        } else {
            return codepoint != '\0' && strchr(punctuation, codepoint) != NULL;
        }
    }
    return false;
}

bool check_punctuation(const char* bytes, size_t size, bool ascii_mode)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate((uint8_t*)bytes + i, size - i,
                                       &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }
        if (get_charwidth(codepoint, ascii_mode) < 0) {
            logged_return(false);
        }
        if (is_controlchar(codepoint, ascii_mode)) {
            return false;
        }
        if (is_whitespace(codepoint, ascii_mode)) {
            return true;  // interesting for Markdown? e.g. "*   list item"
        }
    }
    return true;
}

bool has_linefeed(const uint8_t* bytes, size_t size, bool ascii_mode)
{
#ifndef UFOLD_DEBUG
    (void)ascii_mode;
    return memchr(bytes, '\n', size) != NULL;
#else
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            n_bytes = 1;
            continue;
        }
        if (is_linefeed(codepoint, ascii_mode)) {
            return true;
        }
    }
    return false;
#endif
}

size_t utf8_valid_length(uint8_t byte)
{
    static const uint8_t lengths[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0,
    };
    return lengths[byte >> 3];
}

int get_charwidth(utf8proc_int32_t codepoint, bool ascii_mode)
{
    return !ascii_mode ? utf8proc_charwidth(codepoint) :
        (codepoint > 0x1F && codepoint < 0x7F ? 1 : 0);
}

// TODO: grapheme cluster?
bool calc_width(const uint8_t* bytes, size_t size, size_t tab_width,
                size_t* line_offset, bool ascii_mode)
{
    size_t new_offset = *line_offset;
    size_t offset = new_offset;  // for current line

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        int width = 0;

        if (codepoint == '\t') {
            width = calc_tab_width(tab_width, offset);
        } else {
            width = get_charwidth(codepoint, ascii_mode);
        }

        if (width >= 0) {
            if (!add(&offset, width)) {
                logged_return(false);
            }
            if (!add(&new_offset, width)) {
                logged_return(false);
            }
        } else {
            logged_return(false);
        }

        if (is_linefeed(codepoint, ascii_mode)) {
            offset = 0;
        }
    }
    *line_offset = new_offset;
    return true;
}

uint8_t ascii_sanitize(uint8_t byte)
{
    return !(byte > 0x7F || is_controlchar(byte, true)) ? byte : '?';
}

size_t utf8_sanitize(uint8_t* bytes, size_t size)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        // NOTE: surrogates are invalid
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            bytes[i] = '?';
            n_bytes = 1;
            continue;
        }

        if (is_controlchar(codepoint, false) ||
                get_charwidth(codepoint, false) < 0) {
            memset(bytes + i, '?', n_bytes);
            continue;
        }
    }

    return size;
}

bool find_eol(const uint8_t* bytes, size_t size, size_t tab_width,
              const uint8_t** index, size_t* line_width, bool* linefeed_found,
              bool ascii_mode)
{
    const uint8_t* new_index = bytes;
    bool found = false;

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        new_index = bytes + i + n_bytes;

        if (is_linefeed(codepoint, ascii_mode)) {
            found = true;
            break;
        }
    }

    if (!calc_width(bytes, new_index - bytes, tab_width, line_width,
                    ascii_mode)) {
        logged_return(false);
    }
    *index = new_index;
    *linefeed_found = found;
    return true;
}

bool skip_width(const uint8_t* bytes, size_t size,
                size_t tab_width, size_t max_width,
                const uint8_t** index, size_t* line_width, bool ascii_mode)
{
    if (max_width == 0) {
        *index = bytes;
        return true;
    }

    const uint8_t* new_index = bytes;
    size_t new_width = *line_width;

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }
        if (is_linefeed(codepoint, ascii_mode)) {
            break;
        }

        size_t width = new_width;

        if (!calc_width(new_index, n_bytes, tab_width, &width, ascii_mode)) {
            logged_return(false);
        }
        if (width < new_width) {
            logged_return(false);
        }
        if (new_width > *line_width && width > max_width) {
            break;
        }
        // else: skip at least one column

        new_width = width;
        new_index = bytes + i + n_bytes;

        if (width > max_width) {
            break;
        }
    }

    *index = new_index;
    *line_width = new_width;
    return true;
}

bool skip_space(const uint8_t* bytes, size_t size, size_t tab_width,
                const uint8_t** index, size_t* line_width, bool ascii_mode)
{
    const uint8_t* new_index = bytes;

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        if (!is_whitespace(codepoint, ascii_mode)) {
            break;
        }

        new_index = bytes + i + n_bytes;
    }

    if (!calc_width(bytes, new_index - bytes, tab_width, line_width,
                    ascii_mode)) {
        logged_return(false);
    }
    *index = new_index;
    return true;
}

bool break_line(const uint8_t* bytes, size_t size, bool with_space,
                size_t tab_width, size_t max_width,
                const uint8_t** last_word_end, const uint8_t** first_linefeed,
                const uint8_t** end, size_t* line_width, bool ascii_mode)
{
    const uint8_t* new_index = bytes;
    size_t new_width = *line_width;
    size_t next_width = new_width;
    size_t alt_width = new_width;

    const uint8_t* linefeed = NULL;
    const uint8_t* space_end = NULL;
    const uint8_t* word_ind = NULL;
    const uint8_t* word_end = NULL;
    size_t word_len = 0;

    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        if (!ascii_mode) {
            n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);
        } else {
            n_bytes = 1;
            codepoint = bytes[i];
        }

        if (n_bytes == 0) {
            if (size - i != 0) {
                logged_return(false);
            }
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            logged_return(false);
        }

        if (with_space) {
            if (!is_whitespace(codepoint, ascii_mode)) {
                word_ind = bytes + i + n_bytes;
            } else {
                if (word_ind != NULL) {
                    // no break at zero-width text after newline
                    if (word_len > 0) {
                        space_end = NULL;
                        word_end = word_ind;
                        alt_width = new_width;
                    }
                    word_ind = NULL;
                    word_len = 0;
                }
            }
        }

        next_width = new_width;

        if (!calc_width(new_index, n_bytes, tab_width, &next_width,
                        ascii_mode)) {
            logged_return(false);
        }
        if (next_width < new_width) {
            logged_return(false);
        }
        if (!with_space) {
            if (next_width > max_width) {
                break;
            }
        } else {
            if (word_ind != NULL && next_width > max_width) {
                break;
            }
            if (is_whitespace(codepoint, ascii_mode)) {
                if (next_width > max_width) {
                    break;
                }
                space_end = bytes + i + n_bytes;
                alt_width = next_width;
            } else {
                word_len += next_width - new_width;
            }
        }

        new_width = next_width;
        new_index = bytes + i + n_bytes;

        if (new_width <= max_width && is_linefeed(codepoint, ascii_mode)) {
            linefeed = bytes + i;
            break;
        }
    }

    if (word_end != NULL && next_width > max_width) {
        *last_word_end = word_end;
        *end = (space_end != NULL) ? space_end : word_end;
        *line_width = alt_width;
    } else {
        *last_word_end = NULL;
        *end = new_index;
        *line_width = new_width;
    }
    *first_linefeed = linefeed;

    return true;
}

size_t try_align(size_t x)
{
    if (x <= 0) {
        return 0;
    }
    size_t n = 1;

    while ((n & x) < x) {
        n = (n << 1) + 1;
    }
    return n;
}

bool parse_integer(const char* str, size_t* num)
{
    if (str == NULL || *str == '\0') {
        return false;
    }
    size_t n = 0;
    size_t k = 0;

    for (const char* p = str; *p != '\0'; ++p) {
        if (!('0' <= *p && *p <= '9')) {
            return false;
        }
        k = *p - '0';

        if (!(mul(&n, 10) && add(&n, k))) {
            n = SIZE_MAX;
        }
    }
    *num = n;

    return true;
}
