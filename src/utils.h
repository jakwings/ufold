#ifndef UFOLD_UTILS_H
#define UFOLD_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include "stdbool.h"
#include "../utf8proc/utf8proc.h"

#ifndef NDEBUG
#include <stdio.h>
#define fail() \
    fprintf(stderr, "[FAILURE] from file \"%s\" line %d: %s()\n", \
            __FILE__, __LINE__, __func__); \
    return false;
#else
#define fail() return false
#endif

bool is_controlchar(const utf8proc_int32_t codepoint);

bool is_whitespace(const utf8proc_int32_t codepoint);

bool is_linefeed(const utf8proc_int32_t codepoint);

bool has_linefeed(const uint8_t* bytes, const size_t size);

/*\
 / DESCRIPTION
 /   Find the index of the first linefeed end, otherwise the buffer end.
 /
 / PARAMETERS
 /     tab_width --> maximum tab width (columns)
 /        *index <-- end of skipped content (exclusive)
 /   *line_width <-> distance from the start of line (columns)
 /        *found <-- whether linefeed is found
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool find_eol(const uint8_t* bytes, const size_t size, const size_t tab_width,
              const uint8_t** index, size_t* line_width, bool* linefeed_found);

/*\
 / DESCRIPTION
 /   Calculate the index after advancing the pointer by some columns.
 /
 / PARAM
 /     tab_width --> maximum tab width (columns)
 /     max_width --> maximum line width (columns)
 /        *index <-- end of skipped content (exclusive)
 /   *line_width <-> distance from the start of line (columns)
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool skip_width(const uint8_t* bytes, const size_t size,
                const size_t tab_width, const size_t max_width,
                const uint8_t** index, size_t* line_width);

/*\
 / DESCRIPTION
 /   Calculate the index after skipping consecutive spaces.
 /
 / PARAMETERS
 /     tab_width --> maximum tab width (columns)
 /        *index <-- end of skipped content (exclusive)
 /   *line_width <-> distance from the start of line (columns)
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool skip_space(const uint8_t* bytes, const size_t size, const size_t tab_width,
                const uint8_t** index, size_t* line_width);

/*\
 / DESCRIPTION
 /   Find a suitable point for line break, otherwise force break.
 /
 / PARAMETERS
 /        with_space --> whether to break at whitespace
 /         tab_width --> maximum tab width (columns)
 /         max_width --> maximum line width (columns)
 /    *last_word_end <-- index of last word end (exclusive)
 /   *first_linefeed <-- index of first linefeed start (inclusive)
 /              *end <-- index of line end or word+space end or buffer end
 /       *line_width <-> distance from the start of line (columns)
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool break_line(const uint8_t* bytes, const size_t size, const bool with_space,
                const size_t tab_width, const size_t max_width,
                const uint8_t** last_word_end, const uint8_t** first_linefeed,
                const uint8_t** end, size_t* line_width);

#endif  /* UFOLD_UTILS_H */
