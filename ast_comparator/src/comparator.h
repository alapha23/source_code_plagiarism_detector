#ifndef __COMP
#define __COMP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEBUG(a) fprintf(stderr, #a"\n"); \
        fflush(stderr)
#define DEBUF(a, b) fprintf(stderr, a"\n", b); \
        fflush(stderr)

FILE *fp;

typedef struct tree
{
        char *nodeN;
        int _id;
        int n_child;
	struct tree *parent;
        struct tree **l_child;
} tree_node;

tree_node *root;
tree_node **pool;
int     n_inpool=0;


void open_file(char **name);
size_t get_filesize(FILE *fileptr);
static char peek(FILE *fileptr);
char *read_line(FILE *fileptr);
int check_type(char *line);
void link_n(char *line);
void decl_n(char *line);
void dump_tree(tree_node *root);

static tree_node *search_pool(int id);

#endif
