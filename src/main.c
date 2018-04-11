#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdbool.h"
#include "vm.h"

#define PROGRAM "ufold"
#define VERSION "0.0.0"

#define COPYRIGHT "Copyright (c) 2018 J.W https://github.com/jakwings/ufold"
#define LICENSE "License: https://opensource.org/licenses/ISC"
#define ISSUES "https://github.com/jakwings/ufold/issues"

#define warn(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", __VA_ARGS__)

#define P PROGRAM

const char* const manual =
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
"                Terminals may not support zero-width TABs.\n"
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
"         " P " will concatenate all files' content"
          " as if there is only a single source of input."
          " i.e these two bash commands are equivalent:\n"
"                " P " file1 file2 ;\n"
"                cat file1 file2 | " P " ;\n"
"\n"
"         More to note:\n"
"                When the indent occupies no less columns than the maximum,"
                 " the corresponding line will not be wrapped but kept as is.\n"
"\n"
"                When a fragment contaning no spaces exceeds the maximum width,"
                 " there will be a hard break inside the text.\n"
"\n"
"                Byte sequences that are not conforming with UTF-8 encoding"
                 " will be filtered before output. The --bytes (-b) option will"
                 " enforce the ASCII encoding in order to sanitize the input.\n"
"\n"
"  COPYRIGHT\n"
"         " COPYRIGHT "\n"
"\n"
"         " LICENSE "\n"
"\n"
;

const char* const usage =
"USAGE\n"
"    ufold [option]... [file]...\n"
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

    if (!ufold_vm_feed(vm, s, n) || !ufold_vm_stop(vm)) {
        if (errno != 0) {
            perror(NULL);
        } else {
            warn("unknown error, please report bugs to %s", ISSUES);
        }
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

    for (const char* p = str; *p != '\0'; p++) {
        if (!('0' <= *p && *p <= '9')) {
            return false;
        }
        n = n * 10 + (*p - '0');  // overflow?
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
    static const struct option opts[] = {
        {"width",    required_argument,  NULL,  'w'},
        {"tab",      required_argument,  NULL,  't'},
        {"spaces",   no_argument,        NULL,  's'},
        {"bytes",    no_argument,        NULL,  'b'},
        {"help",     no_argument,        NULL,  'h'},
        {"version",  no_argument,        NULL,  'V'},
        {NULL,       0,                  NULL,  0 },
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

    while ((c = getopt_long(*argc, *argv, "w:t:isbhV", opts, NULL)) != -1) {
        switch (c) {
            case 'i': to_keep_indentation = true; break;
            case 's': to_break_at_spaces = true; break;
            case 'b': to_truncate_bytes = true; break;
            case 'V': to_print_version = true; break;
            case ':': return false;
            case '?': return false;
            case 'h':
                if (!strcmp("--help", (*argv)[optind-1])) {
                    to_print_manual = true;
                } else {
                    to_print_help = true;
                }
                break;
            case 't':
                if (!parse_integer(optarg, &tab_width)) {
                    warn("%s", "-t, --tab requires a non-negative integer");
                    return false;
                }
                break;
            case 'w':
                if (!parse_integer(optarg, &max_width)) {
                    warn("%s", "-w, --width requires a non-negative integer");
                    return false;
                }
                break;
            default:
                warn("unhandled cases, please report bugs to %s", ISSUES);
                exit(EXIT_FAILURE);
        }
    }
    *argc -= optind;
    *argv += optind;

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
    char* line = NULL;
    size_t capacity = 0;
    ssize_t size = -1;

    while ((size = getline(&line, &capacity, stream)) > 0) {
        if (!ufold_vm_feed(vm, line, size)) {
            free(line);
            return false;
        }
    }
    free(line);

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

    if (vm == NULL) goto FAIL;

    if (argc > 0) {
        for (size_t i = 0; i < argc; i++) {
            if ((stream = fopen(argv[i], "rb")) == NULL) goto FAIL;
            if (!wrap_input(vm, stream)) goto FAIL;
            if (fclose(stream) != 0) goto FAIL;
        }
    } else if (!wrap_input(vm, stream)) goto FAIL;

    if (ferror(stream)) {
FAIL:
        if (errno != 0) {
            perror(NULL);
        } else {
            warn("unknown error, please report bugs to %s", ISSUES);
        }
        exitcode = EXIT_FAILURE;

        fclose(stream);  // whatever
    }

    // flush all output
    if (!ufold_vm_stop(vm)) {
        goto FAIL;
    }
    ufold_vm_free(vm);

    return exitcode;
}
