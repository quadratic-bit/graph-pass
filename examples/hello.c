#include <stdio.h>

void func(int x) {
	if (x) printf("foo!\n");
	else   printf("bar!\n");
}

int main(void) {
	func(1);
	return 0;
}
