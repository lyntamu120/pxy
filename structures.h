#include <stdio.h>
#include <time.h>

#define MAXNUMOFCACHE 10 // how many files will be cached
#define Expires "Expires"
#define Date "Date"
#define LastModified "Last-Modified"
#define DAYINSECOND 86400

struct Headers {
    int hasDate;
    int hasExpire;
    int hasLastModifiedTime;
    time_t date;
    time_t expire;
    time_t last_modified_time;
};

struct Pages {
    char *file_name;
    time_t last_used_time;
    struct Headers *header;
};

extern struct Pages cache[MAXNUMOFCACHE];
extern int numOfFile;

void sigchld_handler(int s) {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//generate a valid file name
char *generateFileName(char *message) {
    char *filename = malloc(strlen(message) + 1);
    int i;

    strcpy(filename, message);

    for (i = 0; filename[i] != '\0'; i++) {
        if (filename[i] == '/') {
            filename[i] = '_';
        }
    }
    return filename;
}

// check if the cache contains the doc
int findInCache(char* filename) {
    int i;
    for (i = 0; i < numOfFile; i++) {
        if (strcmp(cache[i].file_name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

// check the status of the existing cached file
// 0: good; 1: expired or modified recently
int checkStale(int index) {
    double s1, s2;
    struct Headers *header = cache[index].header;
    if (header -> hasExpire) {
        s1 = difftime(time(NULL), header -> expire);
        if (s1 > 0) {
            printf("%s\n", "Out of expire date: The file is stale!");
            return 1;
        }
    } else {
        //cache[i] has last_modified_time and doesn't have expire
        s1 = difftime(time(NULL), header -> date);
        s2 = difftime(time(NULL), header -> last_modified_time);
        if (s1 > DAYINSECOND || s2 < 30 * DAYINSECOND) {
            printf("%s\n", "Last modified recently: The file is stale!");
            return 1;
        }
    }
    return 0;
}

// update the last_used_time for current doc
void update(int docInCache) {
    cache[docInCache].last_used_time = time(NULL);
}

//return 0 means haven't found yet, return 1 means have found it
int obtainHeader(int *iter, char* buf, char* recvHeader, int rBytes) {
    const char *split = "\r\n\r\n";
    int hasFind = 0;
    if (strstr(buf, split) != NULL) { //this the last packet that contains header information
        hasFind = 1;
    }
    memcpy(recvHeader + (*iter), buf, rBytes);
    *iter = *iter + rBytes;
    return hasFind;
}

//transfer string format date info to time_t
time_t parseDate(char *timeStamp) {
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
    time_t epoch;
    if (strptime(timeStamp, "%a, %d %b %Y %H:%M:%S %Z", &tm) != NULL) {
        epoch = mktime(&tm);
        printf("The epoch is: %ld\n", epoch);
        return epoch;
    } else {
        printf("%s\n", "Date format isn't match");
        return 0;
    }
}

//parse the header and obtain the date info
struct Headers *parseHeader(char *recvHeader) {
    char *dateStr;
    struct Headers *header = malloc(sizeof (struct Headers));
    char *headerField, *headerFieldName;

    header->hasExpire = 0;
    header->hasDate = 0;
    header->hasLastModifiedTime = 0;

    headerField = strtok(recvHeader, "\n");
    while( headerField != NULL ) {
        printf("The pure header content is: %s\n", headerField);
        if (strstr(headerField, Expires) != NULL) {
            dateStr = strstr(headerField, ":") + 2;
            printf("%s: %s\n", "Has Expire Header!", dateStr);
            header->expire = parseDate(dateStr);
            header->hasExpire = 1;
        }
        if (strstr(headerField, Date) != NULL) {
            dateStr = strstr(headerField, ":") + 2;
            printf("%s: %s\n", "Has Date Header!", dateStr);
            header->date = parseDate(dateStr);
            header->hasDate = 1;
        }
        if (strstr(headerField, LastModified) != NULL) {
            dateStr = strstr(headerField, ":") + 2;
            printf("%s: %s\n", "Has LastModified Header!", dateStr);
            header->last_modified_time = parseDate(dateStr);
            header->hasLastModifiedTime = 1;
        }
        headerField = strtok(NULL, "\n");
    }
    return header;
}

//generate page
struct Pages *generatePage(char *filename, struct Headers *header) {
    printf("%s\n", "Inside generatePage");
    struct Pages *page = malloc(sizeof( struct Pages));

    page -> file_name = malloc(strlen(filename));
    strcpy(page -> file_name, filename);
    page -> last_used_time = time(NULL);
    page -> header = header;
    return page;
}

int findTheOldest() {
    int i, rst;
    time_t candidate = time(NULL);
    time_t curDate;
    for (i = 0; i < numOfFile; i++) {
        curDate = cache[i].last_used_time;
        if (difftime(curDate, candidate) < 0) {
            candidate = curDate;
            rst = i;
        }
    }
    return rst;
}

void addInCache(struct Pages *page) {
    int index;
    if (numOfFile == MAXNUMOFCACHE) {
        index = findTheOldest();
        cache[index] = *page;
    } else {
        cache[numOfFile] = *page;
        numOfFile++;
    }
}

void deleteInCache(int docInCache) {
    cache[docInCache] = cache[numOfFile - 1];
    numOfFile--;
}


void cacheHTTPRequest(char *filename, char *host, char *doc) {
    struct addrinfo hints, *servinfo, *res;
    int sockfd, byte_count, maxDescriptors, selectVal, i;
    FILE * fp;
    char buf[512];
    char message[512];
    char recvHeader[512];

    int iter = 0;
    int hasObtainedHeader = 0;


    fd_set clientDescriptors;
    fd_set allDescriptors;

    memset(&hints, 0,sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo(host, "80", &hints, &res);
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    printf("Connecting to web server...\n");
    connect(sockfd, res->ai_addr, res->ai_addrlen);
    printf("Connected!\n");
    char* sendHeader = "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n";

    //fill in the parameters
    sprintf(message, sendHeader, doc, host);

    printf("%s\n", message);
    send(sockfd, message, strlen(message), 0);
    printf("GET Sent...\n");

    fp = fopen (filename, "w+");

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
            if (byte_count == 0) {
                printf("All data has been received\n");
                fclose(fp);
                break;
            }
            if (hasObtainedHeader == 0) {
                hasObtainedHeader = obtainHeader(&iter, &buf[0], &recvHeader[0], byte_count);
            }
            printf("http: recv()'d %d bytes of data in buf\n", byte_count);
//            fprintf(fp, "%s", buf);
            fwrite(buf , 1 , byte_count , fp );
        }

    }
    printf("The header content is: %s\n", recvHeader);

    //form the header struct
    struct Headers *header = parseHeader(&recvHeader[0]);

    printf("%s\n", "Finish parserHeader");
    if (header -> hasExpire == 0 && header -> hasLastModifiedTime == 0) {
        printf("%s\n", "Missing important headers, reject caching!");
    } else {
        struct Pages *page = generatePage(filename, header);
        addInCache(page);
    }
}

//parse the url into doc and host
//host: www.tamu.edu, doc: index.html
void parseHostAndDoc(char *url, char *doc, char *host) {
    char delim[] = "/";
    char *token;
    int i = 1;
    for(token = strtok(url, delim); token != NULL; token = strtok(NULL, delim)) {
        // printf("%s\n", token);
        if (i == 1) {
            strcpy(host, token);
        } else {
            strcpy(doc, token);
        }
        i++;
    }
}

//send spacifc file to the client
void sendFileToClient(int new_fd, char *doc) {
    FILE *file;
    size_t nread;
    char buf[512];
    int byte_count;

    printf("%s\n", "Inside the sendFileToClient");
    file = fopen(doc, "r");
    if (file) {
        while ((nread = fread(buf, 1, sizeof buf, file)) > 0) {
            byte_count = send(new_fd, buf, nread, 0);
            if (byte_count == -1) {
                perror("send");
            }
            printf("send()'d %d bytes of data in buf to the client\n", byte_count);

            if (nread < 512) {
                if (feof(file))
                    printf("End of file\n");
                if (ferror(file))
                    printf("Error reading\n");
                break;
            }
        }

        if (ferror(file)) {
            perror("fread");
        }
        fclose(file);
    }
}
