#include <arpa/nameser.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <resolv.h>

int die(char *msg, int status) {
	perror(msg);
	fflush(stderr);
	exit(status);
}

enum state { LISTENING, READING, WRITING };

struct workfd {
	enum state curstate;
	struct sockaddr_storage addr;
	int len;
	char buf[4096];
	char *resp;
	int roff;
	int rlen;
};

struct pollfd* pollfds;
struct workfd* workfds;

#define str(x) {x, sizeof x}
struct string {
	char *text;
	int len;
};

struct string bad[]= {
str("HTTP/1.0 400 Bad Request\n\
Content-Type: text/html\n\
Connection: close\n\
\n\
<?xml verion=\"1.0\" encoding=\"iso-8859-1\"?>\n\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n\
         \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
        <head>\n\
                <title>400 - Bad Request</title>\n\
        </head>\n\
        <body>\n\
                <h1>400 - Bad Request</h1>\n\
        </body>\n\
</html>\n")
};
struct string good[]= {
#include "good.h"
};

int setcontent(int ndx, int rndx) {
	if (0>rndx) {
		workfds[ndx].resp= bad[1+rndx].text;
		workfds[ndx].rlen= bad[1+rndx].len;
	} else {
		workfds[ndx].resp= good[rndx].text;
		workfds[ndx].rlen= good[rndx].len;
	}
	pollfds[ndx].events= POLLOUT;
	workfds[ndx].roff= 0;
	workfds[ndx].curstate= WRITING;
	return 0;
}

short *ipmap;

int lookup(char *buf) {
	char *parse= strstr(buf, "ip=");
	if (!parse) return -1;
	parse+=3;
	long byte0= strtol(parse, &parse, 10);
	if (byte0<0||byte0>255||'.'!=*parse++) return -1;
	long byte1= strtol(parse, &parse, 10);
	if (byte1<0||byte1>255||'.'!=*parse++) return -1;
	long byte2= strtol(parse, &parse, 10);
	if (byte2<0||byte2>255||'.'!=*parse++) return -1;
	long byte3= strtol(parse, &parse, 10);
	if (byte3<0||byte3>255) return -1;
	return ipmap[256*(256*(256*byte0+byte1)+byte2)+byte3];
}

int handlefd(int ndx, int*n, int max){
	char *buf;
	int j, k, end, len, off, siz, sta;
	switch (workfds[ndx].curstate) {
		case LISTENING:
			siz= *n;
			for (j= 1, k= 0; j<siz; j++) {
				if (-1==pollfds[j].fd) {
					k= j;
					break;
				}
			}
			if (!k) k= siz;
			if (k < max) {
				socklen_t ignore;
				int fd= accept(pollfds[ndx].fd, (struct sockaddr*)&(workfds[k].addr), &ignore);
				if (-1==fd) {
					perror("accept");
					fflush(stderr);
				} else {
					pollfds[k].fd= fd;
					pollfds[k].events= POLLIN;
					pollfds[k].revents= 0;
					workfds[k].curstate= READING;
					workfds[k].len= 0;
					if (k>=*n) *n=k+1;
				}
			}
			return LISTENING;
		case READING:
			len= workfds[ndx].len;
			/* buf can hold 4096 characters, but will get null terminator */
			siz= read(pollfds[ndx].fd, workfds[ndx].buf+len, 4095-len);
			if (1>siz) {
				if (-1==siz) perror("read"); /* FIXME: decorate with more detail? */
				close(pollfds[ndx].fd);
				pollfds[ndx].fd= -1;
				pollfds[ndx].revents= 0;
				return READING;
			}
			end= workfds[ndx].len+= siz;
			off= len-(0==len ?0 :1); /* want len-(0==len) but can't find standards language guaranteeing from == is in {1,0} */
			buf= workfds[ndx].buf;
			for (j= off, sta= 0; j<end; j++) {
				if ('\n' == buf[j]) {
					sta++;
				} else {
					if ('\r'!=buf[j]) sta= 0;
				}
				if (2==sta) {
					buf[end]= 0;
					setcontent(ndx, lookup(buf));
					return READING;
				}
			}
			if (4096==end) {
				setcontent(ndx, -1);
			}
			return READING;
		case WRITING:
			off= workfds[ndx].roff;
			siz= write(pollfds[ndx].fd, workfds[ndx].resp+off, workfds[ndx].rlen-off);
			if (1>siz || siz+off==workfds[ndx].rlen) {
				if (-1==siz) perror("write"); /* FIXME: decorate with more detail? */
				close(pollfds[ndx].fd);
				pollfds[ndx].fd= -1;
				pollfds[ndx].revents= 0;
				return WRITING;
			}
			workfds[ndx].roff+= siz;
			return WRITING;
	}
}

int serve(int listenfd){
	int maxfds= 1024;
	int curfds= 1;
	int newlim;
	pollfds= calloc(maxfds, sizeof (struct pollfd));
	workfds= calloc(maxfds, sizeof (struct workfd));
	pollfds[0].fd= listenfd;
	pollfds[0].events= POLLIN;
	while (1) {
		if (-1==poll(pollfds, curfds, 60*60*1000)) die("poll", 7);
		int j;
		for (j= newlim= 0; j<curfds; j++) {
			if (pollfds[j].revents) {
				handlefd(j, &curfds, maxfds);
			}
			if (-1<pollfds[j].fd) newlim= j;
		}
		curfds= newlim+1;
	}
}

struct sockaddr_in listenaddr_in= {
	.sin_family= AF_INET,
	.sin_port= 1799 /* htons not needed */
};
void* listenaddr= &listenaddr_in;

int main(){
	int mapf= open("ip.map", O_RDONLY);
	if (-1 == mapf) die("open", 1);
	ipmap= mmap(NULL, 8589934592, PROT_READ, MAP_PRIVATE, mapf, 0);
	if (MAP_FAILED == ipmap) die("mmap", 2);
	int s= socket(AF_INET, SOCK_STREAM, 6/*tcp*/);
	if (-1==s) die("socket", 3);
	int optval= 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof optval)) die("setsockopt", 4);
	if (-1==bind(s, listenaddr, sizeof listenaddr_in)) die("bind", 5);
	if (-1==listen(s, 128)) die("listen", 6);
	serve(s);
	exit(0);
}


