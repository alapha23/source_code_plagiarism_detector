#ifndef __SCORER
#define __SCORER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#define DEBUG(a) fprintf(stderr, #a"\n"); \
	fflush(stderr)
#define DEBUF(a, b) fprintf(stderr, a"\n", b); \
	fflush(stderr)
#define EMIT(name1, name2, type, scope) fprintf(stdout, "| %s | %s | %s | %s |\n", name1, name2, type, scope); \
	fflush(stdout)
#define bigger(a, b)	((a)>(b) ? (a):(b))
#define smaller(a, b)	((a)>(b) ? (b):(a))
#define SUSP	"suspicious"
#define GUIL	"guilty"
#define SAFE	"safe"

typedef struct empty
{
	int id;
	char filename[128];	// we check length of file name 
	int e_dis;		// empty distance
} node;

FILE	*fp=NULL;
node 	**pool=NULL;
int	n_inpool=0;

void eval_file(char *);
void open_file(char **);
size_t get_filesize(void);

void add2pool(void);	// name1 should not be longer than 64
void cur_ref(char *);
void cur_prev(char *);
void cur_cur(char *);

static node *search_pool(char*);

#endif
