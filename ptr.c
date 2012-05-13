#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define TID 0

void init(int **i)
{
	*i = (int *)malloc(10 * sizeof(int));
	return;
}

int main()
{
#if 0
	int p = 1;
	int *ptr;
	ptr = &p;
	*ptr++;
	printf("%d\n", p);
	int chunk_sz = 5;
	int buf_sz = 7;
	int x = (((TID+1)*chunk_sz-1)>(buf_sz-1))?(buf_sz-1):
		((TID+1) * chunk_sz - 1);
	printf("%d\n", x);
	printf("%8d""%16.4f""\n", 100, 45.67);

#endif
	int *a = malloc(sizeof(int));
	*a = 1;
	int *p = a;
	free(a);
	printf("%d\n", *p);

	char *t = (char *)malloc(10*sizeof(char));
	char t1[10];
	printf("%lu\n", sizeof(t1));

	return 0;
}
