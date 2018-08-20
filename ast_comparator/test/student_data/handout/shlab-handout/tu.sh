#! /bin/bash

NUM=0
FILENAME=$NUM"_tsh.c"

i=39
while [ $i -ge 0 ]
do

	NUM=$i
	FILENAME="prev_"$NUM"_tsh.c"
	cp cases/$FILENAME .
	make "prev_"$NUM"_tsh"
	mv $FILENAME".001t.tu" ../../../tu/shelllab/prev_students
	rm $FILENAME *tsh
	i=$[$i-1]
done

