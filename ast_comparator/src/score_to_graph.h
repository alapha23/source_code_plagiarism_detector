#ifndef SCORE_TO_GRAPH
#define SCORE_TO_GRAPH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define N_REF	2<<14	// Number of references to be compared to
#define	N_SCOPE	32
#define N_CUR	128*N_SCOPE
#define Tolerance	30	// threshold. Plagiarism Below are ignored

#define DEBUG(a) fprintf(stderr, #a"\n"); \
	fflush(stderr)
#define DEBUF(a, b)	fprintf(stderr, a"\n", (b)); \
	fflush(stderr)
#define readline(line, size, fp) \
       	({ char *_flag; _flag = line;\
	 if(fgets((line), 128, fp)==NULL) \
		{	\
			DEBUG(reached EOF); \
			_flag = NULL;	\
		}\
		_flag;\
	})
#define is_scope(line) (*(line) == 'S'? (1):(0))

#define emit_footer()	({fprintf(stdout, "\n}\n"); fflush(stdout);})

#define dot_box(_id, label)	({fprintf(stdout, "  node%s [label=\"%s\", shape=box];\n", _id, label); fflush(stdout);})

#define dot_ellipse(id,label)	({fprintf(stdout, "  node%s [label=\"%s\", shape=ellipse];\n", id, label); fflush(stdout);\
		})
#define dot_link(id1, id2, label)	({fprintf(stdout, "  node%s -> node%s [label=\"%s\", color=red, fontsize=15];\n", id1, id2, label);}) 


typedef struct _p
{
	char hername[64];
	char scope[64];	// the relationship exist within a scope
	int target_id;	// my target_id of my parent node
	int hertype;	// 0 for cur, 1 for prev, 2 for ref
	float match;	
} plagiarism;

typedef struct _n
{
	int id;			// id in pool starting from 0. *(pool+id)
	char filename[64];
	int n_plagiarism;		// how many target node has
	plagiarism *plag[N_REF];
} node;				// node stands of the left hand side of comparison

// fileptr for Similarity_tsh.txt
FILE *fp = NULL;
// N_SCOPE scopes with length of 64
char scopes[N_SCOPE][64] = {'\0'};

int n_inpool = 0;
node **pool = NULL;	// pool contains all relationships, namely nodes
char **scope_p = NULL;
int n_inscope = 0;
int safezone[N_CUR];
int n_insafe = 0;

// Open file
void open_file(int, char **);

// get file size
size_t get_filesize(void);

// put all data inside the pool
void fill_pool(void);

// add a node
node *add_to_node(char*);

// search node in pool
node *search_pool(char *);

// decide hertype from her name
int check_type(char *);

// check if the node has plagiarism according to the scope
int plagiarism_scope(node *, char*);

// add node to safe zone
void add_to_safe(node *, char*);

// emit_header
void emit_header(char *scope, char *whovswho);

void eval_pool(char*, char*);

void pool_to_neo4j(char*);

// create ref nodes
void ref_to_neo4j(char*, char*);

// create prev nodes
void prev_to_neo4j(char*, char*);

// create prev nodes
void stu_to_neo4j(char*, char*);


// Relationship between current & ref > cur_ref_tsh.dot
void cur_ref_dot(char *);

// Relationship between current & prev
void cur_prev_dot(char *);

// Relationship between current & cur
void cur_cur_dot(char *);

// Relationship between current & ALL
void cur_all_dot(char *);

void emit_safe(void);

// only for debugging
void dump_node(node *);

#endif
