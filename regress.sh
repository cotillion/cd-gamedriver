#! /bin/sh

echo Note that there is supposed to be errors in the following
echo output. There should, however, be a statement at the end
echo stating all regression tests has passed.
echo
echo Running regression tests...
(./driver -m`pwd`/regress 31789 -u-1 -p-1 2>&1) | tee `pwd`/regress/regress.output

diff `pwd`/regress/expected.output `pwd`/regress/regress.output

if [ $? != 0 ]; then
    echo
    echo WARNING: Regression test failed.
    exit 1
fi

echo
echo INFO: All regression tests passed.
rm -f `pwd`/regress/regress.output
exit 0
