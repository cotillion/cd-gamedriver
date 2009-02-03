#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

char *
get_auth_name(unsigned long remote, int local_port, int remote_port)
{
    struct sockaddr_in sa;
    int s, flags;
    char buf[1024];
    static char user_name[1024];
    int buflen;
    int i, n;
    struct timeval timeout;
    fd_set fs;

    if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
	return "";
    }
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(113);
    sa.sin_addr.s_addr = remote;
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
	(void)close(s);
	return "";
    }
    (void)sprintf(buf, "%u , %u\r\n", remote_port, local_port);
    buflen = strlen(buf);
    flags = fcntl(s, F_GETFL);
    (void)fcntl(s, F_SETFL, flags | O_NDELAY);

    FD_ZERO(&fs);
    FD_SET(s, &fs);
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (select(s + 1, NULL, &fs, NULL, &timeout) == 0 ||
	!FD_ISSET(s, &fs)) {
	(void)close(s);
	return "";
    }
    if (write(s, buf, buflen) != buflen) {
	(void)close(s);
	return "";
    }

    FD_ZERO(&fs);
    FD_SET(s, &fs);
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    if (select(s + 1, &fs, NULL, NULL, &timeout) == 0 ||
	!FD_ISSET(s, &fs)) {
	(void)close(s);
	return "";
    }
    
    for (n = i = 0; i < sizeof buf - 1; n++) {
	FD_ZERO(&fs);
	FD_SET(s, &fs);
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	if (select(s + 1, &fs, NULL, NULL, &timeout) == 0 ||
	    !FD_ISSET(s, &fs))
	    break;
	if (read(s, buf + i, 1) != 1)
	    break;
	if (buf[i] == '\n')
	    break;
	if (/*buf[i] != ' ' &&*/ buf[i] != '\t' && buf[i] != '\r')
	    i++;
	if (n > 2000) {
	    /* we've read far too much, just give up. */
	    (void)close(s);
	    return "";
	}
    }
    buf[i] = 0;
    (void)close(s);
    
    if(sscanf(buf, "%*u , %*u : USERID :%*[^:]:%s", user_name) != 1)
	return "";
    else
	return user_name;
}

/* ARGSUSED */
int
main(int argc, char *argv[])
{
    char buf[0x100];
    char *port1;
    char *port2;
    unsigned long addr;
    struct hostent *hp;
    char *name;

#ifndef SOLARIS
    (void)setlinebuf(stdout);
#endif
    (void)printf("\n");
    for(;;) {
	if (fgets(buf, sizeof(buf), stdin) == NULL)
	    break;
	if ((name = strchr(buf, '\n')) != NULL)
	    *name = '\0';
	port1 = strchr(buf, ';');
	if (port1) {
	    *port1 = 0;
	    port1++;
	    port2 = strchr(port1, ',');
	    if (port2) {
		*port2 = 0;
		port2++;
	    }
	}
	else
	    port2 = 0;
	
	addr = inet_addr(buf);
	if (addr != (unsigned long)-1) {
	    if (port2)
		name = get_auth_name(addr, atoi(port1), atoi(port2));
	    else
		name = "";
	    hp = gethostbyaddr((char *)&addr, 4, AF_INET);
	    if (!hp) {
		(void)sleep(5);
	        hp = gethostbyaddr((char *)&addr, 4, AF_INET);
	    }
	    if (hp) {
		if (port2)
		    (void)printf("%s %s,%s,%s:%s\n", buf, hp->h_name,
				 port1,port2,name);
		else
		    (void)printf("%s %s\n", buf, hp->h_name);
	    }
	    else if (port2)
		(void)printf("%s %s,%s,%s:%s\n", buf, buf, port1, port2, name);
	    
	}
    }
    return 0;
}
