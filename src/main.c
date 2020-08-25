#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optparse.h"
#include "stdbool.h"
#include "vm.h"

#define PROGRAM "ufold"
#define VERSION "1.0.0-omicron (Unicode 13.0.0)"

#define COPYRIGHT "Copyright (c) 2018 J.W https://github.com/jakwings/ufold"
#define LICENSE "License: https://opensource.org/licenses/ISC"
#define ISSUES "https://github.com/jakwings/ufold/issues"

#define warn(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", __VA_ARGS__)

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
"                When a line indent occupies no less columns than the maximum,"
                 " the corresponding line will not be wrapped but kept as is.\n"
"\n"
"                When the flag --spaces (-s) is given and a text fragment"
                 " containing no spaces exceeds the maximum width, the program"
                 " will still insert a hard break inside the text.\n"
"\n"
"                Byte sequences that are not conforming with UTF-8 encoding"
                 " will be filtered before output. The flag --bytes (-b) will"
                 " enforce the ASCII encoding in order to sanitize the input.\n"
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
"    -w, --width <width>  Maximum columns for each line.\n"
"    -t, --tab <width>    Maximum columns for each TAB character.\n"
"    -i, --indent         Keep indentation for wrapped text.\n"
"    -s, --spaces         Break lines at spaces.\n"
"    -b, --bytes          Count bytes rather than columns.\n"
"    -h, --help           Show help information.\n"
"    -V, --version        Show version information.\n"
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
    ufold_vm_t* vm = ufold_vm_new(config);

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

    for (const char* p = str; *p != '\0'; p++) {
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

static void print_manual(ufold_vm_config_t config)
{
    config.write = write_to_stdout;
    config.keep_indentation = true;
    config.break_at_spaces = true;
    config.truncate_bytes = false;

    exit(vwrite(manual, strlen(manual), config) ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void print_help(bool error, ufold_vm_config_t config)
{
    config.write = error ? write_to_stderr : write_to_stdout;
    config.keep_indentation = true;
    config.break_at_spaces = true;
    config.truncate_bytes = false;

    error = !vwrite(usage, strlen(usage), config) || error;
    exit(error ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void print_version(ufold_vm_config_t config)
{
    char* info = PROGRAM " " VERSION "\n" COPYRIGHT "\n" LICENSE "\n";
    config.write = write_to_stdout;

    exit(vwrite(info, strlen(info), config) ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool parse_options(int* argc, char*** argv, ufold_vm_config_t* config)
{
    struct optparse_long optspecs[] = {
        {"width",    'w',  OPTPARSE_REQUIRED},
        {"tab",      't',  OPTPARSE_REQUIRED},
        {"indent",   'i',  OPTPARSE_NONE},
        {"spaces",   's',  OPTPARSE_NONE},
        {"bytes",    'b',  OPTPARSE_NONE},
        {"help",     'h',  OPTPARSE_NONE},
        {"version",  'V',  OPTPARSE_NONE},
        {0}
    };

    size_t max_width = config->max_width;
    size_t tab_width = config->tab_width;
    bool to_print_help = false;
    bool to_print_manual = false;
    bool to_print_version = false;
    bool to_keep_indentation = false;
    bool to_break_at_spaces = false;
    bool to_truncate_bytes = false;

    int c = -1;
    struct optparse opt;

    optparse_init(&opt, *argv);
    while ((c = optparse_long(&opt, optspecs, NULL)) != -1) {
        switch (c) {
            case 'i': to_keep_indentation = true; break;
            case 's': to_break_at_spaces = true; break;
            case 'b': to_truncate_bytes = true; break;
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
    config->keep_indentation = to_keep_indentation;
    config->break_at_spaces = to_break_at_spaces;
    config->truncate_bytes = to_truncate_bytes;

    if (to_print_manual) print_manual(*config);
    else if (to_print_help) print_help(false, *config);
    else if (to_print_version) print_version(*config);

    return true;
}

static bool wrap_input(ufold_vm_t* vm, FILE* stream)
{
#define BUFSIZE 4096
    char buf[BUFSIZE];
    ssize_t size = -1;

    while ((size = fread(buf, 1, BUFSIZE, stream)) >= 0) {
        if (!ufold_vm_feed(vm, buf, size)) {
            return false;
        }
        if (feof(stream)) {
            break;
        }
    }

    return !ferror(stream);
}

int main(int argc, char** argv)
{
    if (sizeof(char) != sizeof(uint8_t)) {
        warn("sizeof(char)=%zu sizeof(uint8_t)=%zu",
                sizeof(char), sizeof(uint8_t));
        return EXIT_FAILURE;
    }
    int exitcode = EXIT_SUCCESS;

    ufold_vm_config_t config = {
        .max_width = MAX_WIDTH,
        .tab_width = TAB_WIDTH,
        .keep_indentation = false,
        .break_at_spaces = false,
        .truncate_bytes = false,
        .write = NULL,
        .realloc = NULL,
    };

    if (!parse_options(&argc, &argv, &config)) {
        fputc('\n', stderr);
        print_help(true, config);
    }

    FILE* stream = stdin;
    ufold_vm_t* vm = ufold_vm_new(config);

    if (vm == NULL) {
        warn("%s", "failed to create vm");
        goto FAIL;
    }

    if (argc > 0) {
        for (int i = 0; i < argc; i++) {
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

    return exitcode;
}
