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
get_auth_name(char *addr, int local_port, int remote_port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    int s, flags;
    char buf[1024];
    static char user_name[1024];
    int buflen;
    int i, n;
    struct timeval timeout;
    fd_set fs;

    memset(&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    s = getaddrinfo(addr, "113", &hints, &result);
    if (s != 0)
    {
        fprintf (stderr, "getaddrinfo failed: %s\n", gai_strerror(s));
        return NULL;
    }


    for (rp = result; rp != NULL; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype,
                     rp->ai_protocol);
        if (s == -1)
            continue;

        if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(s);
    }

    freeaddrinfo(result);   

    if (rp == NULL)
        return "";

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

char *
reverse_lookup(char *ip)
{
    static char             buf[NI_MAXHOST];
    struct addrinfo *addrs;
    struct addrinfo  hints;
    int              ret; 

    memset(&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(ip, NULL, &hints, &addrs);
    if (ret != 0)
    {
        fprintf (stderr, "getaddrinfo failed: %s\n", gai_strerror (ret));
        return NULL;
    }

    ret = getnameinfo(addrs->ai_addr, addrs->ai_addrlen, buf, sizeof(buf), NULL, 0, NI_NAMEREQD);
    freeaddrinfo (addrs);
    if (ret != 0)
    {
        return NULL;
    }

    return buf;
}

/* ARGSUSED */
int
main(int argc, char *argv[])
{
    char buf[0x100];
    char *local_port;
    char *remote_port;
    char *addr;
    char *ptr;
    char *ident, *reverse;

#ifndef SOLARIS
    (void)setlinebuf(stdout);
#endif
    (void)printf("\n");

    while (1)
    {
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        if ((ptr = strrchr(buf, '\n')))
            *ptr = '\0';

        addr = strtok(buf, ",");
        local_port = strtok(NULL, ",");
        remote_port = strtok(NULL, ",");

        if (addr == NULL || local_port == NULL || remote_port == NULL)
        {
            fprintf(stderr, "Invalid input format: %s\n", buf);
            continue;
        }

        reverse = reverse_lookup(addr);
        if (reverse == NULL)
            reverse = addr;

        ident = get_auth_name(addr, atoi(local_port), atoi(remote_port));
        if (ident == NULL)
            ident = "";


        printf("%s,%s,%s,%s,%s\n", addr, local_port, remote_port, reverse, ident);

    }
    return 0;
}
