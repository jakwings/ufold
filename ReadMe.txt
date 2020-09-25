______________________________________________________________________________


                            THE FANTASTIC MANUAL

  NAME
         ufold -- wrap each input line to fit in specified width

  SYNOPSIS
         ufold [OPTION]... [FILE]...

         ufold [-w WIDTH | --width=WIDTH]
               [-t WIDTH | --tab=WIDTH]
               [-p[CHARS] | --hang[=CHARS]]
               [-i | --indent]
               [-s | --spaces]
               [-b | --bytes]
               [-h | --help]
               [-V | --version]
               [--] [FILE]...

  DESCRIPTION
         Wrap input lines from files and write to standard output.

         When no file is specified, or when a file path is empty, read from
         standard input.

         The letter u in the name stands for UTF-8, a superset of ASCII.

         -w, --width <width>
                Maximum columns for each line. Default: 78.
                Setting it to zero prevents wrapping.

         -t, --tab <width>
                Maximum columns for each tab. Default: 8.
                It does not change any setting of the terminal.

         -p, --hang[=<characters>]
                Hanging punctuation. Default: (none).
                Respect hanging punctuation while indenting.
                If characters are not provided, use the preset.

         -i, --indent
                Keep indentation for wrapped text.

         -s, --spaces
                Break lines at spaces.

         -b, --bytes
                Count bytes rather than columns.

         -h, --help
                Show help information.

         -V, --version
                Show version information.

         --
                All arguments after two dashes are not treated as options.

         The program will concatenate all files' content as if there is only a
         single source of input, i.e these two shell commands are equivalent:
                ufold file1 file2 ;
                cat file1 file2 | ufold ;

         More to note:
                CRLF (U+000D U+000A), CR (U+000D), LS (U+2028), PS (U+2029)
                and NEL (U+0085) will be normalized to LF (U+000A).

                When a line indent occupies no less columns than the maximum,
                the corresponding line will not be wrapped but kept as is.

                When the flag --spaces (-s) is given and a text fragment
                containing no spaces exceeds the maximum width, the program
                will still insert a hard break inside the text.

                Byte sequences that are not conforming with UTF-8 encoding
                will be filtered before output.  The flag --bytes (-b) will
                enforce the ASCII encoding in order to sanitize the input.
                Control-characters are always filtered.

  COPYRIGHT
         Copyright (c) 2018 J.W https://github.com/jakwings/ufold

         License: https://opensource.org/licenses/ISC

______________________________________________________________________________

______________________________________________________________________________


                            ADDITIONAL UTILITIES

  USAGE
      uwc [options] [files]

      Count bytes, characters, words, lines and the maximum width.
      By default, input must be encoded using the UTF-8 format.
      When no file is specified, read from standard input.

  OPTIONS
      -t, --tab <width>     Maximum columns for each tab.
      -b, --bytes           Count bytes.
      -c, --chars           Count chars (codepoints).
      -g, --graphs          Count word chars.
      -w, --words           Count words.
      -l, --lines           Count lines.
      -m, --width           Count the maximum line width.
      -G, --grapheme        Define chars as grapheme clusters.
      -L, --linear          No end-of-file be end-of-line.
      -n, --numb            Darn non-ASCII encoded text.
      -s, --strict          Warn about strange input.
      -v, --verbose         Show headers and summary.
      -h, --help            Show help information.
      -V, --version         Show version information.

  USAGE
      ucwidth [options] [files]

      Count widths of lines formated in the UTF-8 encoding.
      When no file is specified, read from standard input.

  OPTIONS
      -w, --width <width>   Maximum columns for each line.
      -t, --tab <width>     Maximum columns for each tab.
      [... uninteresting options left out here ...]
      -b, --bytes           Count bytes rather than columns.
      -h, --help            Show help information.
      -V, --version         Show version information.

______________________________________________________________________________
