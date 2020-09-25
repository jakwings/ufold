#!/bin/sh

set -e

cd -- "$(dirname -- "$0")"

echo() {
  printf '%s\n' "$*"
}

utest=../build/test
ufold=../build/ufold
ofold=../build/ofold
urandom=../build/urandom
uwc=../build/uwc
ucwidth=../build/ucwidth

loop=42
seconds=5
seed="${1:-$(date '+%s')}"

printf '[SEED=%s]\n\n' "${seed}"

if [ ! -x "${ufold}" ]; then
    if ! ufold="$(command -v ufold)"; then
        printf 'ufold not found\n'
        exit 1
    fi
fi
printf 'Using ufold: %s\n' "${ufold}"

if [ -x "${ofold}" ] && ! cmp -s "${x}" "${ufold}"; then
    printf 'Using ofold: %s\n' "${ofold}"
else
    ofold=
fi

ufold() {
    timeout "${seconds}" "${ufold}" "$@"
}
ofold() {
    timeout "${seconds}" "${ofold}" "$@"
}

fail() {
    exitcode=$?
    printf 'Failed\n'
    "${uwc}" -Gv tmp_stdin tmp_stdout
    cat tmp_stderr
    exit "${exitcode}"
}
check() {
    if [ -f tmp_expect ]; then
        if ! diff -u tmp_expect tmp_stdout > tmp_diff 2>&1; then
            printf 'Failed\n'
            "${uwc}" -bcwlnv tmp_stdin tmp_expect tmp_stdout
            cat tmp_diff
            printf '#\n'
            exit 1
        fi
    fi
}

if [ -r tmp_flags ]; then
    flags="$(cat tmp_flags)"
    printf 'Retry the previous failed test: ufold %s ... ' "${flags}"
    ufold $flags < tmp_stdin > tmp_stdout 2> tmp_stderr || fail
    check
    rm -f tmp_expect
    "${ucwidth}" $flags < tmp_stdout > /dev/null 2> tmp_stderr || fail
    check
    printf 'Done\n'
fi

if [ -e "${utest}" ]; then
    rm -f tmp_*
    timeout "${seconds}" "${utest}" || exit 1
fi

# test regular inputs
rm -f tmp_*
i=1
while read -r flags; do
    num="$(printf '%03d' "${i}")"
    input="fixtures/${num}.in"
    output="fixtures/${num}.out"

    printf '\r[TEST] ufold %-16s  %s ... ' "${flags}" "${input}"

    printf '%s\n' "${flags}" > tmp_flags
    { cat "${output}"; echo; cat "${output}"; } > tmp_expect
    { cat "${input}"; echo; cat "${input}"; } |
        tee tmp_stdin |
            ufold $flags > tmp_stdout 2> tmp_stderr || fail
    check

    printf 'Done\n'

    i=$(( i + 1 ))
done < flags.txt

# test exit status
flags_w="$(printf ' -w%s ' 80 8 3 1)"
flags_t="$(printf ' -t%s ' 8 3 1 0)"
flags_bis="-b -s -i -bs -bi -is -bis -ip -bip -isp -bisp"
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
rm -f tmp_*
read_total=0
read_delta=10086
eval "set -- ${flags}"
for args; do
    printf '%s\n' "${args}" > tmp_flags

    i=1
    while [ "${i}" -le "${loop}" ]; do
        printf '\r[TEST] ufold %-16s  # Round %-2s (%s+%s)... ' \
               "${args}" "${i}" "${read_total}" "${read_delta}"

        "${urandom}" "${seed}" "${read_total}" |
            head -c"${read_delta}" > tmp_stdin

        if [ -n "${ofold}" ] && case "${args}" in (*[hV]*) false; esac; then
            {
                ofold $args < tmp_stdin 2> tmp_stderr |
                    tee tmp_expect > tmp_stdout
            } || fail
        fi

        ufold $args < tmp_stdin > tmp_stdout 2> tmp_stderr || fail
        check

        rm -f tmp_expect
        "${ucwidth}" $args < tmp_stdout > /dev/null 2> tmp_stderr || fail
        check

        read_total=$(( read_total + read_delta ))
        i=$(( i + 1 ))
    done
    printf 'Done\n'
done

rm -f tmp_*
