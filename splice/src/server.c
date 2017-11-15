#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAXEVENTS 64

unsigned long mtime_since(struct timeval *s, struct timeval *e) {
    long sec, usec, ret;

    sec = e->tv_sec - s->tv_sec;
    usec = e->tv_usec - s->tv_usec;
    if (sec > 0 && usec < 0) {
        sec--;
        usec += 1000000;
    }

    sec *= 1000UL;
    usec /= 1000UL;
    ret = sec + usec;

    /*
     * time warp bug on some kernels?
     */
    if (ret < 0)
        ret = 0;

    return ret;
}

unsigned long mtime_since_now(struct timeval *s) {
    struct timeval t;

    gettimeofday(&t, NULL);
    return mtime_since(s, &t);
}

void connect_destination(int *sockfd, char *host, int port) {
    struct hostent *he;
    struct sockaddr_in their_addr; /* connector's address information */

    if ((he=gethostbyname(host)) == NULL) {  /* get the host info */
        herror("gethostbyname");
        exit(1);
    }

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    their_addr.sin_family = AF_INET;      /* host byte order */
    their_addr.sin_port = htons(port);    /* short, network byte order */
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    bzero(&(their_addr.sin_zero), 8);     /* zero the rest of the struct */

    if (connect(*sockfd, (struct sockaddr *)&their_addr, \
                sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }
}

static int normal_copy(int in_fd, int out_fd, int buff_size) {
    int done = 0;
    ssize_t len_r=-1, len_w=-1;

    printf("NORMAL Copy\n");

    while (len_r != 0) {
        char buf[buff_size];
        errno = 0;

        len_r = read (in_fd, buf, sizeof buf);
        if (len_r == -1) {
            /* If errno == EAGAIN, that means we have read all
               data. So go back to the main loop. */
            if (errno == EAGAIN) {
                continue;
            }
            else {
                done = 1;
                break;
            }
        }
        else if (len_r == 0) {
            /* End of file. The remote has closed the
               connection. */
            break;
        }

        while(len_r > 0) {

            errno = 0;

            len_w = write (out_fd, buf, len_r);
            if (errno == EAGAIN) {
                continue;
            }
            if (len_w == -1) {
                done = 1;
                break;
            }

            len_r -= len_w;
        }

        len_r = -1;
    }

    return done;
}

static int spliced_copy(int in_fd, int out_fd, int pipe_size) {
    loff_t in_off = 0;
    loff_t out_off = 0;
    int filedes[2];
    int err = -1;
    int len, len_r, len_w;

    printf("SPLICE Copy\n");

    if(pipe(filedes) < 0) {
        perror("pipe:");
        goto out;
    }

    fcntl(filedes[1],F_SETPIPE_SZ,&pipe_size);
    len_r = -1;
    while(len_r !=0) {
        /*
         * move to pipe buffer.
         */
        errno = 0;
        len_r = splice(in_fd, NULL, filedes[1], NULL, 65536, SPLICE_F_MOVE | SPLICE_F_NONBLOCK );

        if(errno == EAGAIN) {
            continue;
        }
        if(len_r < 0) {
            perror("splice1:");
            goto out_close;
        }
        /*
         * move from pipe buffer to out_fd
         */
        if(len_r == 0) {
            break;
            goto out_close;
        }
        len = len_r;
        while(len_r > 0) {
            errno = 0;
            len_w = splice(filedes[0], NULL, out_fd, NULL, len_r, SPLICE_F_MOVE | SPLICE_F_MORE);
            if(errno == EAGAIN) {
                perror("splice write EAGAIN:");
                continue;
            }
            if(len_w < 0) {
                perror("splice2:");
                goto out_close;
            }
            len_r -= len_w;
        }
        len_r = -1;
    }
    err = 0;

out_close:
    close(filedes[0]);
    close(filedes[1]);
    close(in_fd);
    close(out_fd);

out:
    return err;
}

static int make_socket_non_blocking (int sfd) {
    int flags, s;

    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1) {
        perror ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1) {
        perror ("fcntl");
        return -1;
    }

    return 0;
}

static int create_and_bind (char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo (NULL, port, &hints, &result);
    if (s != 0) {
        fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            /* We managed to bind successfully! */
            break;
        }

        close (sfd);
    }

    if (rp == NULL) {
        fprintf (stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo (result);

    return sfd;
}

int main (int argc, char *argv[]) {
    int sfd, s;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
    int dest_fd;
    int err, splice = 1;
    int pipe_size = 65536;

    if (argc < 4) {
        fprintf (stderr, "Usage: %s [server port] [destination host] [destination port] [(s)pliced copy (default) / (n)ormal copy] [buffer size in bytes, (default 65536)]\n", argv[0]);
        exit (EXIT_FAILURE);
    }
    if ( argc >= 5 && *argv[4] == 's') {
        splice = 1;
    }
    else if (argc >=5 && *argv[4] == 'n') {
        splice = 0;
    }
    if ( argc == 6 ) {
        pipe_size = strtol(argv[5],NULL,10);
    }

    sfd = create_and_bind (argv[1]);
    if (sfd == -1)
        abort ();

    s = make_socket_non_blocking (sfd);
    if (s == -1)
        abort ();

    s = listen (sfd, SOMAXCONN);
    if (s == -1) {
        perror ("listen");
        abort ();
    }

    efd = epoll_create1 (0);
    if (efd == -1) {
        perror ("epoll_create");
        abort ();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl (efd, EPOLL_CTL_ADD, sfd, &event);
    if (s == -1) {
        perror ("epoll_ctl");
        abort ();
    }

    /* Buffer where events are returned */
    events = calloc (MAXEVENTS, sizeof event);

    /* The event loop */
    while (1) {
        int n, i;

        n = epoll_wait (efd, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading (why were we notified then?) */
                fprintf (stderr, "epoll error\n");
                close (events[i].data.fd);
                continue;
            }

            else if (sfd == events[i].data.fd) {
                /* We have a notification on the listening socket, which
                   means one or more incoming connections. */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept (sfd, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                                (errno == EWOULDBLOCK))
                        {
                            /* We have processed all incoming
                               connections. */
                            break;
                        }
                        else
                        {
                            perror ("accept");
                            break;
                        }
                    }

                    s = getnameinfo (&in_addr, in_len,
                            hbuf, sizeof hbuf,
                            sbuf, sizeof sbuf,
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0) {
                        printf("--------\nAccepted connection on descriptor %d "
                                "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = make_socket_non_blocking (infd);
                    if (s == -1)
                        abort ();

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                        perror ("epoll_ctl");
                        abort ();
                    }
                }
                continue;
            }
            else {
                /* We have data on the fd waiting to be read. Read and
                   display it. We must read whatever data is available
                   completely, as we are running in edge-triggered mode
                   and won't get a notification again for the same
                   data. */
                err = 0;
                connect_destination(&dest_fd, argv[2], strtol(argv[3],NULL,10));

                struct rusage ru_s, ru_e;
                unsigned long ut, st, rt;
                struct timeval start, end;

                gettimeofday(&start, NULL);
                getrusage(RUSAGE_SELF, &ru_s);

                if (splice) {
                    err = spliced_copy(events[i].data.fd, dest_fd,pipe_size);
                }
                else {
                    err = normal_copy(events[i].data.fd, dest_fd,pipe_size);
                }

                gettimeofday(&end, NULL);
                getrusage(RUSAGE_SELF, &ru_e);

                ut = mtime_since(&ru_s.ru_utime, &ru_e.ru_utime);
                st = mtime_since(&ru_s.ru_stime, &ru_e.ru_stime);
                rt = mtime_since_now(&start);

                int elapsed = ((end.tv_sec - start.tv_sec) * 1000000) + (end.tv_usec - start.tv_usec);

                printf("total time Taken to copy : %d micro seconds\n",elapsed);
                printf("usr=%lu, sys=%lu, real=%lu\n", ut, st, rt);

                if (!err) {
                    printf ("Closed connection on descriptor %d and %d\n",
                            events[i].data.fd, dest_fd);

                    /* Closing the descriptor will make epoll remove it
                       from the set of descriptors which are monitored. */

                    close (events[i].data.fd);
                    close(dest_fd);
                }
                else {
                    perror("error in splicing/copying data");
                    close (events[i].data.fd);
                    close(dest_fd);
                    free(events);
                    close(sfd);
                    return EXIT_FAILURE;
                }
            }
        }
    }
    free (events);
    close (sfd);

    return EXIT_SUCCESS;
}
