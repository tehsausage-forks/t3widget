fail() {
	echo "$@" >&2
	exit 1
}

cd_workdir() {
	cd "$DIR" || fail "Could not change to base dir"
	{ [ -d work ] || mkdir work ; } || fail "Could not create work dir"
	cd work || fail "Could not change to work dir"
}

setup_TEST() {
	if [ "${1#/}" = "$1" ] && [ "${1#~/}" = "$1" ] ; then
		TEST="$PWD/$1"
	elif [ "${1#~/}" != "$1" ] ; then
		TEST="$HOME${1#~}"
	else
		TEST="$1"
	fi
}

setup_vars() {
  if [[ "${DIR#.}" !=  "$DIR" ]] ; then
    TERMINFO_DIR="$PWD/"
  elif [[ "${DIR#~}" != "$DIR" ]] ; then
    TERMINFO_DIR="$HOME${DIR#~}"
  fi
  export TERMINFO="$TERMINFO_DIR/terminfo"
}

build_test() {
	LINE=`grep -n '@CLASS@' ../test.cc | head -n1  | sed -r 's/^([0-9]+).*/\1+1/' | bc`
	{
		echo '#line 1 "../test.cc"'
		sed "/@CLASS@/{s%@CLASS@%#line 1 \"$TEST/test.cc\"%;r $TEST/test.cc
a #line $LINE \"../test.cc\"
}" ../test.cc
	} > test.cc

	g++ -g -Wall -I../../src -I../../../t3shared/include test.cc -L../../src/.libs/ \
		-lt3widget -L../../../t3window/src/.libs -lt3window -o test \
		-Wl,-rpath=$PWD/../../src/.libs:$PWD/../../../t3window/src/.libs:$PWD/../../../t3key/src/.libs:$PWD/../../../t3config/src/.libs:$PWD/../../../transcript/src/.libs || fail "!! Could not compile test"
}

fixup_test() {
	sed -i -r '/^send.*033\[2;.R/s/send +[0-9]+/send 0/' "$1"
}
