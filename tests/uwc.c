#undef NDEBUG
#include <assert.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdbool.h"
#include "../utf8proc/utf8proc.h"
#include "../src/optparse.h"

#include "../src/utils.h"
#define is_linefeed(...) assert(!"typo")
#define is_controlchar(...) assert(!"typo")
#define is_whitespace(...) assert(!"typo")
#define is_wordchar(...) assert(!"typo")

#define warn(fmt, ...) log("%s " fmt "\n", "[ERROR]", __VA_ARGS__)

#define error(msg) \
do { \
    warn("%s", msg); \
    assert(!"DAMN IT!"); \
} while (0)

#define BUFSIZE 4096

// https://pubs.opengroup.org/onlinepubs/9699919799/utilities/wc.html

static const char* const usage =
"USAGE\n"
"    uwc [options] [files]\n"
"\n"
"    Count bytes, characters, words, lines and the maximum width.\n"
"    By default, input must be encoded using the UTF-8 format.\n"
"    When no file is specified, read from standard input.\n"
"\n"
"OPTIONS\n"
"    -t, --tab <width>     Maximum columns for each tab.\n"
"    -b, --bytes           Count bytes.\n"
"    -c, --chars           Count chars (codepoints).\n"
"    -g, --graphs          Count word chars.\n"
"    -w, --words           Count words.\n"
"    -l, --lines           Count lines.\n"
"    -m, --width           Count the maximum line width.\n"
"    -L, --linear          No end-of-file be end-of-line.\n"
"    -n, --numb            Darn non-ASCII encoded text.\n"
"    -s, --strict          Warn about strange input.\n"
"    -v, --verbose         Show headers and summary.\n"
"    -h, --help            Show help information.\n"
"    -V, --version         Show version information.\n"
;

typedef struct struct_record {
    const char* filepath;
    size_t bytes;
    size_t chars;
    size_t graphs;
    size_t words;
    size_t lines;
    size_t width;
} Record;

typedef struct struct_config {
    size_t tab_width;
    bool count_bytes;
    bool count_chars;
    bool count_graphs;
    bool count_words;
    bool count_lines;
    bool count_width;
    bool eof_not_eol;  // my life will go on
    bool numb_mode;    // ascii & wtf-8
    bool strict_mode;
    bool verbose_mode;
} Config;

static bool parse_options(int* argc, char*** argv, Config* config)
{
    static const struct optparse_long optspecs[] = {
        {"tab",      't',  OPTPARSE_REQUIRED},
        {"bytes",    'b',  OPTPARSE_NONE},
        {"chars",    'c',  OPTPARSE_NONE},
        {"graphs",   'g',  OPTPARSE_NONE},
        {"words",    'w',  OPTPARSE_NONE},
        {"lines",    'l',  OPTPARSE_NONE},
        {"width",    'm',  OPTPARSE_NONE},
        {"grapheme", 'G',  OPTPARSE_NONE},
        {"linear",   'L',  OPTPARSE_NONE},
        {"numb",     'n',  OPTPARSE_NONE},
        {"strict",   's',  OPTPARSE_NONE},
        {"verbose",  'v',  OPTPARSE_NONE},
        {"help",     'h',  OPTPARSE_NONE},
        {"version",  'V',  OPTPARSE_NONE},
        {0}
    };

    size_t tab_width = config->tab_width;
    bool to_print_help = false;
    bool to_print_version = false;
    bool to_count_everything = true;
    bool to_count_bytes = false;
    bool to_count_chars = false;
    bool to_count_graphs = false;
    bool to_count_words = false;
    bool to_count_lines = false;
    bool to_count_width = false;
    bool to_numb_mode = false;
    bool to_strict_mode = false;
    bool to_verbose_mode = false;
    bool to_eof_is_to_eol = true;

    int c = -1;
    struct optparse opt;
    optparse_init(&opt, *argv);

    while ((c = optparse_long(&opt, optspecs, NULL)) != -1) {
        switch (c) {
            case 't':
                if (!parse_integer(opt.optarg, &tab_width)) {
                    warn("option requires a non-negative integer -- '%c'", c);
                    return false;
                }
                break;
            case 'b': to_count_bytes = true; break;
            case 'c': to_count_chars = true; break;
            case 'g': to_count_graphs = true; break;
            case 'w': to_count_words = true; break;
            case 'l': to_count_lines = true; break;
            case 'm': to_count_width = true; break;
            case 'n': to_numb_mode = true; break;
            case 'L': to_eof_is_to_eol = false; break;
            case 's': to_strict_mode = true; break;
            case 'v': to_verbose_mode = true; break;
            case 'h': to_print_help = true; break;
            case 'V': to_print_version = true; break;
            case '?':
                warn("%s", opt.errmsg);
                return false;
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

    if (to_count_bytes || to_count_chars || to_count_graphs ||
            to_count_words || to_count_lines || to_count_width) {
        to_count_everything = false;
    }

    config->tab_width = tab_width;
    config->count_bytes = to_count_everything || to_count_bytes;
    config->count_chars = to_count_everything || to_count_chars;
    config->count_graphs = to_count_everything || to_count_graphs;
    config->count_words = to_count_everything || to_count_words;
    config->count_lines = to_count_everything || to_count_lines;
    config->count_width = to_count_everything || to_count_width;
    config->eof_not_eol = !to_eof_is_to_eol;
    config->numb_mode = to_numb_mode;
    config->strict_mode = to_strict_mode;
    config->verbose_mode = to_verbose_mode;

    return true;
}

static bool ist_linefeed(utf8proc_int32_t c, bool numb_mode)
{
    return c == '\n' ||
        (!numb_mode
         && (c == 0x2028 || c == 0x2029 || c == 0x0085));
}

static bool ist_controlchar(utf8proc_int32_t c, bool numb_mode)
{
    if (c == '\n' || c == '\t') {
        return false;
    }
    return (c >= 0 && c <= 0x1F) || c == 0x7F ||
        (!numb_mode
         && c > 0x7F
         && !ist_linefeed(c, numb_mode)
         && utf8proc_category(c) == UTF8PROC_CATEGORY_CC
         && c <= 0x10FFFF);
}

static bool ist_whitespace(utf8proc_int32_t c, bool numb_mode)
{
    return c == ' ' || c == '\t' ||
        (!numb_mode
         && c > 0x7F
         && !ist_linefeed(c, numb_mode)
         && utf8proc_category(c) == UTF8PROC_CATEGORY_ZS
         && c <= 0x10FFFF);
}

static bool ist_wordchar(utf8proc_int32_t c, bool numb_mode)
{
    static const char spaces[] = " \n\t\r\f\v";

    if (c >= 0 && c <= 0x7F && memchr(spaces, c, sizeof(spaces) - 1) != NULL) {
        return false;
    }
    if (!numb_mode && c >= 0 && c <= 0x10FFFF) {
        return !ist_whitespace(c, numb_mode) && !ist_linefeed(c, numb_mode);
    }
    return true;  // invalid bytes are a mess
}

static bool measure_file(FILE* stream, const char* filepath, const Config* config, Record* record)
{
    // TODO: count grapheme clusters
    size_t bytes = 0, chars = 0, graphs = 0, words = 0, lines = 0, width = 0;

    utf8proc_uint8_t buf[BUFSIZE];
    utf8proc_int32_t codepoint = -1;
    utf8proc_ssize_t n_bytes = -1;
    size_t size = 0;
    size_t index = 0;
    size_t offset = 0;  // distance from line start currently
    size_t length = 0;  // maximum line length before line end in history
    bool at_word = false;
    size_t word_len = 0;

    do {
        size_t n_read = fread(buf + size, 1, BUFSIZE - size, stream);

        if (ferror(stream)) {
            warn("%s", "reading failed");
            logged_return(false);
        }
        size += n_read;

        if (config->count_bytes && !add(&bytes, n_read)) {
            error("integer overflow");
        }

        if (config->count_chars || config->count_graphs ||
                config->count_words || config->count_lines || config->count_width) {
            for (index = 0; index < size; index += n_bytes) {
                bool is_cc = false;
                bool is_valid_ch = true;
                bool is_valid_cp = true;

                if (!config->numb_mode) {
                    n_bytes = utf8proc_iterate(buf + index, size - index, &codepoint);

                    if (n_bytes == 0) {
                        if (size - index != 0) {
                            error("broken utf8proc");
                        }
                        break;
                    } else if (n_bytes < 0 || n_bytes > 4) {
                        if (size - index >= 4) {
                            if (n_bytes > 0 && size - index < n_bytes) {
                                error("broken utf8proc");
                            }
                            if (config->strict_mode) {
                                warn("%s: 0x[%02X]...", "invalid UTF-8 byte sequence from input", buf[index]);
                            }
                            is_valid_cp = false;
                            is_valid_ch = false;
                            n_bytes = 1;
                            codepoint = -1;
                        } else {
                            break;
                        }
                    } else if (codepoint < 0 || codepoint > 0x10FFFF) {
                        if (config->strict_mode) {
                            warn("%s: U+%04X", "invalid UTF-8 byte sequence from input", codepoint);
                        }
                        is_valid_cp = false;
                        is_valid_ch = false;
                        n_bytes = 1;
                        codepoint = -1;
                    } else if (ist_controlchar(codepoint, config->numb_mode)) {
                        if (config->strict_mode) {
                            warn("%s: U+%04X", "unwelcome control character from input", codepoint);
                        }
                        is_cc = true;
                        //is_valid_ch = false;
                    }
                } else {
                    n_bytes = 1;
                    codepoint = buf[index];

                    if (codepoint <= 0x1F && codepoint >= 0) {
                        is_cc = true;
                    }
                    if (codepoint > 0x7F || codepoint < 0) {
                        if (config->strict_mode) {
                            warn("%s: 0x[%02X]", "invalid ASCII byte from input", codepoint);
                        }
                        is_valid_cp = false;
                        //is_valid_ch = false;
                    }
                }

                if (is_valid_ch && !add(&chars, 1)) {
                    error("integer overflow");
                }

                if (config->count_graphs || config->count_words || config->count_width) {
                    size_t cwidth = SIZE_MAX - offset + 1;

                    if (codepoint == '\t') {
                        if (config->tab_width > 1) {
                            cwidth = config->tab_width - offset % config->tab_width;
                        } else {
                            cwidth = config->tab_width;
                        }
                    } else if (!config->numb_mode) {
                        int w = get_charwidth(codepoint, config->numb_mode);

                        if (w < 0) {
                            if (config->strict_mode) {
                                warn("%s: U+%04X", "unhandled negative-width character from input", codepoint);
                            }
                            //is_valid_ch = false;
                        }
                        cwidth = is_valid_ch && w > 0 ? w : 0;
                    } else {
                        cwidth = is_valid_ch && codepoint > 0x1F && codepoint < 0x7F ? 1 : 0;
                    }

                    // printer: how many lines to shift downward for \v ?
                    // printer: how many columns to shift backward for \b ?
                    if (codepoint == '\r' || codepoint == '\f') {
                        offset = 0;
                    }
                    if (!add(&offset, cwidth)) {
                        error("integer overflow");
                    }

                    if (config->count_width) {
                        length = max(length, offset);
                    }

                    if (config->count_graphs || config->count_words) {
                        if (ist_wordchar(codepoint, config->numb_mode)) {
                            if (config->count_graphs && !add(&graphs, 1)) {
                                error("integer overflow");
                            }
                            if (config->count_words) {
                                word_len += cwidth;
                                at_word = true;
                            }
                        } else {
                            if (config->count_words) {
                                if (at_word && word_len > 0 && !add(&words, 1)) {
                                    error("integer overflow");
                                }
                                word_len = 0;
                                at_word = false;
                            }
                        }
                    }
                }

                if (config->count_lines || config->count_width) {
                    if (ist_linefeed(codepoint, config->numb_mode)) {
                        if (config->count_lines && !add(&lines, 1)) {
                            error("integer overflow");
                        }
                        if (config->count_width) {
                            width = max(length, width);
                        }

                        offset = 0;
                        length = 0;
                    }
                }
            }
            size = size - index;
            memmove(buf, buf + index, size);
        } else {
            index = size;
            size = 0;
        }
    } while (!feof(stream));

    if (index < size) {
        if (config->strict_mode) {
            warn("%s: 0x[%02X]...", "invalid UTF-8 byte sequence from input", buf[index]);
        }
    }

    if (length > 0) {
        if (config->count_words && at_word && word_len > 0 && !add(&words, 1)) {
            error("integer overflow");
        }
        if (!config->eof_not_eol && !add(&lines, 1)) {
            error("integer overflow");
        }
        if (config->count_width) {
            width = max(length, width);
        }
    }

    record->filepath = filepath;
    record->bytes = bytes;
    record->chars = chars;
    record->graphs = graphs;
    record->words = words;
    record->lines = lines;
    record->width = width;

    return true;
}

static bool write_records(const Record* records, size_t size, const Config* config)
{
    // TODO: use big integer
    Record total = {
        .filepath = "TOTAL",
        .bytes = 0, .chars = 0, .graphs = 0, .words = 0, .lines = 0, .width = 0,
    };

#define K (sizeof(offsets) / sizeof(*offsets))

    static size_t const offsets[] = {
        0,
        offsetof(Record, lines),
        offsetof(Record, words),
        offsetof(Record, graphs),
        offsetof(Record, chars),
        offsetof(Record, bytes),
        offsetof(Record, width),
        offsetof(Record, filepath),
    };

    const char* headers[K] = {
        config->verbose_mode ? "TOTAL" : NULL,
        config->count_lines ? "LINES" : NULL,
        config->count_words ? "WORDS" : NULL,
        config->count_graphs ? "GRAPHS" : NULL,
        config->count_chars ? "CHARS" : NULL,
        config->count_bytes ? "BYTES" : NULL,
        config->count_width ? "WIDTH" : NULL,
        "INPUT",
    };

    size_t fmt_size[K] = {0};  // total, column...
    int tmp_size[K] = {0};
    char* output = NULL;
    char* p = NULL;
    size_t i, k, n, t;

    if (size <= 0) {
        error("unknown error");
    }

    //\ accumulate data
    for (fmt_size[K - 1] = strlen(headers[K - 1]), i = 0; i < size; ++i) {
        if (false ||
                (config->count_lines && !add(&total.lines, records[i].lines)) ||
                (config->count_words && !add(&total.words, records[i].words)) ||
                (config->count_graphs && !add(&total.graphs, records[i].graphs)) ||
                (config->count_chars && !add(&total.chars, records[i].chars)) ||
                (config->count_bytes && !add(&total.bytes, records[i].bytes))) {
            error("integer overflow");
        }
        if (config->count_width) {
            total.width = max(records[i].width, total.width);
        }
        fmt_size[K - 1] = max(strlen(records[i].filepath), fmt_size[K - 1]);
    }
    if (false ||
            (config->count_lines && (tmp_size[1] = snprintf(NULL, 0, "%zu", total.lines)) < 0) ||
            (config->count_words && (tmp_size[2] = snprintf(NULL, 0, "%zu", total.words)) < 0) ||
            (config->count_graphs && (tmp_size[3] = snprintf(NULL, 0, "%zu", total.graphs)) < 0) ||
            (config->count_chars && (tmp_size[4] = snprintf(NULL, 0, "%zu", total.chars)) < 0) ||
            (config->count_bytes && (tmp_size[5] = snprintf(NULL, 0, "%zu", total.bytes)) < 0) ||
            (config->count_width && (tmp_size[6] = snprintf(NULL, 0, "%zu", total.width)) < 0)) {
        error("formatting failed");
    }
    for (fmt_size[0] = fmt_size[K - 1], k = 1; k <= K - 2; ++k) {
        if (headers[k] != NULL) {
            fmt_size[k] = max(tmp_size[k], strlen(headers[k])) + 2;

            if (!add(&fmt_size[0], fmt_size[k])) {
                error("integer overflow");
            }
        }
    }
    if (!add(&fmt_size[0], 2)) {
        error("integer overflow");
    }
    if ((output = malloc(fmt_size[0])) == NULL) {
        error("failed to allocate memory");
    }

    //\ write headers
    if (config->verbose_mode) {
        memset(output, ' ', fmt_size[0]);

        for (n = fmt_size[0], p = output, k = 1; k <= K - 1; ++k) {
            if (headers[k] != NULL) {
                t = strlen(headers[k]);
                memcpy(p, headers[k], t);
                p += fmt_size[k];
                n -= fmt_size[k];
            }
        }
        while (*p == ' ') *p-- = '\0';

        if (printf("%s\n", output) < 0) {
            warn("%s", "writing failed");
            logged_return(false);
        }
    }

    //\ write records
    for (i = 0; i < size; ++i) {
        memset(output, ' ', fmt_size[0]);

        for (n = fmt_size[0], p = output, k = 1; k <= K - 2; ++k) {
            if (headers[k] != NULL) {
                t = *(size_t*)((char*)&records[i] + offsets[k]);

                if ((tmp_size[0] = snprintf(p, n, "%zu", t)) < 0) {
                    error("formatting failed");
                }
                p[tmp_size[0]] = ' ';
                p += fmt_size[k];
                n -= fmt_size[k];
            }
        }
        if (*records[i].filepath != '\0') {
            strncpy(p, records[i].filepath, n);
        } else {
            while (*p == ' ') *p-- = '\0';
        }

        if (printf("%s\n", output) < 0) {
            warn("%s", "writing failed");
            logged_return(false);
        }
    }

    //\ write summary
    if (config->verbose_mode && size > 1) {
        memset(output, ' ', fmt_size[0]);

        for (n = fmt_size[0], p = output, k = 1; k <= K - 2; ++k) {
            if (headers[k] != NULL) {
                t = *(size_t*)((char*)&total + offsets[k]);

                if ((tmp_size[0] = snprintf(p, n, "%zu", t)) < 0) {
                    error("formatting failed");
                }
                p[tmp_size[0]] = ' ';
                p += fmt_size[k];
                n -= fmt_size[k];
            }
        }
        strncpy(p, total.filepath, n);

        if (printf("%s\n", output) < 0) {
            warn("%s", "writing failed");
            logged_return(false);
        }
    }

    // done
    free(output);

    return true;
}

int main(int argc, char** argv)
{
    const char* filepath = NULL;
    const char* alias = NULL;
    FILE* stream = NULL;
    int i = -1;

    Record* records = NULL;
    size_t n_records = 0;

    Config config;
    config.tab_width = TAB_WIDTH;

    if (!parse_options(&argc, &argv, &config)) {
        log("\n%s", usage);
        exit(1);
    }

    n_records = argc > 0 ? argc : 1;

    if ((records = malloc(sizeof(Record) * n_records)) == NULL) {
        error("failed to allocate memory");
    }

    if (argc > 0) {
        for (i = 0; i < argc; ++i) {
            filepath = argv[i];
            alias = *filepath != '\0' ? filepath : "";  // or "/dev/stdin"?
            stream = *filepath != '\0' ? fopen(filepath, "rb") : stdin;

            if (stream == NULL) {
                warn("failed to open \"%s\"", alias);
                goto FAIL;
            }
            if (!measure_file(stream, alias, &config, &records[i])) {
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
        alias = "";

        if (!measure_file(stream, alias, &config, &records[0])) {
            warn("%s", "failed to process stdin");
            goto FAIL;
        }
    }

    if (!write_records(records, n_records, &config)) {
        warn("%s", "failed to write records");
        goto FAIL;
    }

    if (stream != NULL && ferror(stream)) {
FAIL:
        if (errno != 0) {
            warn("%s", strerror(errno));
        } else {
            warn("%s", "unknown error");
        }
        (void)fclose(stream);  // whatever
        free(records);

        return 1;
    }
    free(records);

    return 0;
}
