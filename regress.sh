#! /bin/sh

echo Running regression tests...
./driver -m`pwd`/regress 31789 -u-1 -p-1 1>`pwd`/regress/regress.stdout 2>`pwd`/regress/regress.stderr

diff `pwd`/regress/expected.stdout `pwd`/regress/regress.stdout &&
    diff `pwd`/regress/expected.stderr `pwd`/regress/regress.stderr

if [ $? != 0 ]; then
    echo
    echo WARNING: Regression test failed.
    exit 1
fi

echo
echo INFO: All regression tests passed.
rm -f `pwd`/regress/regress.stdout
rm -f `pwd`/regress/regress.stderr
exit 0
