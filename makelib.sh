#!/bin/bash

set -e

TEMP_DIR=$(mktemp -d)
printf 'TEMP_DIR=%q\n' "${TEMP_DIR}"

TARGET=$1
shift

i=1
for lib in "$@"; do
    dir="${TEMP_DIR}/unpacked-${i}"
    mkdir "${dir}"

    cp "${lib}" "${dir}"

    if [[ "${lib}" =~ \.a$ ]]; then
        pushd "${dir}"
        ar x *.a
        prefix=$(basename -- "${lib}")
        for obj in *.o; do
            mv "${obj}" "${prefix}.${obj}"
        done
        popd
    fi

    i=$(( i + 1 ))
done

ar rs "${TARGET}" "${TEMP_DIR}/unpacked-"*/*.o

rm -R "${TEMP_DIR}"
