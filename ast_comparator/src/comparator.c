#include "comparator.h"

// Open dot file 
// generate tree file

int main(int argc, char**argv)
{
	size_t filesize;
	char *line;
	int temp;

	open_file(argv);

	filesize = get_filesize(fp);

	pool = calloc(filesize/20, sizeof(tree_node*));

	for(temp=0; temp<3; temp++)
	{
		read_line(fp);
		// ignore header
	}

	while((line = read_line(fp))!=NULL)
	{
		// 1 for decl_node; 0 for link node
		if(check_type(line)==1)	// declare node | link node
			decl_n(line);
		else
			link_n(line);
	}

	root = search_pool(1);
	dump_tree(root);

	free(pool);
   return 0;
}

void dump_tree(tree_node *root)
{
	int i;
	fprintf(stdout, "{ node%d ", root->_id);
	for(i=0; i < root->n_child; i++)
	{
		dump_tree(*(root->l_child+i));
	}
	fprintf(stdout, "}");

}

static tree_node *search_pool(int id)
{
	int temp;

//	DEBUG(search pool);
//	DEBUF("n_inpool = %d", n_inpool);
	for(temp = n_inpool-1; temp>=0; temp--)
	{
		tree_node *cur_node;
		cur_node = *(pool+temp);
//		DEBUF("Searching, cur_node->_id= %d", cur_node->_id);

		assert(NULL != cur_node);
		if(id == cur_node->_id)
			return cur_node;	
	}
//	DEBUG(search complete);
}

void decl_n(char *line)
{
	// ADD to Pool
	tree_node *node = calloc(1, sizeof(tree_node));
//	DEBUF("decl node: %s", line);

	node->nodeN = calloc(64, sizeof(char));
	sscanf(line, "%s", node->nodeN);

	sscanf(node->nodeN+4, "%d", &node->_id);

	node->n_child = 0;
	node->l_child = calloc(1, sizeof(tree_node*));

	*(pool+n_inpool) = node;	// Add to pool
	n_inpool++;

	free(line);
}

void link_n(char *line)
{
	// parent & child must both exist
	tree_node *parent;
	tree_node *child;
	int parent_id, child_id;
	char temp[64];
//	DEBUF("%s", line);

	sscanf(line, " node%d", &parent_id);
	char *position = strstr(line, "->");
	sscanf(position, "-> node%d", &child_id);

//	DEBUF("parent_id= %d", parent_id);

	// Search pool for parent
	// Search pool for child
	parent = search_pool(parent_id);
	child = search_pool(child_id);
	assert(NULL != parent);
	assert(NULL != child);
	assert(NULL != parent->l_child);

	// Resize parent's child nodes
	parent->n_child++;
	parent->l_child = realloc(parent->l_child, parent->n_child * sizeof(tree_node*));
	// add child to parent
	*(parent->l_child+parent->n_child-1) = child;
	//child->parent = parent;
}

int check_type(char *line)
{
	//  node4 -> node5;
	//  node6 [label="call get_filesize",shape=ellipse];
	//  node4 -> node6 [style=dotted];
	int type;
	if (strstr(line, "->") == NULL)
		type = 1;
	else type = 0;

	return type;
}

char *read_line(FILE *fileptr)
{
	char *line = calloc(160, sizeof(char));
	if(fgets(line, 80, fileptr)==NULL)
		return NULL;
	if(*line == '}')
	{	// last line
		free(line);
		return NULL;
	}
	char newline[80];
//	DEBUF("%c", *(line+strlen(line)));
//	DEBUF("%c", *(line+strlen(line)-1));
//	DEBUF("%c", *(line+strlen(line)-2));

	while(*(line+strlen(line)-2) != ';')
	{
		fgets(newline, 80, fileptr);
		strcat(line, newline);
		line = realloc(line, (80+strlen(line))*sizeof(char));
	}
//	DEBUF("%s", line);
	return line;
}

void
open_file(char**argv)
{
	char *filedir = argv[1];
        if((fp =fopen(filedir, "r")) == NULL)
        {
                fprintf(stderr, "filedir: %s\n", filedir);
                fprintf(stderr, "Usage: ./comparator <file dir>\n");
                fflush(stderr);
                exit(0);
        }
}

size_t
get_filesize(FILE *fileptr)
{
        // only call this function before you move the ptr to file position
	assert(NULL != fileptr);
        fseek(fileptr, 0L, SEEK_END);

        size_t size =(size_t)ftell(fileptr);
        rewind(fileptr);
        return size;
}

static char
peek(FILE *fileptr)
{
        // peek next
        // eat \n
        int next = fgetc(fileptr);
        ungetc(next, fileptr);

        return (char)next;
}

