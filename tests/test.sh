#!/usr/bin/env bash

cd -- "$(dirname -- "$0")"

ufold=../build/ufold
urandom=../build/urandom
loop=42
seconds=5
seed=${1:-$(date +%s)}

printf '[SEED=%s]\n\n' "${seed}"

if [ ! -x "${ufold}" ]; then
    ufold=$(type -p ufold)
    if [ "$?" -ne 0 ]; then
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
    if [ "${exitcode}" -ne 0 ]; then
        printf 'Failed\n'
        wc -c tmp_std{in,out}
        cat tmp_stderr
        exit "${exitcode}"
    fi
}

if [ -r tmp_flags ]; then
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
    cat "${input}" <(echo) "${input}" |
        tee tmp_stdin |
            fold $flags > tmp_stdout 2> tmp_stderr
    exit_if_failed

    cat "${output}" <(echo) "${output}" > tmp_expect
    diff -u tmp_expect tmp_stdout > tmp_diff 2>&1
    if [ "$?" -ne 0 ]; then
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
read_total=0
read_delta=10086
i=0
while [ $i -lt "${#flags[@]}" ]; do
    args=${flags[$i]}
    printf '%s\n' "${args}" > tmp_flags

    j=1
    while [ $j -le "${loop}" ]; do
        printf '\r[TEST] ufold %-16s  # Round %d ... ' "${args}" $j

        "${urandom}" "${seed}" "${read_total}" | head -c"${read_delta}" |
            tee tmp_stdin |
                fold $args > tmp_stdout 2> tmp_stderr
        exit_if_failed

        read_total=$(( read_total + read_delta ))
        j=$(( j + 1 ))
    done
    printf 'Done\n'

    i=$(( i + 1 ))
done

rm -f tmp_*
