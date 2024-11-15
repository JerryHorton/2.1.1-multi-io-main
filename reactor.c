#include <stdio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <memory.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

#define BUFFER_LENGTH 128
#define ENABLE_HTTP_RESPONSE 1
#define TIME_SUB_MS(tv1, tv2) ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

typedef int(*RCALLBACK)(int fd);

int epfd;

int recv_cb(int connfd);
int send_cb(int fd);

struct epoll_event events[1048576] = {0};
struct timeval tv_begin;
struct timeval tv_cur;

struct conn_item {
    int fd;
    char r_buffer[BUFFER_LENGTH];
    char w_buffer[BUFFER_LENGTH];
    int r_len;
    int w_len;
    union {
        RCALLBACK accept_callback;
        RCALLBACK recv_callback;
    } recv_t;
    RCALLBACK send_callback;
} connList[1048576];

#if ENABLE_HTTP_RESPONSE

typedef struct conn_item connection_t;

int http_request(connection_t *conn) {
    return 0;
}

int http_response(connection_t *conn) {
    int filefd = open("resource/index.html", O_RDONLY);
    struct stat stat_buf;
    fstat(filefd, &stat_buf);
    long length = stat_buf.st_size;
    conn->w_len = sprintf(conn->w_buffer,
                          "HTTP/1.1 200 OK\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "Content-Length: %ld\r\n"
                         "Content-Type: text/html\r\n"
                         "Date: Tue, 12 Nov 2024 12:54:32 GMT\r\n\r\n", length);
    int count = read(filefd, conn->w_buffer + conn->w_len, BUFFER_LENGTH - conn->w_len);
    conn->w_len += count;
    return conn->w_len;
}

#endif

void set_events(int fd, int event, int flag) {
    struct epoll_event ev;
    ev.events = event;
    ev.data.fd = fd;
    if (flag) {
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    } else {
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }
}

int accept_cb(int sockfd) {
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);
    int clientfd = accept(sockfd, (struct sockaddr *) &clientaddr, &len);
    if (clientfd < 0) {
        return -1;
    }
    set_events(clientfd, EPOLLIN, 1);
    connList[clientfd].fd = clientfd;

    memset(connList[clientfd].r_buffer, 0, BUFFER_LENGTH);
    memset(connList[clientfd].w_buffer, 0, BUFFER_LENGTH);
    connList[clientfd].r_len = 0;
    connList[clientfd].w_len = 0;

    connList[clientfd].recv_t.recv_callback = recv_cb;
    connList[clientfd].send_callback = send_cb;

    if ((clientfd % 1000) == 999) {
        gettimeofday(&tv_cur, NULL);
        int time_used = TIME_SUB_MS(tv_cur, tv_begin);
        memcpy(&tv_begin, &tv_cur, sizeof(struct timeval));
        printf("clientfd:%d, time_used:%d\n", clientfd, time_used);
    }

    return clientfd;
}

int recv_cb(int connfd) {
    char *buffer = connList[connfd].r_buffer;
    int idx = connList[connfd].r_len;
    int count = recv(connfd, buffer + idx, BUFFER_LENGTH - idx, 0);
    if (count == 0) {
        printf("disconnect\n");
        epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
        close(connfd);
        return -1;
    }
    connList[connfd].r_len += count;
    //http_request(&connList[connfd]);
    //http_response(&connList[connfd]);
    memcpy(connList[connfd].w_buffer, connList[connfd].r_buffer, connList[connfd].r_len);
    connList[connfd].w_len = connList[connfd].r_len;
    connList[connfd].r_len -= connList[connfd].r_len;
    set_events(connfd, EPOLLOUT, 0);
    return count;
}

int send_cb(int connfd) {
    char *buffer = connList[connfd].w_buffer;
    int idx = connList[connfd].w_len;
    //http_response(&connList[connfd]);
    int count = send(connfd, buffer, idx, 0);
    set_events(connfd, EPOLLIN, 0);
    return count;
}

int Init_server(unsigned short port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("sockdf");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        return -1;
    }
    listen(sockfd, 10);
    return sockfd;
}

int main() {
    int port_count = 30;
    unsigned port = 8080;
    epfd = epoll_create(1);
    for (int i = 0; i < port_count; i++) {
        int sockfd = Init_server(port + i);
        connList[sockfd].fd = sockfd;
        connList[sockfd].recv_t.accept_callback = accept_cb;
        set_events(sockfd, EPOLLIN, 1);
    }

    gettimeofday(&tv_begin, NULL);

    while (1) {
        int nready = epoll_wait(epfd, events, 1048576, -1);
        for (int i = 0; i < nready; i++) {
            int connfd = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                int recv_count = connList[connfd].recv_t.accept_callback(connfd);
                //printf("recv <-- buffer:%s\n", connList[connfd].r_buffer);
            } else if (events[i].events & EPOLLOUT) {
                int send_count = connList[connfd].send_callback(connfd);
                //printf("send --> buffer:%s\n", connList[connfd].w_buffer);
            }
        }
    }
}
