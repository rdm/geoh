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

pid_t pid;
time_t tzero, tnow;
double now, then, atime;
int active;
long ngood, nempty, nbad, tgood, tempty, tbad;
char badreq[4100];

double gettime() {
	struct timeval tv;
	if (-1==gettimeofday(&tv, NULL)) perror("gettimeofday");
	tnow= tv.tv_sec;
	return (tv.tv_sec-tzero)+0.000001*tv.tv_usec;
}

#define LENGTHOF(x) ((sizeof x)-1)
#define str(x) {x, LENGTHOF(x)}
struct string {
	char *text;
	int len;
};

struct string bad= str("HTTP/1.0 403 Forbidden\r\n\
Content-Type: text/html\r\n\
Connection: close\r\n\
\r\n\
<?xml verion=\"1.0\" encoding=\"iso-8859-1\"?>\r\n\
<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\r\n\
         \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\r\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\r\n\
        <head>\r\n\
                <title>403 - Forbidden</title>\r\n\
        </head>\r\n\
        <body>\r\n\
                <h1>403 - Forbidden</h1>\r\n\
        </body>\r\n\
</html>\r\n");

struct string goodhead= str("HTTP/1.1 200 OK\r\n\
Content-Type: application/json;charset=utf-8\r\n\
Date: " IPMAPPREPDATE "\r\n\
Content-Length: ");

struct string goodcont= str("\r\nConnection: ");

struct string refdata[]= {
#include "refdata.h"
};

struct {
	struct string part1;
	struct string part2;
} localdata[]= {
#include "localdata.h"
};

int checkconnectiontype(int ndx, char buf[4096]) {
	workfds[ndx].expire= then;
	workfds[ndx].pipe= strcasestr(buf, "Connection: keep-alive") ?1 :0;
	return strstr(workfds[ndx].buf, key) ?1 :0;
}

char *appendtxt(char *dst, char *src, int len) {
	memcpy(dst, src, len);
	return dst+len;
}

#define PLENTY (8*sizeof (int))
char *appendint(char *buf, unsigned int n) {
	char scratch[PLENTY]; /* more than enough space */
	char *p, *q= scratch+PLENTY-1;
	int len;
	p= q;
	do {
		*p--= '0'+n%10;
		n/= 10;
	} while (n);
	len= q-p;
	return appendtxt(buf, ++p, len);
}

#define STRINGIFY(x) #x

int startwriting(int ndx, char *response, int responselen, char *callback, int callbacklen) {
	workfds[ndx].curstate= WRITING;
	workfds[ndx].roff= 0;
	pollfds[ndx].events= POLLOUT;
	if (response && responselen<3072) {
		char *connection;
		int connectlen;
		int bodylen= responselen+(callback ?callbacklen+LENGTHOF("();\n") :LENGTHOF("\n"));
		if (workfds[ndx].pipe) {
			connection= "Keep-Alive\nKeep-Alive: timeout=" STRINGIFY(TIMEOUT);
			connectlen= LENGTHOF("Keep-Alive\nKeep-Alive: timeout=" STRINGIFY(TIMEOUT));
		} else {
			connection= "Close";
			connectlen= LENGTHOF("Close");
		}
		char *start= workfds[ndx].buf;
		char *buf= start;
		buf= appendtxt(buf, goodhead.text, goodhead.len);
		buf= appendint(buf, bodylen);
		buf= appendtxt(buf, goodcont.text, goodcont.len);
		buf= appendtxt(buf, connection, connectlen);
		*buf++= '\r';
		*buf++= '\n';
		*buf++= '\r';
		*buf++= '\n';
		if (callback) {
			buf= appendtxt(buf, callback, callbacklen);
			*buf++= '(';
			buf= appendtxt(buf, response, responselen);
			*buf++= ')';
			*buf++= ';';
			*buf++= '\n';
		} else {
			buf= appendtxt(buf, response, responselen);
			*buf++= '\n';
		}
		workfds[ndx].isok= GOOD;
		workfds[ndx].resp= start;
		workfds[ndx].rlen= buf-start;
		return 1;
	}
	workfds[ndx].isok= BAD;
	workfds[ndx].resp= bad.text;
	workfds[ndx].rlen= bad.len;
	workfds[ndx].pipe= 0;
	return 0;
}

int donotdothat(int ndx) {
	if (!*badreq) {
		memcpy(badreq, workfds[ndx].buf, workfds[ndx].len);
		badreq[workfds[ndx].len]= 0;
	}
	return startwriting(ndx, NULL, 0, NULL, 0);
}

short *ipmap;

int iswordforming[256];

int initwordforming() {
	int j;
	for (j= 0; j<256; j++) {
		if (isalnum(j) || '_' == j || '$' == j) {
			iswordforming[j]= 1;
		}
	}
	return 0;
}

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

int lookuplocal(int ndx, char*callback, int callbacklen) {
	char body[2048];
	char *buf= body;
	char *forwarded= strcasestr(workfds[ndx].buf, "X-Forwarded-For: ");
	unsigned long addr;
	if (forwarded) {
		forwarded+= LENGTHOF("X-Forwarded-For: ");
		addr= txt2rawip(forwarded);
		if (-1==addr) forwarded=NULL;
	}
	if (!forwarded) {
		addr= ntohl(((struct sockaddr_in *)&(workfds[ndx].addr))->sin_addr.s_addr);
	}
	short loc= ipmap[addr];
	buf= appendtxt(buf, localdata[loc].part1.text, localdata[loc].part1.len);
	buf= appendint(buf, addr>>24);
	*buf++= '.';
	buf= appendint(buf, 0xff&(addr>>16));
	*buf++= '.';
	buf= appendint(buf, 0xff&(addr>>8));
	*buf++= '.';
	buf= appendint(buf, 0xff&addr);
	buf= appendtxt(buf, localdata[loc].part2.text, localdata[loc].part2.len);
	return startwriting(ndx, body, buf-body, callback, callbacklen);
}

int lookupref(int ndx, char *parse, char*callback, int callbacklen) {
	long rawip= txt2rawip(parse);
	if (-1==rawip) return donotdothat(ndx);
	short loc= ipmap[rawip];
	return startwriting(ndx, refdata[loc].text, refdata[loc].len, callback, callbacklen);
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
				socklen_t addrlen= sizeof (struct sockaddr_storage);
				int fd= accept(pollfds[ndx].fd, (struct sockaddr*)&(workfds[k].addr), &addrlen);
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
					if (!buf[j]) return donotdothat(ndx);
				}
				if (2==nlcount) {
					buf[end]= 0;
					if (!checkconnectiontype(ndx, buf)) {
						donotdothat(ndx);
					} else {
						char *parse= strstr(buf, "ip=");
						if (!parse) {
							donotdothat(ndx);
						} else {
							char cbuf[256];
							char *buf= cbuf;
							char *cb= strstr(workfds[ndx].buf, "callback=");
							int clen;
							if (cb) {
								int j;
								for (j= 255, cb+=LENGTHOF("callback="); j&&iswordforming[*cb]; j--) {
									*buf++= *cb++;
								}
								clen= buf-cbuf;
								buf= cbuf;
							} else {
								clen= 0;
								buf= NULL;
							}
							parse+=LENGTHOF("ip=");
							if ('l'==*parse) {
								lookuplocal(ndx, buf, clen);
							} else {
								lookupref(ndx, parse, buf, clen);
							}
						}
					}
					return WRITING;
				}
			}
			if (4096==end) {
				donotdothat(ndx);
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
			printf("%.24s: pid: %d -- NEW good: %ld, empty: %ld, bad: %ld, PENDING %d/%d, TOTAL good: %ld, empty: %ld, bad: %ld\n", when, pid, ngood, nempty, nbad, pending, newlim, tgood, tempty, tbad);
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
			testkey[j]= choice[random()%LENGTHOF(choice)];
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
	initwordforming();
	pid= getpid();
        printf("process %d listening on port %d\n", pid, ntohs(listenaddr_in.sin_port));
	fflush(stdout);
	tzero= (long)gettime();
	serve(s);
	exit(0);
}


