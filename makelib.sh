#!/bin/sh

set -e

TEMP_DIR1=
clean_up() {
    if [ -d "${TEMP_DIR1}" ]; then
        rm -R -- "${TEMP_DIR1}"
    fi
}
trap clean_up EXIT

TEMP_DIR1="$(mktemp -d)"
printf 'TEMP_DIR1="%s"\n' "${TEMP_DIR1}"

TARGET="$1"
shift

i=1
for lib; do
    dir="${TEMP_DIR1}/unpacked-${i}"
    mkdir -- "${dir}"

    cp -- "${lib}" "${dir}"

    if [ "${lib}" != "${lib%.a}" ]; then
        (
            set -e
            cd -- "${dir}"
            ar x -- *.a
            prefix="$(basename -- "${lib}")"
            for obj in *.o; do
                mv -- "${obj}" "${prefix}.${obj}"
            done
        )
    fi

    i=$(( i + 1 ))
done

ar rs -- "${TARGET}" "${TEMP_DIR1}/unpacked-"*/*.o
