#include "utf8.h"
#include "utils.h"

bool is_controlchar(utf8proc_int32_t codepoint)
{
    switch (codepoint) {
        case '\n': case '\t': return false;
        // TODO: deal with \b \e \f \r \v
        case '\b': case '\033': case '\f': case '\r': case '\v': return true;
    }
    return codepoint < 0x20 || codepoint == 0x7F ||
        (codepoint > 0x7F
         && utf8proc_category(codepoint) == UTF8PROC_CATEGORY_CC);
}

bool is_whitespace(utf8proc_int32_t codepoint)
{
    return codepoint == ' ' || codepoint == '\t' ||
        (utf8proc_category(codepoint) == UTF8PROC_CATEGORY_ZS
         && !is_linefeed(codepoint));
}

bool is_linefeed(utf8proc_int32_t codepoint)
{
    return codepoint == '\n' ||
        // TODO: recalculate tab width
        (false && (codepoint == '\r' ||  // TODO: indent with '\r'?
                   codepoint == '\v' ||  // TODO: indent with '\v'
                   codepoint == '\f' ||  // TODO: indent with '\f'?
                   // TODO: need browser&terminal support, isatty()?
                   utf8proc_category(codepoint) == UTF8PROC_CATEGORY_ZL));
}

bool has_linefeed(const uint8_t* bytes, size_t size)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0) {
            n_bytes = 1;
            continue;
        }
        if (is_linefeed(codepoint)) {
            return true;
        }
    }
    return false;
}

bool find_eol(const uint8_t* bytes, size_t size, size_t tab_width,
              const uint8_t** index, size_t* line_width, bool* linefeed_found)
{
    const uint8_t* new_index = bytes;
    bool found = false;

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

        new_index = bytes + i + n_bytes;

        if (is_linefeed(codepoint)) {
            found = true;
            break;
        }
    }

    if (!utf8_calc_width(bytes, new_index - bytes, tab_width, line_width)) {
        logged_return(false);
    }
    *index = new_index;
    *linefeed_found = found;
    return true;
}

bool skip_width(const uint8_t* bytes, size_t size,
                size_t tab_width, size_t max_width,
                const uint8_t** index, size_t* line_width)
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
        n_bytes = utf8proc_iterate(bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0) {
            logged_return(false);
        }

        size_t width = new_width;

        if (!utf8_calc_width(new_index, n_bytes, tab_width, &width)) {
            logged_return(false);
        }
        if (width < new_width) {
            logged_return(false);  // TODO: '\b' and the likes, isatty()?
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
                const uint8_t** index, size_t* line_width)
{
    const uint8_t* new_index = bytes;

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

        if (!is_whitespace(codepoint)) {
            break;
        }

        new_index = bytes + i + n_bytes;
    }

    if (!utf8_calc_width(bytes, new_index - bytes, tab_width, line_width)) {
        logged_return(false);
    }
    *index = new_index;
    return true;
}

bool break_line(const uint8_t* bytes, size_t size, bool with_space,
                size_t tab_width, size_t max_width,
                const uint8_t** last_word_end, const uint8_t** first_linefeed,
                const uint8_t** end, size_t* line_width)
{
    const uint8_t* new_index = bytes;
    size_t new_width = *line_width;
    size_t next_width = new_width;
    size_t alt_width = new_width;

    const uint8_t* linefeed = NULL;
    const uint8_t* word_ind = NULL;
    const uint8_t* word_end = NULL;
    const uint8_t* space_end = NULL;

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

        if (with_space) {
            if (!is_whitespace(codepoint)) {
                word_ind = bytes + i + n_bytes;
            } else {
                if (word_ind != NULL) {
                    space_end = NULL;
                    word_end = word_ind;
                    alt_width = new_width;
                    word_ind = NULL;
                }
            }
        }

        next_width = new_width;

        if (!utf8_calc_width(new_index, n_bytes, tab_width, &next_width)) {
            logged_return(false);
        }
        if (next_width < new_width) {
            logged_return(false);  // TODO: '\b' and the likes, isatty()?
        }
        if (!with_space) {
            if (next_width > max_width) {
                break;
            }
        } else {
            if (word_ind != NULL && next_width > max_width) {
                break;
            }
            if (is_whitespace(codepoint)) {
                if (next_width > max_width) {
                    break;
                }
                space_end = bytes + i + n_bytes;
                alt_width = next_width;
            }
        }

        new_width = next_width;
        new_index = bytes + i + n_bytes;

        if (new_width <= max_width && is_linefeed(codepoint)) {
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
