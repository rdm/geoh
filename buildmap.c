#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

int die(char *msg, int status) {
	perror(msg);
	fflush(stderr);
	exit(status);
}

int extract(char *buf, long *addr, short *ndx) {
	if ('"' == *buf) buf++;
	if (!isdigit(*buf)) die("expected number for ip address", 4);
	long ip= 0;
	while (isdigit(*buf)) {
		ip*=10;
		ip+=(*buf++)-'0';
	}
	*addr= ip;
	if ('"' == *buf) buf++;
	if (!',' == *buf++) die("expected comma after ip address", 5);
	if ('"' == *buf) buf++;
	if (!isdigit(*buf)) die("expected number for location index", 6);
	short nub= 0;
	while (isdigit(*buf)) {
		nub*= 10;
		nub+=(*buf++)-'0';
	}
	return *ndx= nub;
}

int main(){
	FILE*ipnub= fopen("ip-nub.csv","r");
	if (!ipnub) die("fopen ip-nub.csv",1);
	FILE*ipmap= fopen("ip.map.new","w");
	if (!ipmap) die("fopen ip.map.new",2);
	long ip, lastip;
	short nub;
	char buf[256];
	for (ip= 0, lastip=-1; ip<4294967296; ip++) {
		if (ip > lastip) {
			if (!fgets(buf, 256, ipnub)) die("fgets line of ip-nub.csv", 3);
			extract(buf, &lastip, &nub);
		}
		if (ip == 758512576 || ip == 1815626509) {
			printf("ip: %ld, %d (%d, %d)\n", ip, nub, 255&nub, nub>>8);
		}
		/* little-endian */
		if (EOF==putc(255&nub, ipmap)) die("putc to ip.map.new", 7);
		if (EOF==putc(nub>>8, ipmap)) die("putc to ip.map.new", 8);
	}
	exit(0);
}
