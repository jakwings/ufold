#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "stdbool.h"
#include "optparse.h"
#include "utils.h"
#include "vm.h"

#define warn(fmt, ...) fprintf(stderr, "%s " fmt "\n", "[ERROR]", __VA_ARGS__)

#ifndef MAX_WIDTH
#define MAX_WIDTH 78
#endif

#ifndef TAB_WIDTH
#define TAB_WIDTH 8
#endif

#define PROGRAM "ufold"
#define VERSION "1.0.0-tau (Unicode 13.0.0)"

#define COPYRIGHT "Copyright (c) 2018 J.W https://github.com/jakwings/ufold"
#define LICENSE "License: https://opensource.org/licenses/ISC"
#define ISSUES "https://github.com/jakwings/ufold/issues"

#define P PROGRAM

static const char* const manual =
"\n"
"  NAME\n"
"         " P " -- wrap each input line to fit in specified width\n"
"\n"
"  SYNOPSIS\n"
"         " P " [OPTION]... [FILE]...\n"
"\n"
"         " P " [-w WIDTH | --width=WIDTH]\n"
"               [-t WIDTH | --tab=WIDTH]\n"
"               [-p[CHARS] | --hang[=CHARS]]\n"
"               [-i | --indent]\n"
"               [-s | --spaces]\n"
"               [-b | --bytes]\n"
"               [-h | --help]\n"
"               [-V | --version]\n"
"               [--] [FILE]...\n"
"\n"
"  DESCRIPTION\n"
"         Wrap input lines from files and write to standard output.\n"
"\n"
"         When no file is specified, read from standard input.\n"
"\n"
"         The letter u in the name stands for UTF-8, a superset of ASCII.\n"
"\n"
"         -w, --width <width>\n"
"                Maximum columns for each line. Default: 78.\n"
"                Setting it to zero prevents wrapping.\n"
"\n"
"         -t, --tab <width>\n"
"                Maximum columns for each TAB character. Default: 8.\n"
"                It does not change any setting of the terminal.\n"
"\n"
"         -p, --hang[=<characters>]\n"
"                Hanging punctuation. Default: (none).\n"
"                Respect hanging punctuation while indenting.\n"
"                If characters are not provided, use the preset.\n"
"\n"
"         -i, --indent\n"
"                Keep indentation for wrapped text.\n"
"\n"
"         -s, --spaces\n"
"                Break lines at spaces.\n"
"\n"
"         -b, --bytes\n"
"                Count bytes rather than columns.\n"
"\n"
"         -h, --help\n"
"                Show help information.\n"
"\n"
"         -V, --version\n"
"                Show version information.\n"
"\n"
"         --\n"
"                All arguments after two dashes are not treated as options.\n"
"\n"
"         The program will concatenate all files' content"
          " as if there is only a single source of input,"
          " i.e these two shell commands are equivalent:\n"
"                " P " file1 file2 ;\n"
"                cat file1 file2 | " P " ;\n"
"\n"
"         More to note:\n"
"                CRLF (U+000D U+000A), CR (U+000D), LS (U+2028), PS (U+2029)"
                 " and NEL (U+0085) will be normalized to LF (U+000A).\n"
"\n"
"                When a line indent occupies no less columns than the maximum,"
                 " the corresponding line will not be wrapped but kept as is.\n"
"\n"
"                When the flag --spaces (-s) is given and a text fragment"
                 " containing no spaces exceeds the maximum width, the program"
                 " will still insert a hard break inside the text.\n"
"\n"
"                Byte sequences that are not conforming with UTF-8 encoding"
                 " will be filtered before output.  The flag --bytes (-b) will"
                 " enforce the ASCII encoding in order to sanitize the input."
                 "  Control-characters are always filtered.\n"
"\n"
"  COPYRIGHT\n"
"         " COPYRIGHT "\n"
"\n"
"         " LICENSE "\n"
"\n"
;

static const char* const usage =
"USAGE\n"
"    " P " [option]... [file]...\n"
"\n"
"    Wrap input lines from files and write to standard output.\n"
"    When no file is specified, read from standard input.\n"
"\n"
"OPTIONS\n"
"    -w, --width <width>   Maximum columns for each line.\n"
"    -t, --tab <width>     Maximum columns for each TAB character.\n"
"    -p, --hang[=<chars>]  Hanging punctuation.\n"
"    -i, --indent          Keep indentation for wrapped text.\n"
"    -s, --spaces          Break lines at spaces.\n"
"    -b, --bytes           Count bytes rather than columns.\n"
"    -h, --help            Show help information.\n"
"    -V, --version         Show version information.\n"
;

static bool write_to_stdout(const void* s, size_t n)
{
    return (n > 0) ? (fwrite(s, n, 1, stdout) == 1) : true;
}

static bool write_to_stderr(const void* s, size_t n)
{
    return (n > 0) ? (fwrite(s, n, 1, stderr) == 1) : true;
}

static bool vwrite(const void* s, size_t n, ufold_vm_config_t config)
{
    ufold_vm_t* vm = ufold_vm_new(&config);

    if (vm == NULL || !ufold_vm_feed(vm, s, n) || !ufold_vm_stop(vm)) {
        if (errno != 0) {
            perror(NULL);
        } else {
            warn("unknown error, please report bugs to %s", ISSUES);
        }
        ufold_vm_free(vm);
        return false;
    }
    ufold_vm_free(vm);

    return true;
}

static bool parse_integer(const char* str, size_t* num)
{
    if (str == NULL) {
        return false;
    }
    size_t n = 0;
    size_t k = 0;

    for (const char* p = str; *p != '\0'; ++p) {
        if (!('0' <= *p && *p <= '9')) {
            return false;
        }
        k = *p - '0';

        if ((n > 0 && SIZE_MAX / n < 10) || n * 10 + k < n) {
            n = SIZE_MAX;
            continue;
        }
        n = n * 10 + k;
    }
    *num = n;

    return true;
}

static bool check_punctuation(const char* bytes, size_t size)
{
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;

    for (size_t i = 0; i < size; i += n_bytes) {
        n_bytes = utf8proc_iterate((uint8_t*)bytes + i, size - i, &codepoint);

        if (n_bytes == 0) {
            break;
        }
        if (n_bytes < 0 || n_bytes > 4) {
            return false;
        }
        if (is_whitespace(codepoint) || is_controlchar(codepoint) ||
                utf8proc_charwidth(codepoint) < 0) {
            return false;
        }
    }
    return true;
}

static void print_manual(ufold_vm_config_t config)
{
    config.write = write_to_stdout;
    config.hang_punctuation = false;
    config.keep_indentation = true;
    config.break_at_spaces = true;

    bool done = vwrite(manual, strlen(manual), config);
    debug_assert(done);

    exit(done ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void print_help(bool error, ufold_vm_config_t config)
{
    config.write = error ? write_to_stderr : write_to_stdout;
    config.hang_punctuation = false;
    config.keep_indentation = true;
    config.break_at_spaces = true;

    bool done = vwrite(usage, strlen(usage), config);
    debug_assert(done);

    exit((error || !done) ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void print_version(ufold_vm_config_t config)
{
    char* info = PROGRAM " " VERSION "\n" COPYRIGHT "\n" LICENSE "\n";
    config.write = write_to_stdout;
    config.max_width = 0;

    bool done = vwrite(info, strlen(info), config);
    debug_assert(done);

    exit(done ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool parse_options(int* argc, char*** argv, ufold_vm_config_t* config)
{
    struct optparse_long optspecs[] = {
        {"width",    'w',  OPTPARSE_REQUIRED},
        {"tab",      't',  OPTPARSE_REQUIRED},
        {"hang",     'p',  OPTPARSE_OPTIONAL},
        {"indent",   'i',  OPTPARSE_NONE},
        {"spaces",   's',  OPTPARSE_NONE},
        {"bytes",    'b',  OPTPARSE_NONE},
        {"help",     'h',  OPTPARSE_NONE},
        {"version",  'V',  OPTPARSE_NONE},
        {0}
    };

    size_t max_width = config->max_width;
    size_t tab_width = config->tab_width;
    char* punctuation = NULL;
    bool to_hang_punctuation = false;
    bool to_print_help = false;
    bool to_print_manual = false;
    bool to_print_version = false;
    bool to_keep_indentation = false;
    bool to_break_at_spaces = false;
    bool to_count_bytes = false;

    int c = -1;
    struct optparse opt;

    optparse_init(&opt, *argv);
    while ((c = optparse_long(&opt, optspecs, NULL)) != -1) {
        switch (c) {
            case 'i': to_keep_indentation = true; break;
            case 's': to_break_at_spaces = true; break;
            case 'b': to_count_bytes = true; break;
            case 'V': to_print_version = true; break;
            case '?':
                warn("%s", opt.errmsg);
                return false;
            case 'h':
                if (!strcmp("--help", (opt.argv)[opt.optind-1])) {
                    to_print_manual = true;
                } else {
                    to_print_help = true;
                }
                break;
            case 'p':
                if (opt.optarg == NULL) {
                    punctuation = NULL;
                    to_hang_punctuation = true;
                } else if (strlen(opt.optarg) > 0) {
                    if (!check_punctuation(opt.optarg, strlen(opt.optarg))) {
                        warn("option requires non-whitespace characters in the UTF-8 encoding -- '%c'", c);
                        return false;
                    }
                    punctuation = opt.optarg;
                    to_hang_punctuation = true;
                } else {
                    punctuation = NULL;
                    to_hang_punctuation = false;
                }
                break;
            case 't':
                if (!parse_integer(opt.optarg, &tab_width)) {
                    warn("option requires a non-negative integer -- '%c'", c);
                    return false;
                }
                break;
            case 'w':
                if (!parse_integer(opt.optarg, &max_width)) {
                    warn("option requires a non-negative integer -- '%c'", c);
                    return false;
                }
                break;
            default:
                warn("unhandled option '%c', please report to %s", c, ISSUES);
                exit(EXIT_FAILURE);
        }
    }
    *argc -= opt.optind;
    *argv += opt.optind;

    config->max_width = max_width;
    config->tab_width = tab_width;
    config->punctuation = punctuation;
    config->hang_punctuation = to_hang_punctuation;
    config->keep_indentation = to_keep_indentation;
    config->break_at_spaces = to_break_at_spaces;
    config->ascii_mode = to_count_bytes;

    if (to_print_manual) print_manual(*config);
    else if (to_print_help) print_help(false, *config);
    else if (to_print_version) print_version(*config);

    return true;
}

static bool wrap_input(ufold_vm_t* vm, FILE* stream)
{
    bool is_interactive = isatty(fileno(stream));

    if (!is_interactive) {
#define BUFSIZE 4096
        char buf[BUFSIZE];
        do {
            size_t size = fread(buf, 1, BUFSIZE, stream);
            if (ferror(stream)) {
                return false;
            }
            if (!ufold_vm_feed(vm, buf, size)) {
                return false;
            }
            if (has_linefeed((uint8_t*)buf, size) && !ufold_vm_flush(vm)) {
                return false;
            }
        } while (!feof(stream));
    } else {
        char c;
        do {
            size_t n = fread(&c, 1, 1, stream);
            (void)n;

            if (ferror(stream)) {
                return false;
            }
            if (!ufold_vm_feed(vm, &c, 1)) {
                return false;
            }
            if (is_linefeed(c) && !ufold_vm_flush(vm)) {
                return false;
            }
        } while (!feof(stream));
    }

    return true;
}

int main(int argc, char** argv)
{
    if (sizeof(char) != sizeof(uint8_t)) {
        warn("sizeof(char)=%zu sizeof(uint8_t)=%zu",
                sizeof(char), sizeof(uint8_t));
        return EXIT_FAILURE;
    }
    int exitcode = EXIT_SUCCESS;

    ufold_vm_config_t config;
    ufold_vm_config_init(&config);

    config.max_width = MAX_WIDTH;
    config.tab_width = TAB_WIDTH;
    config.punctuation = NULL;
    config.hang_punctuation = false;
    config.keep_indentation = false;
    config.break_at_spaces = false;
    config.ascii_mode = false;
    config.line_buffered = true;
    config.write = NULL;
    config.realloc = NULL;

    if (!parse_options(&argc, &argv, &config)) {
        fputc('\n', stderr);
        print_help(true, config);
    }

    FILE* stream = stdin;
    ufold_vm_t* vm = ufold_vm_new(&config);

    if (vm == NULL) {
        warn("%s", "failed to create vm");
        goto FAIL;
    }

    if (argc > 0) {
        for (int i = 0; i < argc; ++i) {
            if ((stream = fopen(argv[i], "rb")) == NULL) {
                warn("failed to open \"%s\"", argv[i]);
                goto FAIL;
            }
            if (!wrap_input(vm, stream)) {
                warn("failed to process \"%s\"", argv[i]);
                goto FAIL;
            }
            if (fclose(stream) != 0) {
                warn("failed to close \"%s\"", argv[i]);
                goto FAIL;
            }
        }
    } else if (!wrap_input(vm, stream)) {
        warn("%s", "failed to process stdin");
        goto FAIL;
    }

    if (ferror(stream)) {
FAIL:
        if (errno != 0) {
            perror(NULL);
            errno = 0;
        } else {
            warn("unknown error, please report bugs to %s", ISSUES);
        }
        exitcode = EXIT_FAILURE;

        if (stream != stdin) {
            fclose(stream);  // whatever
        }
    }

    // flush all output
    if (!ufold_vm_stop(vm)) {
        warn("%s", "failed to stop vm");
        goto FAIL;
    }
    ufold_vm_free(vm);

    debug_assert(exitcode == EXIT_SUCCESS);
    return exitcode;
}
