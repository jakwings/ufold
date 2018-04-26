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

function exit_if_failed {
    local exitcode=$?
    if [[ "${exitcode}" -ne 0 ]]; then
        printf 'Failed\n'
        wc -c tmp_std{in,out}
        cat tmp_stderr
        exit "${exitcode}"
    fi
}

if [[ -r tmp_flags ]]; then
    printf 'Retry the previous failed test: ufold %s ... ' "$(cat tmp_flags)"
    fold $(cat tmp_flags) < tmp_stdin > tmp_stdout 2> tmp_stderr
    exit_if_failed
    printf 'Done\n'
fi

# test regular inputs
i=1
while read -r flags; do
    num=$(printf %03d $i)
    input="fixtures/${num}.in"
    output="fixtures/${num}.out"

    printf '\r[TEST] ufold %-16s  %s ... ' "${flags}" "${input}"

    printf '%s\n' "${flags}" > tmp_flags
    # please make sure each input end with a linefeed
    cat "${input}" "${input}" |
        tee tmp_stdin |
            fold $flags > tmp_stdout 2> tmp_stderr
    exit_if_failed

    cat "${output}" "${output}" > tmp_expect
    diff -u tmp_expect tmp_stdout > tmp_diff 2>&1
    if [[ "$?" -ne 0 ]]; then
        printf 'Failed\n'
        cat tmp_diff
        exit 2
    fi

    printf 'Done\n'

    i=$(( i + 1 ))
done < flags.txt

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
        exit_if_failed

        j=$(( j + 1 ))
    done
    printf 'Done\n'

    i=$(( i + 1 ))
done

rm -f tmp_*
