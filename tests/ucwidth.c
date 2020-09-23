#undef NDEBUG
#include <assert.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stdbool.h"
#include "../utf8proc/utf8proc.h"
#include "../src/optparse.h"
#include "../src/utils.h"

#define warn(fmt, ...) log("%s " fmt "\n", "[ERROR]", __VA_ARGS__)

#define error(msg) \
do { \
    warn("%s", msg); \
    assert(!"DAMN IT!"); \
} while (0)

#define BUFSIZE 4096

static const char* const usage =
"USAGE\n"
"    ucwidth [options] [files]\n"
"\n"
"    Count widths of lines from the output of ufold.\n"
"    When no file is specified, read from standard input.\n"
"\n"
"OPTIONS\n"
"    -w, --width <width>   Maximum columns for each line.\n"
"    -t, --tab <width>     Maximum columns for each tab.\n"
"    -p, --hang[=<chars>]  Hanging punctuation.\n"
"    -i, --indent          Keep indentation for wrapped text.\n"
"    -s, --spaces          Break lines at spaces.\n"
"    -b, --bytes           Count bytes rather than columns.\n"
"    -h, --help            Show help information.\n"
"    -V, --version         Show version information.\n"
;

typedef struct struct_config {
    size_t max_width;
    size_t tab_width;
    char* punctuation;
    bool hang_punctuation;  // XXX: no way to detect wrapped lines, e.g. AAAA""""....
    bool keep_indentation;  // XXX: no way to detect indent level
    bool break_at_spaces;   // e.g. " \t" -sw2 should become " \n\t"
    bool ascii_mode;
} Config;

static void parse_options(int* argc, char*** argv, Config* config)
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

    int c = -1;
    struct optparse opt;
    bool to_print_help = false;
    bool to_print_version = false;

    optparse_init(&opt, *argv);
    while ((c = optparse_long(&opt, optspecs, NULL)) != -1) {
        switch (c) {
            case 'i': config->keep_indentation = true; break;
            case 's': config->break_at_spaces = true; break;
            case 'b': config->ascii_mode = true; break;
            case 'V': to_print_version = true; break;
            case 'h': to_print_help = true; break;
            case 'p':
                if (opt.optarg == NULL) {
                    config->punctuation = NULL;
                    config->hang_punctuation = true;
                } else if (strlen(opt.optarg) > 0) {
                    if (!check_punctuation(opt.optarg, strlen(opt.optarg), config->ascii_mode)) {
                        warn("option requires non-whitespace characters in the UTF-8 encoding -- '%c'", c);
                        exit(1);
                    }
                    config->punctuation = opt.optarg;
                    config->hang_punctuation = true;
                } else {
                    config->punctuation = NULL;
                    config->hang_punctuation = false;
                }
                break;
            case 't':
                if (!parse_integer(opt.optarg, &config->tab_width)) {
                    warn("option requires a non-negative integer -- '%c'", c);
                    exit(1);
                }
                break;
            case 'w':
                if (!parse_integer(opt.optarg, &config->max_width)) {
                    warn("option requires a non-negative integer -- '%c'", c);
                    exit(1);
                }
                break;
            case '?':
                warn("%s", opt.errmsg);
                exit(1);
            default:
                error("unhandled option");
        }
    }
    *argc -= opt.optind;
    *argv += opt.optind;

    if (to_print_help) {
        printf("%s", usage);
        exit(0);
    }
    if (to_print_version) {
        printf("%s\n", VERSION);
        exit(0);
    }
}

static bool count_width(FILE* stream, const Config* config, bool* exceeded)
{
    utf8proc_uint8_t buf[BUFSIZE];
    utf8proc_int32_t codepoint = -1;
    utf8proc_int32_t old_point = -1;
    utf8proc_ssize_t n_bytes = -1;
    size_t size = 0;
    size_t index = 0;
    size_t width = 0;
    size_t owidth = 0;
    size_t lineno = 1;
    size_t length = 0;
    size_t indent = 0;
    bool indented = false;
    bool hanging = false;

    do {
        size += fread(buf + size, 1, BUFSIZE - size, stream);

        if (ferror(stream)) {
            warn("%s", "reading failed");
            logged_return(false);
        }

        for (index = 0; index < size; index += n_bytes) {
            old_point = codepoint;
            n_bytes = utf8proc_iterate(buf + index, size - index, &codepoint);

            if (n_bytes == 0) {
                if (size - index != 0) {
                    error("broken utf8proc");
                }
                break;
            } else if (n_bytes < 0 || n_bytes > 4) {
                if (size - index >= 4) {
                    warn("%s", "invalid byte sequence from input");
                    logged_return(false);
                }
                break;
            } else if (codepoint < 0 || codepoint > 0x10FFFF) {
                warn("%s", "invalid UTF-8 byte sequence from input");
                logged_return(false);
            } else if (config->ascii_mode && codepoint > 0x7F) {
                warn("%s", "invalid ASCII byte sequence from input");
                logged_return(false);
            } else if (is_controlchar(codepoint, config->ascii_mode)) {
                warn("%s", "unwanted control character from input");
                logged_return(false);
            } else if (get_charwidth(codepoint, config->ascii_mode) < 0) {
                warn("%s", "unwanted negative-width character from input");
                logged_return(false);
            } else if (codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x0085) {
                warn("%s", "unwanted line separators besides U+000A from input");
                logged_return(false);
            }

            owidth = width;
            width = SIZE_MAX - length + 1;

            if (codepoint == '\t') {
                if (config->tab_width > 1) {
                    width = config->tab_width - length % config->tab_width;
                } else {
                    width = config->tab_width;
                }
            } else {
                width = get_charwidth(codepoint, config->ascii_mode);
            }

            if (length + width < length) {
                error("integer overflow while counting length");
            }
            length += width;

            if (config->keep_indentation && !indented) {
                if (!hanging && is_whitespace(codepoint, config->ascii_mode)) {
                    indent += width;
                } else if (config->hang_punctuation) {
                   if (is_punctuation(config->punctuation, NULL, codepoint, config->ascii_mode)) {
                       indent += width;
                       hanging = true;
                   } else {
                       indented = true;
                   }
                } else {
                    indented = true;
                }
            }

            if (is_linefeed(codepoint, config->ascii_mode)) {
                if (printf("%zu\n", length) < 0) {
                    warn("%s", "writing failed");
                    logged_return(false);
                }
                if (config->max_width > 0
                        && indent < config->max_width
                        && (indent + owidth <= config->max_width ||
                            (config->break_at_spaces
                             && is_whitespace(old_point, config->ascii_mode)
                             && indent + owidth < length))
                        && length > config->max_width) {
                    *exceeded = true;
                    warn("%s at line %zu", "maximum length exceeded", lineno);
                }
                lineno += 1;
                length = 0;
                indent = 0;
                indented = false;
                hanging = false;
            }
        }
        owidth = width;

        size = size - index;
        memmove(buf, buf + index, size);
    } while (!feof(stream));

    if (index < size) {
        warn("%s", "invalid bytes from input");
        logged_return(false);
    }

    if (length > 0) {
        if (printf("%zu\n", length) < 0) {
            warn("%s", "writing failed");
            logged_return(false);
        }
        if (config->max_width > 0
                && indent < config->max_width
                && (indent + owidth <= config->max_width ||
                    (config->break_at_spaces
                     && is_whitespace(old_point, config->ascii_mode)
                     && indent + owidth < length))
                && length > config->max_width) {
            *exceeded = true;
            warn("%s at line %zu", "maximum length exceeded", lineno);
        }
    }

    return true;
}

int main(int argc, char** argv)
{
    const char* filepath = NULL;
    const char* alias = NULL;
    FILE* stream = NULL;
    bool exceeded = false;
    int i = -1;

    Config config = {
        .max_width = MAX_WIDTH,
        .tab_width = TAB_WIDTH,
        .punctuation = NULL,
        .hang_punctuation = false,
        .keep_indentation = false,
        .break_at_spaces = false,
        .ascii_mode = false,
    };

    parse_options(&argc, &argv, &config);

    if (argc > 0) {
        for (i = 0; i < argc; ++i) {
            filepath = argv[i];
            alias = *filepath != '\0' ? filepath : "";  // or "/dev/stdin"?
            stream = *filepath != '\0' ? fopen(filepath, "rb") : stdin;

            if (stream == NULL) {
                warn("failed to open \"%s\"", alias);
                goto FAIL;
            }
            if (!count_width(stream, &config, &exceeded)) {
                warn("failed to process \"%s\"", alias);
                goto FAIL;
            }
            if (stream != stdin && fclose(stream) != 0) {
                warn("failed to close \"%s\"", alias);
                goto FAIL;
            }
        }
    } else {
        stream = stdin;

        if (!count_width(stream, &config, &exceeded)) {
            warn("%s", "failed to process stdin");
            goto FAIL;
        }
    }

    if (stream != NULL && ferror(stream)) {
FAIL:
        if (errno != 0) {
            warn("%s", strerror(errno));
        } else {
            warn("%s", "unknown error");
        }
        (void)fclose(stream);  // whatever

        return 1;
    }

    return exceeded ? 1 : 0;
}
