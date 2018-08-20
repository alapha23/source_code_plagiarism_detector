#include "score_to_graph.h"

/* Open score files, collect plagiarism results & possibility
 *
 *
 * **/

int main(int argc, char **argv)
{
	size_t filesize;
	
	open_file(argc, argv);
	filesize = get_filesize();

	pool = calloc(N_CUR, sizeof(node*));
	scope_p = (char**)calloc(N_SCOPE, 64*sizeof(char));
	
	fill_pool();

	pool_to_neo4j(argv[2]);
//	eval_pool(argv[2], argv[3]);

	fclose(fp);
	free(pool);
	return 0;
}

void eval_pool(char *scope, char*options)
{
	switch(*(options+1))
	{
		case 'a':
			cur_all_dot(scope);
			break;
		case 'c':
			cur_cur_dot(scope);
			break;
		case 'p':
			cur_prev_dot(scope);
			break;
		case 'r':
			cur_ref_dot(scope);
			break;
		default:
			DEBUF("Option error: %s", options);
			exit(0);
	}

}

void open_file(int argc, char **argv)
{

	if((fp = fopen(argv[1], "r")) == NULL || argc < 4)
	{
		DEBUF("Filedir: %s", argv[1]);
		DEBUG("Usage: ./score_to_graph <file dir> <scope> <option>");
		DEBUG("Options: ca --> dump all node relations");
		DEBUG("Options: cc --> dump plagiarism relations between current students");
		DEBUG("Options: cp --> dump plagiarism relations between current students and previous students");
		DEBUG("Options: cr --> dump plagiarism relations between current students and references");
		exit(0);	
	}
}

size_t get_filesize(void)
{
	fseek(fp, 0L, SEEK_END);
	size_t size = (size_t)ftell(fp);
	rewind(fp);
	return size;
}

void emit_header(char *scope, char *whovswho)
{
        fprintf(stdout, "digraph AST {\n");
        fprintf(stdout, "  graph [fontname=\"Times New Roman\",fontsize=10];\n");
        fprintf(stdout, "  node  [fontname=\"Courier New\",fontsize=10];\n");
        fprintf(stdout, "  edge  [fontname=\"Times New Roman\",fontsize=10];\n\n");
        
        fprintf(stdout, "  node0 [label=\"%s  %s\",shape=box];\n", scope, whovswho);
        fflush(stdout);
}

void fill_pool(void)
{
	char line[128];
	char *scope = calloc(64, sizeof(char));

	while(1)
	{
		if(NULL == readline(line, 128, fp))
		{
			// EOF
			break;
		}
		if(is_scope(line))	
		{
			sscanf("SCOPE: %s", scope);
			*(scope_p+n_inscope) = scope;
			n_inscope++;
			if(n_inscope > N_SCOPE)
			{
				DEBUG("Number of scopes exceeds expectation");
				DEBUG("Modify N_SCOPE in the header please");
			}
		}		
		else
		{
			// Add a new node
			if(add_to_node(line) == NULL)
				break;	
			//dump_node(n);
		}
	}
}

node *add_to_node(char *line)
{
	// has myname existed in pool?
	char *myname = calloc(64, sizeof(char));
	node *n;
	plagiarism *p;

	assert(NULL != line);
	if(*line == ' ')
		return NULL;

	sscanf(line, "| %s ", myname);
	
	n = search_pool(myname);
	if(n == NULL)
	{
		// not exist, build a new node
		n = (node *)calloc(1, sizeof(node));
		// add new node to pool
		*(pool+n_inpool) = n;
		n->id = n_inpool;
		strcpy(n->filename, myname);
		n_inpool++;
		if(n_inpool >= N_CUR)
		{
			DEBUG("Number of current students exceeds N_CUR");
			DEBUG("Modify N_CUR in the header please");
			exit(0);
		}
	}

	// ignore plagiarism lower than 40%
	float match;
	sscanf(line, "| %*s | %*s | %f | %*s ", &match);
	match = match *100;
//	DEBUF("match: %.2f", match);
//	DEBUF("Threshold %d", Tolerance);
	if(match > (float)Tolerance)
	{
		// build a new plagiarism relationship
		n->n_plagiarism++;

		p = calloc(1, sizeof(plagiarism));
		sscanf(line, "| %*s | %s | %f | %s ", p->hername, &p->match, p->scope);
		p->target_id = n->n_plagiarism;
	       	p->hertype = check_type(p->hername);

		// add plagiarsm
		assert(n->n_plagiarism != N_REF);
		n->plag[n->n_plagiarism-1] = p;

		free(myname);
		return n;
	}
	else
		return n;
}

int check_type(char *hername)
{
	if(*(hername) == 'r')
	{
	// reference
		return 2;
	}else if (*(hername) == 'p')
	{
		return 1;
	}
	else
		return 0;
}

node *search_pool(char *myname)
{
	node *ret = NULL;
	int i = n_inpool;
	while(i)
	{
		ret = *(pool + i - 1);
//		printf("myname:%s vs pool:%s\n", myname, ret->filename);
//		fflush(stdout);
		if(!strcmp(myname, ret->filename))
			return ret;
		i--;
	}
	return NULL;
}

void dump_node(node *n)
{
	int i = n->n_plagiarism;
	plagiarism *p;
	DEBUF("node id: %d", n->id);
	DEBUF("my name: %s", n->filename);
	DEBUF("Num of plagiarisms: %d", n->n_plagiarism);
	while(i)
	{
		p = n->plag[i-1];
		DEBUF("Plagiarism id: %d", p->target_id);
		DEBUF("hername : %s", p->hername);
		DEBUF("Hertype: %d", p->hertype);
		DEBUF("scope: %s", p->scope);
		DEBUF("match: %f", p->match);
		i--;
	}
}

void emit_safe(void)
{
	fprintf(stdout, "nodesafe [shape=none, margin=0, label=<\n  \
			<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n \
			<TR> <TD COLSPAN=\"5\"><FONT COLOR=\"red\">SAFE ZONE</FONT></TD>\n \
			</TR>\n");
	int i = n_insafe;
	while(i)
	{
		node *safe = *(pool + safezone[i-1]);
		fprintf(stdout, "<TR><TD COLSPAN=\"4\">%s</TD></TR>\n", safe->filename);
		i--;
	}
	fprintf(stdout, "</TABLE>>];");
	fflush(stdout);
	DEBUF("numbers in safe zone: %d", n_insafe);
}

int plagiarism_scope(node *n, char *scope)
{
	int plagiarism_inscope=0;
	int backup = n->n_plagiarism;
	while(backup)
	{
		if(strcmp(scope, n->plag[backup-1]->scope))
		{
			backup--;
			continue;
		}
		plagiarism_inscope = 1;
		break;
	}
	return plagiarism_inscope;
}

void add_to_safe(node *n, char *scope)
{
	char node_s[64];
	sscanf(n->filename, "%*[^.].%[^.]", node_s);
	if (strcmp(node_s, scope))
		return ;
	 // add node to safe zone
	safezone[n_insafe] = n->id;
	n_insafe++;
	if(n_insafe > N_CUR)		
	{
		DEBUG("SAFE ZONE overflowing");
		exit(0);
	}
}

void cur_all_dot(char *scope)
{
	int iter = n_inpool-1;
	node *n;
	plagiarism *p;
	char *id1 = calloc(32, sizeof(char));
	char *id2 = calloc(32, sizeof(char));
	char link_label[64];
	emit_header(scope, "cur vs. prev");
	while(iter)
	{
		n = *(pool+iter-1);
		int i = n->n_plagiarism;
		sscanf(n->filename, "%[^.]%*s", id1);

		// check number of plagiarism according to the scope
		if(plagiarism_scope(n, scope) != 0)
		{
			dot_box(id1, n->filename);

			while(i)
			{
				p = n->plag[i-1];
				// check if scope meets condition
				if(strcmp(scope,p->scope))
				{
					i--;
					continue;
				}
				// plagiarism dots
				sscanf((n->plag)[i-1]->hername, "%[^.]", id2);
				dot_ellipse(id2, (n->plag)[i-1]->hername);
				sprintf(link_label, "%s: %.1f\%", scope, (n->plag)[i-1]->match*100.0);
				dot_link(id1, id2, link_label);
				i--;
			}
		}
		else
		{
			add_to_safe(n, scope);
		}
		iter--;
	}
	emit_safe();
	emit_footer();
	free(id1);
	free(id2);
}

void cur_ref_dot(char *scope)
{
	int iter = n_inpool-1;
	node *n;
	plagiarism *p;
	char *id1 = calloc(32, sizeof(char));
	char *id2 = calloc(32, sizeof(char));
	char link_label[64];
	emit_header(scope, "cur vs. prev");
	while(iter)
	{
		n = *(pool+iter-1);
		int i = n->n_plagiarism;
		sscanf(n->filename, "%[^.]%*s", id1);
		if(plagiarism_scope(n, scope) != 0)
		{

		dot_box(id1, n->filename);
		while(i)
		{
			p = n->plag[i-1];
			// check if scope meets condition--> scope & ref
			if(strcmp(scope,p->scope))
			{
//				fprintf(stderr,"Not this scope: %s compared to %s\n", (n->plag)[i-1]->scope, scope);
				i--;
				continue;
			}
			if(check_type(p->hername) != 2)
			{
				i--;
				continue;
			}
			// plagiarism dots
			sscanf((n->plag)[i-1]->hername, "%[^.]", id2);
			dot_ellipse(id2, (n->plag)[i-1]->hername);
			sprintf(link_label, "%s: %.1f\%", scope, (n->plag)[i-1]->match*100.0);
			dot_link(id1, id2, link_label);
			i--;
		}}
		else
		{
			add_to_safe(n, scope);
		}

		iter--;
	}
	emit_footer();
	free(id1);
	free(id2);

}

void cur_prev_dot(char *scope)
{

	int iter = n_inpool-1;
	node *n;
	plagiarism *p;
	char *id1 = calloc(32, sizeof(char));
	char *id2 = calloc(32, sizeof(char));
	char link_label[64];
	emit_header(scope, "cur vs. prev");
	while(iter)
	{
		n = *(pool+iter-1);
		int i = n->n_plagiarism;
		sscanf(n->filename, "%[^.]%*s", id1);
		if(plagiarism_scope(n, scope) != 0)
		{

		dot_box(id1, n->filename);
		while(i)
		{
			p = n->plag[i-1];
			// check if scope meets condition--> scope & ref
			if(strcmp(scope,p->scope))
			{
//				fprintf(stderr,"Not this scope: %s compared to %s\n", (n->plag)[i-1]->scope, scope);
				i--;
				continue;
			}
			if(check_type(p->hername) != 1)
			{
				i--;
				continue;
			}
			// plagiarism dots
			sscanf((n->plag)[i-1]->hername, "%[^.]", id2);
			dot_ellipse(id2, (n->plag)[i-1]->hername);
			sprintf(link_label, "%s: %.1f\%", scope, (n->plag)[i-1]->match*100.0);
			dot_link(id1, id2, link_label);
			i--;
		}}
		else
		{
			add_to_safe(n, scope);
		}

		iter--;
	}
	emit_footer();
	free(id1);
	free(id2);


}

void cur_cur_dot(char *scope)
{

	int iter = n_inpool-1;
	node *n;
	plagiarism *p;
	char *id1 = calloc(32, sizeof(char));
	char *id2 = calloc(32, sizeof(char));
	char link_label[64];
	emit_header(scope, "cur vs. prev");
	while(iter)
	{
		n = *(pool+iter-1);
		int i = n->n_plagiarism;
		sscanf(n->filename, "%[^.]%*s", id1);
		if(plagiarism_scope(n, scope) != 0)
		{

		dot_box(id1, n->filename);
		while(i)
		{
			p = n->plag[i-1];
			// check if scope meets condition--> scope & ref
			if(strcmp(scope,p->scope))
			{
//				fprintf(stderr,"Not this scope: %s compared to %s\n", (n->plag)[i-1]->scope, scope);
				i--;
				continue;
			}
			if(check_type(p->hername) != 0)
			{
				i--;
				continue;
			}
			// plagiarism dots
			sscanf((n->plag)[i-1]->hername, "%[^.]", id2);
			dot_ellipse(id2, (n->plag)[i-1]->hername);
			sprintf(link_label, "%s: %.1f\%", scope, (n->plag)[i-1]->match*100.0);
			dot_link(id1, id2, link_label);
			i--;
		}}
		else
		{
			add_to_safe(n, scope);
		}

		iter--;
	}
	emit_footer();
	free(id1);
	free(id2);

}

void prev_to_neo4j(char*label, char*scope)
{
	int prev_counter = 22;
	char *cypher = calloc(256, sizeof(char));

	assert(NULL != label);
	assert(NULL != scope);
	while(prev_counter)	
	{
//		sprintf(cypher, "CREATE (ref_%d:REFERENCE {scope:\"%s\"})", ref_counter, scope);
		sprintf(cypher, "CREATE (prev_%d:%sPREVIOUS {filename:\"prev_%d\"})", prev_counter, label,prev_counter);

		fprintf(stdout, "%s\n", cypher);
		prev_counter--;
	}
	fflush(stdout);
	free(cypher);
}

void ref_to_neo4j(char*label, char *scope)
{
	int ref_counter = 39;
	char *cypher = calloc(256, sizeof(char));

	assert(NULL != label);
	assert(NULL != scope);
	while(ref_counter)	
	{
/*		if(ref_counter == 8 || ref_counter == 9 || ref_counter == 28 \
				|| ref_counter == 29 || ref_counter == 35 || ref_counter == 39)
		{
			ref_counter--;
			continue;
		}*/
//		sprintf(cypher, "CREATE (ref_%d:REFERENCE {scope:\"%s\"})", ref_counter, scope);
		sprintf(cypher, "CREATE (ref_%d:%sREFERENCE {filename:\"ref_%d\"})", ref_counter, label,ref_counter);

		fprintf(stdout, "%s\n", cypher);
		ref_counter--;
	}
	fflush(stdout);
	free(cypher);
}

void stu_to_neo4j(char *label, char *scope)
{
// CREATE current students nodes
	int stu_counter = 63;
	char *cypher = calloc(256, sizeof(char));

	assert(NULL != label);
	assert(NULL != scope);
	while(stu_counter)
	{
//		sprintf(cypher, "CREATE (student%d_tsh:student {scope: \"%s\"})", stu_counter-1, scope);
		sprintf(cypher, "CREATE (student%d_tsh:%sSTUDENT {filename:\"%d_tsh.c\" })", stu_counter-1, label, stu_counter-1);

		fprintf(stdout, "%s\n", cypher);
		stu_counter--;
	}
	fflush(stdout);
	free(cypher);
}

void pool_to_neo4j(char *scope)
{
        int iter = n_inpool-1;	// iterate through all in the pool
        node *n;
        plagiarism *p;
        char link_label[64];
	char *cypher = calloc(256, sizeof(char));

	assert(NULL != scope);
	// CREATE reference nodes
//	ref_to_neo4j("Test", scope);
	// CREATE current students nodes
//	prev_to_neo4j("Test", scope);
//	stu_to_neo4j("Test", scope);
	while(iter)
        {
                n = *(pool+iter-1);
                int i = n->n_plagiarism;	// how many plagiarism does node have
		while(i)
		{
			p = n->plag[i-1];
			if(strcmp(scope,p->scope))
                        {
	                       i--;
		               continue;
			}
			if(p->match < 0.9)	// theshold of 90%
			{
				i--;
				continue;
			}
/*			if(p->hertype == 0)	// 
			{
				i--;
				continue;
			}*/
			int *id_student = calloc(1, sizeof(int));
			int *id_ref = calloc(1, sizeof(int));
			sscanf(n->filename, "%d%*[^_]", id_student);
			if(p->hertype != 0)
				sscanf(p->hername, "%*[^_]_%d_tsh.c", id_ref);
			else
				sscanf(p->hername, "%d%*[^_]", id_ref);
//			DEBUF("id student: %d", *id_student);
//			DEBUF("reference: %d", *id_ref);
//			exit(0);
			if(p->hertype == 2)
				sprintf(cypher, "CREATE (student%d_tsh)-[:Plagiarism {conformance:%.2f, scope: \"%s\"}]->(ref_%d)", *id_student, p->match*100, scope,*id_ref);
			else if(p->hertype == 1)
				sprintf(cypher, "CREATE (student%d_tsh)-[:Plagiarism {conformance:%.2f, scope: \"%s\"}]->(prev_%d)", *id_student, p->match*100, scope, *id_ref);
			else 
			{
				if(*id_ref == *id_student)
				{	i--; continue;
				}
				sprintf(cypher, "CREATE (student%d_tsh)-[:Plagiarism {conformance:%.2f, scope: \"%s\"}]->(student%d_tsh)", *id_student, p->match*100, scope, *id_ref);
			}

			fprintf(stdout, "%s\n", cypher);
			i--;
		}
                iter--;
	}
	free(cypher);
}
