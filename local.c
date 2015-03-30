#include <arpa/nameser.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <resolv.h>

#include "timeout.h"
#include "mapdate.h"

char * key;

int die(char *msg, int status) {
	perror(msg);
	fflush(stderr);
	exit(status);
}

#define LENGTHOF(x) ((sizeof x)-1)
#define str(x) {x, LENGTHOF(x)}
struct string {
	char *text;
	int len;
};

struct string refdata[]= {
#include "refdata.h"
};

#define STRINGIFY(x) #x

short *ipmap;

long txt2rawip(char *parse) {
        long byte0= strtol(parse, &parse, 10);
        if (byte0<0||byte0>255||'.'!=*parse++) return -1;
        long byte1= strtol(parse, &parse, 10);
        if (byte1<0||byte1>255||'.'!=*parse++) return -1;
        long byte2= strtol(parse, &parse, 10);
        if (byte2<0||byte2>255||'.'!=*parse++) return -1;
        long byte3= strtol(parse, &parse, 10);
        if (byte3<0||byte3>255) return -1;
        return 256*(256*(256*byte0+byte1)+byte2)+byte3;
}

int main(int c, char**v){
	int mapf= open("ip.map", O_RDONLY);
	if (-1 == mapf) die("open", 1);
	ipmap= mmap(NULL, 8589934592, PROT_READ, MAP_PRIVATE, mapf, 0);
	if (MAP_FAILED == ipmap) die("mmap", 2);
        int j;
        for (j= 1; j < c; j++) {
		long addr= txt2rawip(v[j]);
		if (0>addr) {
			printf("Invalid address: %s\n", v[j]);
		} else {
			short loc= ipmap[addr];
			printf("\n");
			printf("Address: %s (%ld)\n", v[j], addr);
			printf("Index: %d\n", loc);
			printf("refdata: %s\n", refdata[loc].text);
		}
	}
	exit(0);
}


