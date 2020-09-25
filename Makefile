VERSION := '"1.0.0-phi (Unicode 13.0.0)"'
MAX_WIDTH := 78
TAB_WIDTH := 8

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
OBJECTS += build/ufold build/ufold.a build/ufold.h
OBJECTS += utf8proc/libutf8proc.a pcg-c/src/libpcg_random.a
OBJECTS += build/test build/urandom build/uwc build/ucseq build/ucwidth

unexport CFLAGS
override CFLAGS := -O3 ${CFLAGS} -std=c99 -fPIC -Wall -pedantic \
                   -DMAX_WIDTH=${MAX_WIDTH} -DTAB_WIDTH=${TAB_WIDTH} \
                   -DVERSION=${VERSION}

ifdef DEBUG
    override CFLAGS += -UNDEBUG -DUFOLD_DEBUG -O0 -g
else
    override CFLAGS += -DNDEBUG -UUFOLD_DEBUG
endif

ifdef CHECK_LEAK
    override CFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

OS := $(shell uname)

ifeq (${OS},Darwin)
    MAKELIB := libtool -static -o
else
    override CFLAGS += -D_GNU_SOURCE -D_XOPEN_SOURCE=700
    MAKELIB := ./makelib.sh
endif

all: ufold build/ufold.a build/ufold.h

utils: urandom uwc ucseq ucwidth

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
	${CC} ${CFLAGS} -Ipcg-c/include -o $@ $^

build/uwc: tests/uwc.c src/optparse.c build/ufold.a
	${CC} ${CFLAGS} -o $@ $^

build/ucseq: tests/ucseq.c
	${CC} ${CFLAGS} -o $@ $^

build/ucwidth: tests/ucwidth.c src/optparse.c build/ufold.a
	${CC} ${CFLAGS} -o $@ $^

utf8proc/libutf8proc.a:
	${MAKE} -C utf8proc UTF8PROC_DEFINES=-DUTF8PROC_STATIC

pcg-c/src/libpcg_random.a:
	${MAKE} -C pcg-c > /dev/null

test: build/test utils
	./tests/test.sh ${SEED}

clean:
	${MAKE} -C utf8proc clean
	${MAKE} -C pcg-c clean
	rm -rf build/ tests/tmp_*

.PHONY: all utils clean test ufold urandom uwc ucseq ucwidth
