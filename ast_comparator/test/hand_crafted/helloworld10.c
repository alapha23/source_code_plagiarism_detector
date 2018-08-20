#include <stdlib.h>

int foo(void){return 2;}
int koo(void){return 1;}
int hoo(int a, int b, char *a){return 3+a;}

int main(void)
{
	int a;
	int b;
	char c = 'c';

	while (foo()>0)
	{
		if((a=hoo(koo(), 232, &c))> 0)
		{
			foo();
		}else if(foo())
		{
			koo();
		}else if(koo()+foo())
		{
			foo();
		}		
	}

	return 0;
}
