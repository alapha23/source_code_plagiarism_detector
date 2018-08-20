CC=gcc
CCFLAGS=-std=c99 -g -O0 -Wall

SRC_DIR=src
OBJ_DIR=obj
all: comparator


comparator: 
	$(CC) $(CCFLAGS) -o $@  $(SRC_DIR)/*


clean:
	rm -f comparator
