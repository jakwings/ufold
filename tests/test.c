#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdbool.h"
#include "ufold.h"

#define warn(fmt, ...) fprintf(stderr, "[ERROR]: " fmt "\n", __VA_ARGS__)

static uint8_t* buf = NULL;
static size_t buf_size = 0;
static size_t text_len = 0;

static void free_buf()
{
    free(buf);
    buf = NULL;
    buf_size = 0;
    text_len = 0;
}

static void clear_buf()
{
    text_len = 0;
}

static bool check_buf(const void* s, size_t n)
{
    return memcmp(buf, s, n) == 0;
}

static bool write_to_buf(const void* s, size_t n)
{
    if (buf_size < text_len + n) {
        buf_size = buf_size + ((n > buf_size) ? n : buf_size);
        if (NULL == (buf = realloc(buf, buf_size))) {
            warn("%s", "failed to allocate memory for buffer");
            return false;
        }
    }
    if (n > 0) {
        memmove(buf + text_len, s, n);
        text_len += n;
        assert(text_len <= buf_size);
    }
    return true;
}

#define run_test(name) \
    (test_ ## name)()

#define TEST_START(name) \
    static void (test_ ## name)() { \
        errno = 0; \
        printf("[API TEST] %s ... ", #name); \
        fflush(stdout); \
        ufold_vm_t* vm = NULL; \
        ufold_vm_config_t config = { \
            .max_width = MAX_WIDTH, \
            .tab_width = TAB_WIDTH, \
            .keep_indentation = false, \
            .break_at_spaces = false, \
            .truncate_bytes = false, \
            .write = write_to_buf, \
            .realloc = NULL, \
        }; \
        clear_buf();

#define TEST_END(name) \
        free_buf(); \
        ufold_vm_free(vm); \
        printf("Done\n"); \
        fflush(stdout); \
        return; \
    TEST_FAIL: \
        if (errno != 0) perror("[ERROR]"); \
        ufold_vm_free(vm); \
        printf("\n[API TEST] %s ... Failed\n", #name); \
        exit(EXIT_FAILURE); \
    }

#define expect(bytes, size) \
    do { \
        if (!check_buf((bytes), (size))) { \
            fprintf(stderr, "[ERROR]: unexpected output\n"); \
            fprintf(stderr, "[EXPECTED]\n"); \
            fwrite((bytes), (size), 1, stderr); \
            fprintf(stderr, "\n[/EXPECTED]\n"); \
            fprintf(stderr, "[ACTUAL]\n"); \
            fwrite(buf, text_len, 1, stderr); \
            fprintf(stderr, "\n[/ACTUAL]\n"); \
            goto TEST_FAIL; \
        } \
    } while (false)

#define vnew(vm, config) \
    if (NULL == ((vm) = ufold_vm_new(config))) goto TEST_FAIL

#define vfeed(vm, bytes, size) \
    if (!ufold_vm_feed((vm), (bytes), (size))) goto TEST_FAIL

#define vflush(vm) \
    if (!ufold_vm_flush(vm)) goto TEST_FAIL

#define vstop(vm) \
    if (!ufold_vm_stop(vm)) goto TEST_FAIL


TEST_START (indent_01)
    config.max_width = 9;
    config.keep_indentation = true;

    vnew(vm, config);
    vfeed(vm, "    ", 4); vflush(vm);
    vfeed(vm, "    ", 4);
    vfeed(vm, "AAAA", 4);
    vstop(vm);

    char result[] =
    "        A\n"
    "        A\n"
    "        A\n"
    "        A";
    expect(result, sizeof(result) - 1);
TEST_END (indent_01)


TEST_START (indent_02)
    config.max_width = 9;
    config.keep_indentation = true;

    vnew(vm, config);
    vfeed(vm, "         A", 10);
    vstop(vm);

    char result[] = "         A";
    expect(result, sizeof(result) - 1);
TEST_END (indent_02)


int main()
{
    run_test(indent_01);
    run_test(indent_02);

    return EXIT_SUCCESS;
}
