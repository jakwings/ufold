#!/bin/sh

cd -- "$(dirname -- "$0")"

ufold=../build/ufold
urandom=../build/urandom
loop=42
seconds=5
seed="${1:-$(date '+%s')}"

printf '[SEED=%s]\n\n' "${seed}"

if [ ! -x "${ufold}" ]; then
    ufold="$(type -p ufold)"
    if [ "$?" -ne 0 ]; then
        printf 'ufold not found\n'
        exit 1
    fi
fi
printf 'Using ufold: %s\n' "${ufold}"

fold() {
    timeout "${seconds}" "${ufold}" "$@"
}

exit_if_failed() {
    local exitcode=$?
    if [ "${exitcode}" -ne 0 ]; then
        printf 'Failed\n'
        wc -c tmp_stdin tmp_stdout
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
    num="$(printf '%03d' "${i}")"
    input="fixtures/${num}.in"
    output="fixtures/${num}.out"

    printf '\r[TEST] ufold %-16s  %s ... ' "${flags}" "${input}"

    printf '%s\n' "${flags}" > tmp_flags
    { cat "${input}"; echo; cat "${input}"; } |
        tee tmp_stdin |
            fold $flags > tmp_stdout 2> tmp_stderr
    exit_if_failed

    { cat "${output}"; echo; cat "${output}"; } > tmp_expect
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
flags_w="$(printf ' -w%s ' $(seq 8 -1 0))"
flags_t="$(printf ' -t%s ' $(seq 8 -1 0))"
flags_bis='-b -s -i -bs -bi -is -bis'
flags=" \
    -V -h '' \
    ${flags_w} \
    ${flags_t} \
    $(for opt in ${flags_w}; do printf " '${opt} %s' " ${flags_t}; done) \
    ${flags_bis} \
    $(for opt in ${flags_bis}; do printf " '${opt} %s' " ${flags_w}; done) \
    $(for opt in ${flags_bis}; do printf " '${opt} %s' " ${flags_t}; done) \
    $(
      for opt_bis in ${flags_bis}; do
          for opt_w in ${flags_w}; do
              printf " '${opt_bis} ${opt_w} %s' " ${flags_t}
          done
      done
    ) \
"
read_total=0
read_delta=10086
eval "set -- ${flags}"
for args; do
    printf '%s\n' "${args}" > tmp_flags

    i=1
    while [ "${i}" -le "${loop}" ]; do
        printf '\r[TEST] ufold %-16s  # Round %d ... ' "${args}" "${i}"

        "${urandom}" "${seed}" "${read_total}" | head -c"${read_delta}" |
            tee tmp_stdin |
                fold $args > tmp_stdout 2> tmp_stderr
        exit_if_failed

        read_total=$(( read_total + read_delta ))
        i=$(( i + 1 ))
    done
    printf 'Done\n'
done

rm -f tmp_*
