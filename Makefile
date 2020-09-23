VERSION := '"1.0.0-upsilon (Unicode 13.0.0)"'
MAX_WIDTH := 78
TAB_WIDTH := 8

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
OBJECTS += build/ufold build/ufold.a build/ufold.h
OBJECTS += utf8proc/libutf8proc.a pcg-c/src/libpcg_random.a
OBJECTS += build/test build/urandom build/uwc build/ucseq build/ucwidth

unexport CFLAGS
override CFLAGS := -O2 ${CFLAGS} -std=c99 -fPIC -Wall -pedantic \
                   -DMAX_WIDTH=${MAX_WIDTH} -DTAB_WIDTH=${TAB_WIDTH} \
                   -DVERSION=${VERSION}

UTIL_FLAGS := -std=c99 -Wall -pedantic -O3 \
              -DMAX_WIDTH=${MAX_WIDTH} -DTAB_WIDTH=${TAB_WIDTH} \
              -DVERSION=${VERSION}

ifdef DEBUG
    override CFLAGS += -UNDEBUG -DUFOLD_DEBUG -O0 -g
    override UTIL_FLAGS += -UNDEBUG -DUFOLD_DEBUG -O0 -g
else
    override CFLAGS += -DNDEBUG -UUFOLD_DEBUG
    override UTIL_FLAGS += -DNDEBUG -UUFOLD_DEBUG
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
uwc: build/uwc
ucseq: build/ucseq
ucwidth: build/ucwidth
urandom: build/urandom

${OBJECTS}: | build/

build/:
	@mkdir -p $@

build/ufold: src/main.c src/optparse.c build/ufold.a
	${CC} ${CFLAGS} -o $@ $^

build/ufold.h: src/vm.h
	cp src/vm.h build/ufold.h

build/ufold.a: build/vm.o build/utils.o utf8proc/libutf8proc.a
	${MAKELIB} $@ $^

build/vm.o: src/vm.c src/vm.h src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/utils.o: src/utils.c src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/test: tests/test.c build/ufold.a
	${CC} ${CFLAGS} -Ibuild/ -o $@ $^

build/urandom: tests/urandom.c pcg-c/src/libpcg_random.a
	${CC} ${UTIL_FLAGS} -Ipcg-c/include -o $@ $^

build/uwc: tests/uwc.c src/optparse.c build/ufold.a
	${CC} ${UTIL_FLAGS} -o $@ $^

build/ucseq: tests/ucseq.c
	${CC} ${UTIL_FLAGS} -o $@ $^

build/ucwidth: tests/ucwidth.c src/optparse.c build/ufold.a
	${CC} ${UTIL_FLAGS} -o $@ $^

utf8proc/libutf8proc.a:
	${MAKE} -C utf8proc UTF8PROC_DEFINES=-DUTF8PROC_STATIC

pcg-c/src/libpcg_random.a:
	${MAKE} -C pcg-c

test: build/test urandom uwc ucseq ucwidth
	./tests/test.sh ${SEED}

clean:
	${MAKE} -C utf8proc clean
	${MAKE} -C pcg-c clean
	rm -rf build/ tests/tmp_*

.PHONY: all clean test ufold urandom uwc ucseq ucwidth
