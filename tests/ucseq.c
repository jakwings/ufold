#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if CHAR_BIT != 8
#error "char must be exactly 8 bits wide"
#endif

#if SIZE_MAX < 0x10FFFF
#error "size_t is not wide enough for all Unicode scalar values"
#endif

typedef char byte;

//\ High/Low Surrogates are invalid for UTF-8 but allowed here for convenience.
size_t utf8encode(size_t codepoint, byte* sequence) {
    if (codepoint < 0x80) {
        sequence[0] = codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        sequence[0] = 0xC0 + (codepoint >> 6);
        sequence[1] = 0x80 + (codepoint & 0x3F);
        return 2;
    } else if (codepoint < 0x10000) {
        // U+D800-DBFF and U+DC00-DFFF are allowed here for convenience.
        sequence[0] = 0xE0 + (codepoint >> 12);
        sequence[1] = 0x80 + ((codepoint >> 6) & 0x3F);
        sequence[2] = 0x80 + (codepoint & 0x3F);
        return 3;
    } else if (codepoint < 0x110000) {
        sequence[0] = 0xF0 + (codepoint >> 18);
        sequence[1] = 0x80 + ((codepoint >> 12) & 0x3F);
        sequence[2] = 0x80 + ((codepoint >> 6) & 0x3F);
        sequence[3] = 0x80 + (codepoint & 0x3F);
        return 4;
    } else {
        return 0;
    }
}

#define expect(ok) \
do { \
    if (!(ok)) { \
        if (errno != 0) { \
            fflush(stdout); \
            fprintf(stderr, "[ucs:%d] %s\n", __LINE__, strerror(errno)); \
        } \
        assert(!#ok); \
    } \
} while (false)

void redirect(char** argv, const byte* sequence, size_t size)
{
    int ret;

    int fds[2];
    // the pipe persists until all associated file descriptors are closed
    ret = pipe(fds);
    expect(ret != -1);

    int cid = fork();
    expect(cid != -1);

    if (cid == 0)  // child process
    {
        // (release)
        ret = close(fds[1]);
        expect(ret != -1);

        // fd_in ==> stdin (detain)
        ret = dup2(fds[0], fileno(stdin));
        expect(ret != -1);
        // (release)
        ret = close(fds[0]);
        expect(ret != -1);

        ret = execvp(argv[0], argv);
        expect(ret != -1);
        _exit(-1);
    }
    else  // parent process
    {
        // (release)
        ret = close(fds[0]);
        expect(ret != -1);

        // fd_out --> fd_in
        ret = write(fds[1], sequence, size);
        expect(ret != -1);
        // fd_out <-- EOF (release)
        ret = close(fds[1]);
        expect(ret != -1);

        int wstatus = 0, woption = 0;
        ret = waitpid(cid, &wstatus, woption);
        expect(ret != -1);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fputs("Usage: ucs <min> <max> [<prog> [arg]...]\n", stderr);
        return EXIT_FAILURE;
    }
    long min = strtol(argv[1], NULL, 0);
    long max = strtol(argv[2], NULL, 0);
    char** argve = argc > 3 ? argv + 3 : NULL;
    const byte dummy[] = "\357\277\275";  // U+FFFD "�"

    for (long codepoint = min; codepoint <= max; ++codepoint) {
        byte sequence[5];

        size_t size = utf8encode(codepoint, sequence);

        if (size >= 1 && size <= 4) {
            sequence[size] = '\0';

            if (argve != NULL) {
                redirect(argve, sequence, size);
            } else {
                if (codepoint != '\n') {
                    printf("%s\tU+%04lX\n", sequence, codepoint);
                } else {
                    printf("%s\tU+%04lX\n", "", codepoint);
                }
            }
        } else {
            if (argve != NULL) {
                redirect(argve, "", 0);
            } else {
                printf("%s\tU+%04lX\n", dummy, codepoint);
            }
        }
    }

    return EXIT_SUCCESS;
}
