#ifndef UFOLD_UTF8_H
#define UFOLD_UTF8_H

#include <stddef.h>
#include <stdint.h>
#include "stdbool.h"

/*\
 / DESCRIPTION
 /   Get the length of a valid UTF-8 byte sequence by checking its first byte.
 /
 / RETURN
 /   0 :: invalid byte
 /   + :: length of the complete sequence
\*/
size_t utf8_valid_length(const uint8_t byte);

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
bool utf8_calc_width(
        const uint8_t* bytes, const size_t size, const size_t tab_width,
        size_t* line_offset);

/*\
 / DESCRIPTION
 /   Find invalid unit in the UTF-8 byte sequence.
 /   Only invalid successive byte is reported when the sequence is incomplete.
 /
 / RETURN
 /   BEAF :: address of the invalid byte
 /   NULL :: no invalid byte found
\*/
const uint8_t* utf8_validate(const uint8_t* bytes, const size_t size);

/*\
 / DESCRIPTION
 /   Sanitize the buffer in place for valid UTF-8 byte sequence.
 /
 / PARAMETERS
 /   ascii --> whether to use ASCII characters only
 /
 / RETURN
 /   N :: new size of buffer
\*/
size_t utf8_sanitize(uint8_t* bytes, const size_t size, const bool ascii);

#endif  /* UFOLD_UTF8_H */
