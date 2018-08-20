#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FILE *fp;

void open_file(int num, char **argv)
{
	if((fp =fopen(argv[num], "r")) == NULL)
	{
		fprintf(stderr, "Usage: ./rm_header <file dir>\n");	
		fflush(stderr);
		exit(0);	
	}
}

int check_(char *line)
{
//	printf("check: %c\n\n", *(line+9));
	if(*line == '#')
		if(*(line + 1) == 'i')
		if((*(line + 9) == '<') || ((*(line + 9) == '"') ))
			return 1;
	return 0;
	// we don't need include <>
}

void write_file(char *buffer, char *filename)
{
	char newname[256];
	char suffix[8];
//	printf("original file name:%s\n", filename);
	fflush(stdout);
	sscanf(filename, "%[^.].%s ", newname, suffix);
//	printf("suffix:%s\n", suffix);
	fflush(stdout);
	strcat(newname, ".rm_header.");
	strcat(newname, suffix);

//	printf("newfile:%s\n", newname);
	fflush(stdout);

	FILE *newfp = fopen(newname, "w");
	if(newfp == NULL)
	{
		fprintf(stderr, "write filename.rm_header.suffix error\n");	
		fflush(stderr);
		exit(0);	
	}

	fprintf(newfp, "%s", buffer);

	fclose(newfp);
}

void dump_file(char *name)
{
	// set file size
	fseek(fp, 0L, SEEK_END);
	size_t size =(size_t)ftell(fp);
	rewind(fp);

//	printf("File size=%d\n", size);

	char *buffer = (char *)calloc(1, size);
	char line[256];
	
	while(1)
	{
		if(fgets(line, size, fp) == NULL)
			break;
		if(check_(line))
		{
			continue;
		}
		strcat(buffer, line);
	}

	printf("%s",  buffer);
	fflush(stdout);

	write_file(buffer, name);

	if( fclose(fp) == EOF)
	{
		fprintf(stderr, "File close error:%s\n");	
		fflush(stderr);
		exit(0);	
	}

}

int main(int argc, char **argv)
{
	if(argc == 1)
	{
		fprintf(stderr, "Usage: ./rm_header <file dir>\n");	
		fflush(stderr);
		return 0;	
	}

	while(argc != 1)
	{
		argc--;
		open_file(argc, argv);
		dump_file(argv[argc]);
	}
	return 0;
}
