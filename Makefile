MAX_WIDTH := 78
TAB_WIDTH := 8

OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))
OBJECTS += build/ufold build/ufold.a build/ufold.h
OBJECTS += utf8proc/libutf8proc.a

unexport CFLAGS
override CFLAGS := ${CFLAGS} -std=c99 -fPIC -Wall -pedantic \
                   -DMAX_WIDTH=${MAX_WIDTH} -DTAB_WIDTH=${TAB_WIDTH}

ifndef DEBUG
    override CFLAGS += -O2 -DNDEBUG
else
    override CFLAGS += -O0 -g
endif

all: ufold build/ufold.a build/ufold.h

ufold: build/ufold

${OBJECTS}: | build/

build/:
	@mkdir -p $@

build/ufold: src/main.c build/ufold.a
	${CC} ${CFLAGS} -o $@ $^

build/ufold.a: build/vm.o build/utf8.o build/utils.o utf8proc/libutf8proc.a
	libtool -static -o $@ - $^

build/ufold.h: src/vm.h
	cp src/vm.h build/ufold.h

build/vm.o: src/vm.c src/vm.h src/utf8.h src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/utf8.o: src/utf8.c src/utf8.h src/utils.h
	${CC} ${CFLAGS} -c -o $@ $<

build/utils.o: src/utils.c src/utils.h src/utf8.h
	${CC} ${CFLAGS} -c -o $@ $<

utf8proc/libutf8proc.a:
	${MAKE} -C utf8proc

test: build/ufold
	./tests/test.sh

clean:
	${MAKE} -C utf8proc clean
	rm -rf build/ tests/tmp_*

.PHONY: all clean test ufold
