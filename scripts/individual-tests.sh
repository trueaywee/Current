#!/bin/bash
#
# Consecutively runs `make test` from within each directory that contains a `test.cc` file.

set -u -e

RETVAL=0

for i in $(find . -name test.cc | sort -g) ; do
  DIR=$(dirname $i)
  echo -e -n "\n\033[0m\033[1mDir\033[0m: \033[36m"
  echo $DIR
  (cd $DIR && make -s test) || RETVAL=1
done

if [ $RETVAL -eq 0 ] ; then
 echo -e "\n\033[32m\033[1mALL TESTS PASS.\033[0m"
else
 echo -e "\n\033[31m\033[1mFAIL\033[0m"
fi

exit $RETVAL
