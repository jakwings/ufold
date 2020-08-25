MAX_WIDTH := 78
TAB_WIDTH := 8

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
OBJECTS += build/ufold build/ufold.a build/ufold.h
OBJECTS += utf8proc/libutf8proc.a pcg-c/src/libpcg_random.a
OBJECTS += build/test build/urandom

unexport CFLAGS
override CFLAGS := -O2 ${CFLAGS} -std=c99 -fPIC -Wall -pedantic \
                   -DMAX_WIDTH=${MAX_WIDTH} -DTAB_WIDTH=${TAB_WIDTH}

ifndef DEBUG
    override CFLAGS += -DNDEBUG
else
    override CFLAGS += -O0 -g
endif

ifdef CHECK_LEAK
    override CFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

OS := $(shell uname)

ifeq ($(OS),Darwin)
    MAKELIB := libtool -static -o
else
    override CFLAGS := ${CFLAGS} -D_GNU_SOURCE -D_XOPEN_SOURCE=700
    MAKELIB := ./makelib.sh
endif

all: ufold build/ufold.a build/ufold.h

ufold: build/ufold

${OBJECTS}: | build/

build/:
	@mkdir -p $@

build/ufold: src/main.c src/optparse.c build/ufold.a
	${CC} ${CFLAGS} -o $@ $^

build/ufold.h: src/vm.h
	cp src/vm.h build/ufold.h

build/ufold.a: build/vm.o build/utf8.o build/utils.o utf8proc/libutf8proc.a
	${MAKELIB} $@ $^

build/vm.o: src/vm.c src/vm.h src/utf8.h src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/utf8.o: src/utf8.c src/utf8.h src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/utils.o: src/utils.c src/utils.h src/utf8.h
	${CC} ${CFLAGS} -c -o $@ $<

build/test: tests/test.c build/ufold.a build/ufold.h
	${CC} ${CFLAGS} -UNDEBUG -Ibuild/ -o $@ tests/test.c build/ufold.a

build/urandom: tests/urandom.c pcg-c/src/libpcg_random.a
	${CC} -std=c99 -Wall -pedantic -O3 -Ipcg-c/include -o $@ $^

utf8proc/libutf8proc.a:
	${MAKE} -C utf8proc UTF8PROC_DEFINES=-DUTF8PROC_STATIC

pcg-c/src/libpcg_random.a:
	${MAKE} -C pcg-c

test: build/test build/urandom
	timeout 5 ./build/test
	./tests/test.sh ${SEED}

clean:
	${MAKE} -C utf8proc clean
	${MAKE} -C pcg-c clean
	rm -rf build/ tests/tmp_*

.PHONY: all clean test ufold
