#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef  __APPLE__
#include <sys/uio.h>
#else
#include <sys/sendfile.h>
#endif

int main(int argc, char *argv[])
{
    int sockfd, read_fd, port, r=0;
    struct hostent *he; /* host information */
    struct sockaddr_in their_addr; /* connector's address information */

    off_t offset = 0; /* file offset for sendfile */
    struct stat stat_buf; /* file stat */
    size_t len;

    if (argc != 4) {
        fprintf(stderr,"Usage: %s [server address] [server port] [file]\n", argv[0]);
        exit(1);
    }

    if ((he=gethostbyname(argv[1])) == NULL) {  /* get the host info */
        herror("gethostbyname");
        exit(1);
    }

    port = strtol(argv[2],NULL,10); /* extract port */
    if (errno != 0) {
    	perror("port");
	    exit(1);
    }

    read_fd = open(argv[3], O_RDONLY); /* open file */
    if (read_fd == -1) {
    	perror("open");
	    exit(1);
    }

    r = fstat(read_fd, &stat_buf); /* get file details */
    if (r == -1) {
    	perror("fstat");
        close(read_fd);
	    exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) { /* create a socket */
        perror("socket");
        exit(1);
    }

    their_addr.sin_family = AF_INET;      /* host byte order */
    their_addr.sin_port = htons(port);    /* short, network byte order */
    their_addr.sin_addr = *((struct in_addr *)he->h_addr); /* host address */
    bzero(&(their_addr.sin_zero), 8);     /* zero the rest of the struct */

    if (connect(sockfd, (struct sockaddr *)&their_addr,    /* connect to host */
                                          sizeof(struct sockaddr)) == -1) {
        perror("connect");
        close(sockfd);
        exit(1);
    }

    len = stat_buf.st_size; /*get length of file */

    while (len>0) {

	    errno = 0;
#ifdef  __APPLE__
	    r = sendfile(read_fd, sockfd, &offset,0,NULL, 0);
#else
	    r = sendfile(sockfd, read_fd, &offset, stat_buf.st_size);
#endif
	    if (errno == EAGAIN) { /* try again */
		    continue;
		}

	    if(r == 0) { /* no more data to write, break */
		    break;
		}

		len -= r;

	    printf("Total bytes sent = %d\n",r);
    }

    close(read_fd); 
    close(sockfd);

    return 0;
}

