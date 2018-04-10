______________________________________________________________________________


                            THE FANTASTIC MANUAL

  NAME
         ufold -- wrap each input line to fit in specified width

  SYNOPSIS
         ufold [OPTION]... [FILE]...

         ufold [-w WIDTH | --width=WIDTH]
               [-t WIDTH | --tab=WIDTH]
               [-i | --indent]
               [-s | --spaces]
               [-b | --bytes]
               [-h | --help]
               [-V | --version]
               [--] [FILE]...

  DESCRIPTION
         Wrap input lines from files and write to standard output.

         When no file is specified, read from standard input.

         The letter u in the name stands for UTF-8, a superset of ASCII.

         -w, --width <width>
                Maximum columns for each line. Default: 78.
                Setting it to zero prevents wrapping.

         -t, --tab <width>
                Maximum columns for each TAB character. Default: 8
                Terminals may not support zero-width TABs.

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

         ufold will concatenate all files' content as if there is only a
         single source of input. i.e these two bash commands are equivalent:
                ufold file1 file2 ;
                cat file1 file2 | ufold ;

         More to note:
                When the indent occupies no less columns than the maximum,
                the corresponding line will not be wrapped but kept as is.

                Byte sequences that are not conforming with for UTF-8
                encoding will be filtered before output. The --bytes (-b)
                option will enforce the ASCII encoding in order to sanitize
                the input.

  COPYRIGHT
         Copyright (c) 2018 J.W https://github.com/jakwings/ufold

         License: https://opensource.org/licenses/ISC

______________________________________________________________________________

