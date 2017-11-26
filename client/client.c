#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT "5000" // the port client will be connecting to
#define MAXDATASIZE 100 // max number of bytes we can get at once
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



int main(int argc, char *argv[]) {
    int sockfd, numbytes, maxDescriptors, selectVal, byte_count;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    char *message;
    char buf[512];
    FILE *fp;
    int i;
    char dateStr[20];
    char filename[200];

    fd_set clientDescriptors;
    fd_set allDescriptors;

    if (argc != 3) {
        fprintf(stderr,"usage: client hostname url\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        printf("The sockfd from client is: %d\n", sockfd);
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure

    message = argv[2];
    if ((numbytes = send(sockfd, message, strlen(message), 0)) == -1) {
        perror("send");
        exit(1);
    }

    time_t now = time(NULL);
    strftime(dateStr, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
    strcpy(filename, dateStr);
    strcat(filename, message);

    for (i = 0; filename[i] != '\0'; i++) {
        if (filename[i] == '/') {
            filename[i] = '_';
        }
    }
    printf("The file name: %s\n", filename);


    if ((fp = fopen(filename, "w+")) == NULL) {
       printf("File could not be found or opened; Aborting\n");
       return 1;
    }
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
            perror("Select");
        }

        if(FD_ISSET(sockfd, &clientDescriptors)) {
            byte_count = recv(sockfd, buf, sizeof buf, 0);
            if (byte_count == -1) {
                perror("recv");
                exit(1);
            }
            if (byte_count == 0) {
                printf("All data has been received\n");
                fclose(fp);
                break;
            }
            printf("http: recv()'d %d bytes of data in buf\n", byte_count);
            fwrite(buf, 1, byte_count, fp);
        }

    }

    close(sockfd);
    return 0;
}