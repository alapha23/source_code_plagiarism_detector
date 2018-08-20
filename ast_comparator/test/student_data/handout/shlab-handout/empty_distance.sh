#! /bin/bash
# Empty Distance of all trees

DIR=../../../../../
SCOPE=builtin_cmd
FILENAME=0_tsh.${SCOPE}.dot


i=63
# cat $DIR/brk_tree/"${i}_tsh.c.eval.tree"
while [ $i -ge 0 ] 
do
	FILENAME="${i}_tsh.c.${SCOPE}.tree"
	java -jar $DIR/apted.jar -f $DIR/brk_tree/$FILENAME $DIR/brk_tree/empty.tree >> $DIR/score/"${SCOPE}_tsh.txt"
	echo "Empty Distance: $FILENAME" >> $DIR/score/"${SCOPE}_tsh.txt"
	echo "Empty Distance: $FILENAME"
	i=$[$i-1]
done 

i=22
while [ $i -ge 0 ] 
do
	FILENAME="prev_${i}_tsh.c.${SCOPE}.tree"
	java -jar $DIR/apted.jar -f $DIR/brk_tree/$FILENAME $DIR/brk_tree/empty.tree >> $DIR/score/${SCOPE}_tsh.txt
	echo "Empty Distance: $FILENAME" >> $DIR/score/${SCOPE}_tsh.txt
	echo "Empty Distance: $FILENAME"
	i=$[$i-1]
done 


i=39
while [ $i -ge 0 ] 
do
	if [ $i -eq 8 ]; then
		i=$[$i-1]
		continue
	fi
	if [ $i -eq 9 ]; then
		i=$[$i-1]
		continue
	fi
	if [ $i -eq 28 ]; then
		i=$[$i-1]
		continue
	fi
	if [ $i -eq 29 ]; then
		i=$[$i-1]
		continue
	fi
	if [ $i -eq 35 ]; then
		i=$[$i-1]
		continue
	fi
	if [ $i -eq 39 ]; then
		i=$[$i-1]
		continue
	fi
	FILENAME="reference_${i}_tsh.c.${SCOPE}.tree"
	java -jar $DIR/apted.jar -f $DIR/brk_tree/$FILENAME $DIR/brk_tree/empty.tree >> $DIR/score/${SCOPE}_tsh.txt
	echo "Empty Distance: $FILENAME" >> $DIR/score/${SCOPE}_tsh.txt
	echo "Empty Distance: $FILENAME"
	i=$[$i-1]
done 




