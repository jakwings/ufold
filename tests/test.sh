#!/usr/bin/env bash

cd "$(dirname -- "$0")"

ufold=../build/ufold
loop=100
seconds=5

if [[ ! -x "${ufold}" ]]; then
    ufold=$(type -p ufold)
    if [[ "$?" -ne 0 ]]; then
        printf 'ufold not found\n'
        exit 1
    fi
fi
printf 'Using ufold: %s\n' "${ufold}"

function fold {
    timeout "${seconds}" "${ufold}" "$@"
}

if [[ -r tmp_flags ]]; then
    printf 'Retry the previous failed test: ufold %s ... ' "$(cat tmp_flags)"
    fold $(cat tmp_flags) < tmp_stdin > tmp_stdout 2> tmp_stderr

    exitcode=$?
    if [[ "${exitcode}" -ne 0 ]]; then
        printf 'Failed\n'
        wc -c tmp_std{in,out}
        cat tmp_stderr
        exit "${exitcode}"
    fi
    printf 'Done\n'
fi

# test exit status
flags=(
    -V -h ''
    -w{8..0}
    -t{8..0}
    -w{8..0}\ -t{8..0}
    -{b,s,i,bs,bi,is,bis}
    -{b,s,i,bs,bi,is,bis}\ -w{8..0}
    -{b,s,i,bs,bi,is,bis}\ -t{8..0}
    -{b,s,i,bs,bi,is,bis}\ -w{8..0}\ -t{8..0}
)
i=0
while [[ $i -lt ${#flags[@]} ]]; do
    args=${flags[$i]}
    printf '%s\n' "${args}" > tmp_flags

    j=1
    while [[ $j -le "${loop}" ]]; do
        printf '\r[TEST] ufold %-16s  # Round %d ... ' "${args}" $j

        head -c10000 /dev/random |
            tee tmp_stdin |
                fold $args > tmp_stdout 2> tmp_stderr

        exitcode=$?
        if [[ "${exitcode}" -ne 0 ]]; then
            printf 'Failed\n'
            wc -c tmp_std{in,out}
            cat tmp_stderr
            exit "${exitcode}"
        fi

        j=$(( j + 1 ))
    done
    printf 'Done\n'

    i=$(( i + 1 ))
done
rm tmp_flags tmp_std{in,out,err}
