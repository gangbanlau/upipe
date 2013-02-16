#!/bin/bash

srcdir="$1"
shift

# Check for shell scripts
if test "${1%.sh}" != "$1"; then
	exec "$1" "$srcdir"
fi

# Run normally
"$@"
RET=$?
if test $RET -ne 0; then
	exit $RET
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	exit 1
fi

# Run in valgrind, with leak checking enabled
FILE="`mktemp tmp.XXXXXXXXXX`"
libtool --mode=execute valgrind -q --leak-check=full --suppressions="$srcdir"/valgrind.supp "$@" > /dev/null 2> "$FILE"
RET=$?
if test -s "$FILE"; then
        cat "$FILE" >&2
        RET=1
fi
rm -f "$FILE"
exit $RET
