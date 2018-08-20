#include "scorer.h"

int main(int argc, char **argv)
{
	size_t filesize;

	open_file(argv);
	filesize = get_filesize();

	// pool storing empty distance
	pool = (node **)calloc(filesize/1000, sizeof(node*));
	eval_file(argv[2]);
	free(pool);
}

static node *search_pool(char *filename)
{
	int i = n_inpool-1;
	node *n;
	assert(filename != NULL);
	while(i+1)
	{
		n = *(pool + i);
		if(!strcmp(filename, n->filename))
		{
			// found the node
			return n;
		}
//		printf("Different: %s & %s \n", filename, n->filename);
		i--;
	}
	DEBUG(TREE not in Pool);
	return NULL;
}

void eval_file(char *scope)
{
	// firstly add empty distances to pool
	// check if empty distance to the end
	// if not
	// add to pool
	add2pool();

	// current students vs. reference
	// emit format:
	// | FILENAME1 | FILENAME2 | TYPE        | SCOPE |
	// | example.c | example.c | Suspicious  | main  |
	// | example.c | example.c | SAFE        | main  |
	// | example.c | example.c | GUILTY      | main  |
	cur_ref(scope);

	//
	// current students vs. prev
	cur_prev(scope);

	// current students vs. current students
	cur_cur(scope);
}

void cur_ref(char *scope)
{
/*	// Only for debugging
 	int i = n_inpool-1;
	while(i)
	{
		node *n = *(pool+i);
		assert(n != NULL);
		printf("%s: %d\n", n->filename, n->e_dis);	
		i--;
	}
*/
	char line[128];
	int score;
	char *filename1 = calloc(128, sizeof(char));
	char *filename2 = calloc(128, sizeof(char));
	node *n1;
	node *n2;

	while(1)
	{
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] == 'C')
		{
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
			break;
		}
	}
	// Current vs ref is recognized

	while(1)
	{
		// ignore empty line
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] < '0' || line[0] > 'z')
		{
		// This function is done
			return ;
		}

		// filename1 vs. All REFERENCES
		sscanf(line, "%s vs. %*s", filename1);
		n1 = search_pool(filename1);
		// n1 is the current we would like to compare
		while(1)
		{
			// read score line
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
			if((line[0] < '0') || (line[0] > 'z'))
			{
			// current has been finished
				break;	
			}
			if(!strncmp(line, "TREE", 4))
			{
				// no score
				DEBUG("Bad tree analysis, abandoned");
				// ignore line
				if(fgets(line, 128, fp) == NULL)
				{
					DEBUG(Unexpected reached EOF);
					exit(0);	
				}
				continue;
			}
			if(n1 == NULL )
			{
				DEBUF("Bad Tree selected:%s", filename1);
				continue;
			}

			sscanf(line, "%d", &score);
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}

			sscanf(line, "%*s vs. ref: %s", filename2);

			n2 = search_pool(filename2);
			// Two nodes
			if(n2 == NULL)	
			{
			// The second node is bad
				DEBUG(Second node is bad);
				continue;
			}
			// eval nodes
			int avg = (n1->e_dis + n2->e_dis)/2;
			int bias = n1->e_dis > n2->e_dis ? n2->e_dis/10: n1->e_dis/10;

			if(score < smaller(n1->e_dis, n2->e_dis) - bias)
			{
				char percent[8];
	float percent_i = 1 - ((float)score)/((float)smaller(n1->e_dis, n2->e_dis)-(float)bias);

				sprintf(percent, "%f\%", percent_i);
				// GUILTY
				EMIT(filename1, filename2, percent, scope);		
			}
		}
	}
}

void cur_prev(char *scope)
{
	char line[128];
	int score;
	char *filename1 = calloc(128, sizeof(char));
	char *filename2 = calloc(128, sizeof(char));
	node *n1;
	node *n2;

	while(1)
	{
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] == 'C')
		{
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
			break;
		}
	}

	while(1)
	{
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] < '0' || line[0] > 'z')
		{
			// This function is done
			return ;
		}
		if(!strncmp(line, "TREE", 4))
		{
				// no score
				DEBUG("Bad tree analysis, abandoned");
				// ignore line
				if(fgets(line, 128, fp) == NULL)
				{
					DEBUG(Unexpected reached EOF);
					exit(0);	
				}
				continue;
		}
		if(n1 == NULL )
		{
			DEBUF("Bad Tree selected:%s", filename1);
			continue;
		}


		// filename1 vs previous
		sscanf(line, "%s vs. %*s", filename1);
		n1 = search_pool(filename1);

		while(1)
		{
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
//			DEBUF("ascii: %d", line[0]);
			if((line[0] < '0') || (line[0] > 'z'))
			{
			// this node have been finished
				break;	
			}
			sscanf(line, "%d", &score);
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}

			sscanf(line, "%*s vs. ref: %s", filename2);

			n2 = search_pool(filename2);
			// Two nodes
			if(n2 == NULL)	
			{
			// The second node is bad
				DEBUG(Second node is bad);
				continue;
			}

			// eval nodes
			int avg = (n1->e_dis + n2->e_dis)/2;
			int bias = n1->e_dis > n2->e_dis ? n2->e_dis/10: n1->e_dis/10;
			if(score < smaller(n1->e_dis, n2->e_dis) - bias)
			{
				char percent[8];
				float percent_i = 1 - ((float)score)/((float)smaller(n1->e_dis, n2->e_dis)-(float)bias);
				sprintf(percent, "%f\%", percent_i);
	
				// GUILTY
				EMIT(filename1, filename2, percent, scope);		
			}
		//score > bigger(n1->e_dis, n2->e_dis) + bias
		}
	}


}

void cur_cur(char *scope)
{
	char line[128];
	int score;
	char *filename1 = calloc(128, sizeof(char));
	char *filename2 = calloc(128, sizeof(char));
	node *n1;
	node *n2;

	while(1)
	{
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] == 'C')
		{
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
			break;
		}
	}

	while(1)
	{
		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Unexpected reached EOF);
			exit(0);	
		}
		if(line[0] < '0' || line[0] > 'z')
		{
		// This function is done
			return ;
		}

		sscanf(line, "%s vs. %*s", filename1);
		n1 = search_pool(filename1);

		while(1)
		{
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}
//			DEBUF("ascii: %d", line[0]);
			if((line[0] < '0') || (line[0] > 'z'))
			{
			// this node have been finished
				break;	
			}
			if(!strncmp(line, "TREE", 4))
			{
				// no score
				DEBUG("Bad tree analysis, abandoned");
				// ignore line
				if(fgets(line, 128, fp) == NULL)
				{
					DEBUG(Unexpected reached EOF);
					exit(0);	
				}
				continue;
			}
			if(n1 == NULL )
			{
				DEBUF("Bad Tree selected:%s", filename1);
				continue;
			}


			sscanf(line, "%d", &score);
			if(fgets(line, 128, fp) == NULL)
			{
				DEBUG(Unexpected reached EOF);
				exit(0);	
			}

			sscanf(line, "%*s vs. ref: %s", filename2);

			n2 = search_pool(filename2);
			// Two nodes
			if(n2 == NULL)	
			{
			// The second node is bad
				DEBUG(Second node is bad);
				continue;
			}
			// eval nodes
			int avg = (n1->e_dis + n2->e_dis)/2;
			int bias = n1->e_dis > n2->e_dis ? n2->e_dis/10: n1->e_dis/10;
			if(score < smaller(n1->e_dis, n2->e_dis) - bias)
			{

				char percent[8];
				float percent_i = 1 - ((float)score)/((float)smaller(n1->e_dis, n2->e_dis)-(float)bias);
				sprintf(percent, "%f\%", percent_i);
				// GUILTY
				EMIT(filename1, filename2, percent, scope);		
			}
		//score > bigger(n1->e_dis, n2->e_dis) + bias
		}
	}


}

void add2pool(void)
{
	char line[128];
	int score;
	node *n;

	while(1)
	{
		n = (node *)calloc(1, sizeof(node));

		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Error fgets);
			exit(0);	
		}
		if(line[0] < '0' || line[0] > '9')
			return ;
		// return if this is the end
		sscanf(line, "%d", &score);
		n->e_dis = score;

		if(fgets(line, 128, fp) == NULL)
		{
			DEBUG(Error fgets);
			exit(0);	
		}
		sscanf(line, "Empty Distance: %s ", n->filename);

		n->id = n_inpool;
		*(pool+n_inpool) = n;
		n_inpool++;
	}
}

void open_file(char **argv)
{
	if((fp = fopen(argv[1], "r")) == NULL)
	{
		DEBUF("filedir: %s", argv[1]);
		DEBUG("Usage: ./scorer <file dir> <scope>");
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
