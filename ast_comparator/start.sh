#! /bin/bash

echo "All scopes we are evaluating:\n"
while IFS='' read -r line || [[ -n "$line" ]]; do
	echo $line
done < "test/scope.conf"
echo ""
echo ""

if [ -f log/log.txt ]; then
	rm log/log.txt
fi
i=0
line=""
while IFS='' read -r line || [[ -n "$line" ]]; do
	if [[ i -eq 0 ]]; then 	# ignore SCOPE
		i=$[$i+1]
		continue 
	fi
	
	line="$(echo -e "${line}" | tr -d '[:space:]')"

	echo "Analizing Scope: "$line
	echo "Generate *.dot files under dot_obj/"
	
	s=63
	while [ $s -ge 0 ]
	do
		FILENAME="${s}_tsh.c.001t.tu"
		bin/tu_eater test/tu/$FILENAME $line > dot_obj/"$FILENAME.$line.dot" 3>>log/log.txt 2>>log/log.txt
		s=$[$s-1]
	done
	s=22
	while [ $s -ge 0 ]
	do
		FILENAME="prev_${s}_tsh.c.001t.tu"
		bin/tu_eater test/tu/$FILENAME $line > dot_obj/"$FILENAME.$line.dot" 3>>log/log.txt 2>>log/log.txt

		s=$[$s-1]
	done
	s=39
	while [ $s -ge 0 ]
	do
		FILENAME="reference_${s}_tsh.c.001t.tu"
		if [ ! -f test/tu/$FILENAME ]; then
			s=$[$s-1]
			continue
		fi
		bin/tu_eater test/tu/$FILENAME $line > dot_obj/"$FILENAME.$line.dot" 3>>log/log.txt 2>>log/log.txt

		s=$[$s-1]
	done
	i=$[$i+1]
	echo ""

	
done < "test/scope.conf"


echo ""
echo "Then we convert dot files to bracket tree representation "
echo ""

for FILENAME in dot_obj/*.dot; do
	FILENAME=`basename $FILENAME .dot`
	#echo "Dealing with "$FILENAME
	./bin/comparator dot_obj/"$FILENAME.dot" > brk_tree/"${FILENAME}.tree" 3>>log/log.txt 2>>log/log.txt
done

echo ""
echo "Now compare tree of students' assignment with references. This might take more than 10 mins."
echo ""
python3 utils/com_student_ref.py

