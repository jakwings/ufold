#ifndef UFOLD_UTILS_H
#define UFOLD_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include "stdbool.h"
#include "../utf8proc/utf8proc.h"

#include <stdio.h>
#define log(fmt, ...) \
do { \
    (void)fflush(stdout); \
    (void)fprintf(stderr, (fmt), __VA_ARGS__); \
} while (0)

#ifdef UFOLD_DEBUG
#   undef NDEBUG
#   include <assert.h>
#   define debug_assert(yes) assert(yes)
#   define logged_return(x) \
do { \
    if (!(x)) { \
        (void)fflush(stdout); \
        (void)fprintf(stderr, "[FAILURE] from file \"%s\" line %d: %s()\n", \
                      __FILE__, __LINE__, __func__); \
    } \
    return (x); \
} while (0)
#else
#   define debug_assert(yes) ((void)0)
#   define logged_return(x) return(x)
#endif

bool is_linefeed(utf8proc_int32_t codepoint, bool ascii_mode);

bool is_controlchar(utf8proc_int32_t codepoint, bool ascii_mode);

bool is_whitespace(utf8proc_int32_t codepoint, bool ascii_mode);

bool is_hanging_punctuation(utf8proc_int32_t codepoint, bool ascii_mode);

bool is_punctuation(const char* punctuation, const char* sequence,
                    utf8proc_int32_t codepoint, bool ascii_mode);

bool check_punctuation(const char* bytes, size_t size, bool ascii_mode);

bool has_linefeed(const uint8_t* bytes, size_t size, bool ascii_mode);

/*\
 / DESCRIPTION
 /   Get the length of a valid UTF-8 byte sequence by checking its first byte.
 /
 / RETURN
 /   0 :: invalid byte
 /   + :: length of the complete sequence
\*/
size_t utf8_valid_length(uint8_t byte);

/*\
 / DESCRIPTION
 /   Get the width associated with an isolated codepoint.
\*/
int get_charwidth(utf8proc_int32_t codepoint, bool ascii_mode);

/*\
 / DESCRIPTION
 /   Calculate the width of an elastic horizontal TAB.
 /   Zero-width TAB may not be supported in some environments.
 /
 / PARAMETERS
 /     tab_width --> maximum TAB width (columns)
 /   line_offset --> distance from the start of line (columns)
\*/
static inline size_t calc_tab_width(size_t tab_width, size_t line_offset)
{
    return tab_width > 1 ? (tab_width - line_offset % tab_width) : tab_width;
}

/*\
 / DESCRIPTION
 /   Calculate the total width of lines of text with elastic TABs in mind.
 /
 / PARAMETERS
 /      tab_width --> maximum TAB width (columns)
 /   *line_offset <-> distance from the start of line (columns)
 /
 / RETURN
 /    true :: success
 /   false :: failure
\*/
bool calc_width(const uint8_t* bytes, size_t size, size_t tab_width,
                size_t* line_offset, bool ascii_mode);

/*\
 / DESCRIPTION
 /   Sanitize the byte per ASCII.
 /
 / PARAMETERS
 /   byte <-> unchecked byte
 /
 / RETURN
 /   byte :: a good byte
\*/
uint8_t ascii_sanitize(uint8_t byte);

/*\
 / DESCRIPTION
 /   Sanitize the buffer in place for valid UTF-8 byte sequence.
 /
 / PARAMETERS
 /   bytes <-> unchecked byte sequence
 /    size --> number of bytes
 /
 / RETURN
 /   N :: new size of buffer
\*/
size_t utf8_sanitize(uint8_t* bytes, size_t size);

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
bool find_eol(const uint8_t* bytes, size_t size, size_t tab_width,
              const uint8_t** index, size_t* line_width, bool* linefeed_found,
              bool ascii_mode);

/*\
 / DESCRIPTION
 /   Calculate the index after advancing the pointer by some columns.
 /   Skip at least one column when max_width is greater than zero.
 /   Always stop at newline despite the previous rule.
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
bool skip_width(const uint8_t* bytes, size_t size,
                size_t tab_width, size_t max_width,
                const uint8_t** index, size_t* line_width, bool ascii_mode);

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
bool skip_space(const uint8_t* bytes, size_t size, size_t tab_width,
                const uint8_t** index, size_t* line_width, bool ascii_mode);

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
bool break_line(const uint8_t* bytes, size_t size, bool with_space,
                size_t tab_width, size_t max_width,
                const uint8_t** last_word_end, const uint8_t** first_linefeed,
                const uint8_t** end, size_t* line_width, bool ascii_mode);

/*\
 / DESCRIPTION
 /   Try to round up the number to a power of 2 then minus 1.
 /   Return a number no smaller than the original.
\*/
size_t try_align(size_t x);

/*\
 / DESCRIPTION
 /   Add two number with overflow check and store the result in *a.
 /   Return true if no overflow happened.
\*/
static inline bool add(size_t* a, size_t b)
{
    return !((*a += b) < b);
}


/*\
 / DESCRIPTION
 /   Multiply two number with overflow check and store the result in *a.
 /   Return true if no overflow happened.
\*/
static inline bool mul(size_t* a, size_t b)
{
    size_t d = *a;
    *a = d * b;
    return b == 0 || !(d > SIZE_MAX / b);
}

static inline size_t max(size_t a, size_t b)
{
    return a > b ? a : b;
}

static inline size_t min(size_t a, size_t b)
{
    return a < b ? a : b;
}

/*\
 / DESCRIPTION
 /   Parse a non-negative integer and store the result in *num.
\*/
bool parse_integer(const char* str, size_t* num);

#endif  /* UFOLD_UTILS_H */
