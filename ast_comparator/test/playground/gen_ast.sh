FILENAME=$1
SCOPENAME=$2
foo=".001t.tu"
# yell() { echo "$0: $*" >&2; }
# die() { yell "$*"; exit 111; }
# try() { "$@" || die "cannot $*"; }


#rm rm_header *.*.*
rm tu_eater

if [ -z "$SCOPENAME" ]; then
	echo "Usage: bash ./gen_ast.sh <file name> <scope name>"
	exit 1;
fi

gcc -o tu_eater tu_eater.c tu_eater.h
#gcc -o rm_header rm_header.c
#./rm_header $FILENAME > temp.c
# helloworld.c -> helloworld.rm_header.c
gcc -fdump-translation-unit -fno-builtin -ffreestanding -c  $FILENAME 
./tu_eater "$FILENAME$foo" $SCOPENAME > ast.dot
dot -Tpdf -o ast.pdf ast.dot 

#rm temp.c *.o
unset FILENAME SCOPENAME
