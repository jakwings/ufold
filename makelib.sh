#!/bin/bash

set -e

TEMP_DIR1=
function clean_up {
  if [ -d "${TEMP_DIR1}" ]; then
    rm -R -- "${TEMP_DIR1}"
  fi
}
trap clean_up EXIT

TEMP_DIR1=$(mktemp -d)
printf 'TEMP_DIR1=%q\n' "${TEMP_DIR1}"

TARGET=$1
shift

i=1
for lib in "$@"; do
    dir="${TEMP_DIR1}/unpacked-${i}"
    mkdir -- "${dir}"

    cp -- "${lib}" "${dir}"

    if [[ "${lib}" =~ \.a$ ]]; then
        pushd "${dir}"
        ar x -- *.a
        prefix=$(basename -- "${lib}")
        for obj in *.o; do
            mv -- "${obj}" "${prefix}.${obj}"
        done
        popd
    fi

    i=$(( i + 1 ))
done

ar rs -- "${TARGET}" "${TEMP_DIR1}/unpacked-"*/*.o
