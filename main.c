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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <resolv.h>

#include "timeout.h"

char * key;

int die(char *msg, int status) {
	perror(msg);
	fflush(stderr);
	exit(status);
}

enum state { LISTENING, READING, WRITING };

struct workfd {
	enum state curstate;
	int len;
	char *resp;
	int roff;
	int rlen;
	enum {GOOD, EMPTY, NEUTRAL, BAD} isok;
	int pipe;
	double expire;
	struct sockaddr_storage addr;
	char buf[4096];
};

struct pollfd* pollfds;
struct workfd* workfds;

time_t tzero, tnow;
double now, then, atime;
int active;
long ngood, nempty, nbad, tgood, tempty, tbad;
char badreq[4096];

double gettime() {
	struct timeval tv;
	if (-1==gettimeofday(&tv, NULL)) perror("gettimeofday");
	tnow= tv.tv_sec;
	return (tv.tv_sec-tzero)+0.000001*tv.tv_usec;
}

#define str(x) {x, sizeof x}
struct string {
	char *text;
	int len;
};

struct string bad[]= {
str("HTTP/1.0 403 Forbidden\n\
Content-Type: text/html\n\
Connection: close\n\
\n\
<?xml verion=\"1.0\" encoding=\"iso-8859-1\"?>\n\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n\
         \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
        <head>\n\
                <title>403 - Forbidden</title>\n\
        </head>\n\
        <body>\n\
                <h1>403 - Forbidden</h1>\n\
        </body>\n\
</html>\n")
};
struct string good[][2]= {
#include "good.h"
};

int setpipe(int ndx, char buf[4096]) {
	if (!strcasestr(buf, "Connection: keep-alive")) return 0;
	workfds[ndx].pipe= 1;
	workfds[ndx].expire= then;
	return 1;
}

int setcontent(int ndx, int rndx) {
	if (!strstr(workfds[ndx].buf, key)) rndx=-1;
	if (0>rndx) {
		if (!*badreq) {
			strncpy(badreq, workfds[ndx].buf, workfds[ndx].len);
			badreq[workfds[ndx].len]= 0;
		}
		workfds[ndx].isok= BAD;
		workfds[ndx].resp= bad[1+rndx].text;
		workfds[ndx].rlen= bad[1+rndx].len;
		workfds[ndx].pipe= 0;
	} else {
		workfds[ndx].isok= GOOD;
		int pipe= workfds[ndx].pipe;
		workfds[ndx].resp= good[rndx][pipe].text;
		workfds[ndx].rlen= good[rndx][pipe].len;
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

int closefd(int ndx) {
	close(pollfds[ndx].fd);
	pollfds[ndx].fd= -1;
	pollfds[ndx].revents= 0;
	switch (workfds[ndx].isok) {
		case GOOD: ngood++, tgood++; break;
		case EMPTY: nempty++, tempty++; break;
		case NEUTRAL: break; /* pipeline between requests */
		case BAD: nbad++, tbad++; break;
	}
	return 0;
}

int handlefd(int ndx, int*n, int max) {
	char *buf;
	int j, k, end, len, nlcount, off, siz;
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
					workfds[k].isok= EMPTY;
					workfds[k].pipe= 0;
					workfds[k].expire= then;
					if (k>=*n) *n=k+1;
				}
			}
			return LISTENING;
		case READING:
			len= workfds[ndx].len;
			/* buf can hold 4096 characters, but will get null terminator */
			siz= read(pollfds[ndx].fd, workfds[ndx].buf+len, 4095-len);
			if (1>siz) {
				if (-1==siz && ECONNRESET != errno) perror("read"); /* connection reset happens too often to be interesting */
				closefd(ndx);
				return READING;
			}
			end= workfds[ndx].len+= siz;
			off= len-2; /* hypothetically speaking, \r\n\r could have already been read */
			if (0>off) off= 0;
			buf= workfds[ndx].buf;
			for (j= off, nlcount= 0; j<end; j++) { /* http requests terminated by blank line */
				if ('\n' == buf[j]) {
					nlcount++;
				} else {
					if ('\r'!=buf[j]) nlcount= 0;
				}
				if (2==nlcount) {
					buf[end]= 0;
					setpipe(ndx, buf);
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
				if (-1==siz) {
					perror("write"); /* FIXME: decorate with more detail? */
					workfds[ndx].isok= BAD;
					workfds[ndx].pipe= 0;
				}
				if (workfds[ndx].pipe) {
					switch (workfds[ndx].isok) {
						case GOOD: ngood++, tgood++; break;
						case EMPTY: nempty++, tempty++; break;
						case NEUTRAL: break;
						case BAD: nbad++, tbad++; break;
					}
					pollfds[ndx].events= POLLIN;
					pollfds[ndx].revents= 0;
					workfds[ndx].curstate= READING;
					workfds[ndx].len= 0;
					workfds[ndx].isok= NEUTRAL;
					workfds[ndx].expire= then;
				
				} else {
					closefd(ndx);
				}
				return WRITING;
			}
			workfds[ndx].roff+= siz;
			return WRITING;
	}
}

int serve(int listenfd) {
	int maxfds= 1024;
	int curfds= 1;
	int newlim;
	pollfds= calloc(maxfds, sizeof (struct pollfd));
	workfds= calloc(maxfds, sizeof (struct workfd));
	pollfds[0].fd= listenfd;
	pollfds[0].events= POLLIN;
	while (1) {
		if (-1==poll(pollfds, curfds, (1+TIMEOUT)*1000)) die("poll", 11);
		workfds[0].expire= then= TIMEOUT+(now= gettime());
		if (!active) atime= then;
		int j, pending;
		for (j= newlim= 0, pending= -1; j<curfds; j++) {
			if (pollfds[j].revents) {
				active=1;
				handlefd(j, &curfds, maxfds);
			}
			if (-1<pollfds[j].fd) {
				if (now > workfds[j].expire) {
					closefd(j);
				} else {
					pending++;
					newlim= j;
				}
			}
		}
		curfds= newlim+1;
		if (active && now > atime) {
			char when[]= "Clock is broken.............";
			ctime_r(&tnow, when);
			if (*badreq) fprintf(stderr, "%s\n", badreq);
			fflush(stderr);
			printf("%.24s: NEW good: %ld, empty: %ld, bad: %ld, PENDING %d/%d, TOTAL good: %ld, empty: %ld, bad: %ld\n", when, ngood, nempty, nbad, pending, newlim, tgood, tempty, tbad);
			fflush(stdout);
			badreq[0]= active= ngood= nempty= nbad= 0;
		}
	}
}

struct sockaddr_in listenaddr_in= {
	.sin_family= AF_INET,
	.sin_port= 1799 /* htons not needed */
};
void* listenaddr= &listenaddr_in;

int main(int c, char**v){
	if (2>c) {
		static char testkey[7];
		char choice[]= "'()*-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz{|}~";
		int j;
#ifndef __GNUC__
		/* GNU doesn't support this - let's hope they're implicitly including entropy */
		srandomdev();
#endif
		for (j= 0; j<6; j++) {
			testkey[j]= choice[random()%sizeof choice];
		}
		testkey[6]= 0;
		key= testkey;
	} else {
		key= v[1];
	}
	printf("Using key: %s\n", key);
	int mapf= open("ip.map", O_RDONLY);
	if (-1 == mapf) die("open", 1);
	ipmap= mmap(NULL, 8589934592, PROT_READ, MAP_PRIVATE, mapf, 0);
	if (MAP_FAILED == ipmap) die("mmap", 2);
	int s= socket(AF_INET, SOCK_STREAM, 6/*tcp*/);
	if (-1==s) die("socket", 3);
	int optval= 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof optval)) die("setsockopt", 4);
	if (-1==fcntl(s, F_SETFL, O_NONBLOCK)) die("fcntl", 5);
	setuid(0); /* this will succeed if we can become root */
	if (0==getuid()) {
                listenaddr_in.sin_port= htons(80);
        }
	if (-1==bind(s, listenaddr, sizeof listenaddr_in)) die("bind", 6);
	if (-1==listen(s, 128)) die("listen", 7);
        if (0==getuid()) {
                if (setgid(33/*www-data*/)) die("setgid", 8);
                if (setuid(33/*www-data*/)) die("setuid", 9);
                if (-1!=setuid(0)) die("regained root", 10);
        }
        printf("listening on port %d\n", ntohs(listenaddr_in.sin_port));
	fflush(stdout);
	tzero= (long)gettime();
	serve(s);
	exit(0);
}


