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
size_t utf8_valid_length(uint8_t byte);

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
bool utf8_calc_width(const uint8_t* bytes, size_t size, size_t tab_width,
                     size_t* line_offset);

/*\
 / DESCRIPTION
 /   Sanitize the buffer in place for valid UTF-8 byte sequence.
 /
 / PARAMETERS
 /   bytes <-> unchecked byte sequence
 /    size --> number of bytes
 /   ascii --> whether to use ASCII characters only
 /
 / RETURN
 /   N :: new size of buffer
\*/
size_t utf8_sanitize(uint8_t* bytes, size_t size, bool ascii);

#endif  /* UFOLD_UTF8_H */
