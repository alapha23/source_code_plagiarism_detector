FILENAME=$1
SCOPENAME=$2
FILEDIR=$3
# yell() { echo "$0: $*" >&2; }
# die() { yell "$*"; exit 111; }
# try() { "$@" || die "cannot $*"; }

if [ -z "$SCOPENAME" ]; then
	echo "Usage: bash ./gen_ast.sh <file name> <scope name> <file dir>"
	echo "test files are supposed to be placed under ast-comparator/test"
	echo "example: bash ./gen_ast.sh helloworld.c main test/"
	exit 1;
fi

gcc -o tu_eater tu_eater.c tu_eater.h
gcc -std=c99 -lpthread -fdump-translation-unit -fno-builtin -ffreestanding  "../$FILEDIR$FILENAME" -o temp

./tu_eater "${FILENAME}.001t.tu" $SCOPENAME > ../dot_obj/"${FILENAME}.dot"
# filename test/xxx.c 
# 

dot -Tpdf -o ../dot_obj/"${FILENAME}.pdf" ../dot_obj/"${FILENAME}.dot"
# atril ../dot_obj/"${FILENAME}.pdf" &

rm  *.*.* tu_eater temp
unset FILENAME SCOPENAME FILEDIR

# fno-builtin: Don't recognize built-in functions that do not begin with __builtin_ as prefix.
#
# ffreestanding: Assert that compilation targets a freestanding environment.  This
#        implies -fno-builtin.  A freestanding environment is one in which
#        the standard library may not exist, and program startup may not
#        necessarily be at "main".  The most obvious example is an OS
#        kernel.  This is equivalent to -fno-hosted.
#
# fdump-translation-unit-options (C++ only)
#        Dump a representation of the tree structure for the entire
#        translation unit to a file.

