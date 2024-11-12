#include <stdio.h>
#include <stdlib.h>

// data uninitialized
int rizz;
// data initialized
char banger[] = "sigma";

int main(int argc, char *argv[]) {
	// pointer located stack
	float *skibiditoilet;
	// stack
	double yeet=1.1234;

	// points to heap
	skibiditoilet = (float *)malloc(sizeof(float)*5);

	// int i in stack
	for(int i = 0; i < 5; i++)
		skibiditoilet[i] = (float)i;
	return 0;
}