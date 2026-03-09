#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define USAGE "Usage: ./fact <number>\n"

uint64_t fact(uint64_t arg) {
	uint64_t res = 0;
	if (arg < 2) {
		res = 1;
	} else {
		uint64_t next = fact(arg - 1);
		res = arg * next;
	}
	return res;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf(USAGE);
		return 1;
	}
	uint64_t arg = atoi(argv[1]);
	if (errno == 0) {
		printf("Fact(%lu) = %lu\n", arg, fact(arg));
	} else {
		printf(USAGE);
		return 1;
	}
	return 0;
}
