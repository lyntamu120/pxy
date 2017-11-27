#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "structures.h"

#define BACKLOG 10 // how many pending connections queue will hold

struct Pages cache[MAXNUMOFCACHE];
int numOfFile = 0;

int main(int argc, char *argv[]) {
    int sockfd, new_fd, maxDescriptors, selectVal; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p, *res;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    void *addr;
    char buf[2056], doc[512], host[512];
    int byte_count;
    int i;
    char *filename;
    int state_code;

    fd_set clientDescriptors;
    fd_set allDescriptors;

    if (argc < 3) {
        fprintf(stderr,"usage: ./proxy hostname port\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        addr = &(ipv4->sin_addr);
        // convert the IP to a string and print it:
        inet_ntop(p->ai_family, addr, s, sizeof s);
        break;
    }
    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    printf("server: waiting for connections on %s...\n", s);

    //clear the sets
    FD_ZERO(&clientDescriptors);
    FD_ZERO(&allDescriptors);

    FD_SET(sockfd, &allDescriptors);
    maxDescriptors = sockfd;

    while(1) {
        clientDescriptors = allDescriptors;
        //If select succeeds, it returns the number of ready socket descriptors.
        selectVal = select(maxDescriptors + 1, &clientDescriptors, NULL, NULL, NULL);
        if(selectVal == -1) {
            printf("ERROR: Select failed for input descriptors.\n");
            return -1;
        }

        for(i = 0; i <= maxDescriptors; i++) {
            //Check which descriptor is in the set.
            if(FD_ISSET(i, &clientDescriptors)) {
                if(i == sockfd) {
                    // client connecting
                    sin_size = sizeof their_addr;
                    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
                    if(new_fd == -1) {
                        printf("ERROR: Fail to connect to the client.\n");
                    } else {
                        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
                        printf("server: got connection from %s\n", s);

                        FD_SET(new_fd, &allDescriptors);
                        if(new_fd >= maxDescriptors) {
                            maxDescriptors = new_fd;
                        }
                    }
                } else {
                    //client sending GET request
                    if (recv(i, buf, sizeof buf, 0) == -1) {
                        perror("send");
                    }

                    filename = generateFileName(&buf[0]);
                    parseHostAndDoc(&buf[0], &doc[0], &host[0]);
                    printf("Path: %s\n", doc);
                    printf("Host: %s\n", host);

                    printf("File name that going to write: %s\n", filename);

                    int docInCache = findInCache(filename);

                    if (docInCache != -1 && checkStale(docInCache) == 0) {
                        // cache contains the doc and it's fresh
                        printf("%s\n", "Cache contains the file, send it to the cient!");
                        update(docInCache);
                    } else if (docInCache == -1) {
                        //cache doesn't contain the file
                        cacheHTTPRequest(filename, &host[0], &doc[0]);
                    } else {
                        // cache contains the doc but it's stale
                        printf("%s\n", "The file in the cache maybe stale, send a conditional GET");
                        state_code = handleStale(docInCache, filename, &host[0], &doc[0]);
                        if (state_code == 200) {
                            //denotes the file is stale
                            deleteInCache(docInCache);
                        } else {
                            update(docInCache);
                        }

                    }

                    sendFileToClient(i, filename);
                    FD_CLR(i, &allDescriptors);
                    close(i);
                    printf("After the transimission, current size of the cache is: %d\n", numOfFile);
                }
            }
        }
    }
    return 0;
}
