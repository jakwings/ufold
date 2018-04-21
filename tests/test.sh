#!/usr/bin/env bash

ufold=../build/ufold
loop=100
seconds=5

function fold {
    head -c10000 /dev/random |
        tee tmp_stdin |
            timeout "${seconds}" "${ufold}" "$@"
}

cd "$(dirname -- "$0")"

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

    j=1
    while [[ $j -le "${loop}" ]]; do
        printf '\r[TEST] ufold %-16s  # Round %d ... ' "${args}" $j
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
rm tmp_std{in,out,err}
