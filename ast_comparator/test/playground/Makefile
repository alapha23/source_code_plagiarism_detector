CFLAGS=-fdump-translation-unit -fno-builtin -ffreestanding 

SRC=helloworld.c helloworld.h
#OUT=helloworld.rm_header.c helloworld.rm_header.h

all: 
	gcc -o rm_header rm_header.c
	./rm_header $(SRC)
	gcc $(CFLAGS) $(SRC)
	./tu_eater helloworld.c.001t.tu > ast.txt
	less ast.txt

tu_eater:
	gcc -o tu_eater tu_eater.c tu_eater.h

clean:
	rm rm_header *.*.*

# fno-builtin: Don't recognize built-in functions that do not begin with __builtin_ as prefix.
#
# ffreestanding: Assert that compilation targets a freestanding environment.  This
#	 implies -fno-builtin.  A freestanding environment is one in which
# 	 the standard library may not exist, and program startup may not
#	 necessarily be at "main".  The most obvious example is an OS
#	 kernel.  This is equivalent to -fno-hosted.
#
# fdump-translation-unit-options (C++ only)
# 	 Dump a representation of the tree structure for the entire
#	 translation unit to a file. 


