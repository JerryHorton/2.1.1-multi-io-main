#include <stdio.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>

void * client_thread(void *arg) {
    int clientfd = *(int *) arg;
    while (1) {
        char buffer[128];
        int count = recv(clientfd, buffer, 128, 0);
        if (count == 0) {
            break;
        }
        printf("clientfd:%d, buffer:%s\n", clientfd, buffer);
        send(clientfd, buffer, count, 0);
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("sockdf");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind error");
        return -1;
    }
    listen(sockfd, 10);
#if 0  //normal
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int clientfd = accept(sockfd, (struct sockaddr *) &client_addr, &len);
    printf("accept\n");

    while (1) {
        char buffer[128];
        long count = recv(clientfd, buffer, 128, 0);
        if (count == 0) {
            break;
        }
        printf("sockfd:%d, clientfd:%d, buffer:%s\n", sockfd, clientfd, buffer);
        send(clientfd, buffer, count, 0);
    }
    close(clientfd);

#elif 0  //multi_thread
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int clientfd = accept(sockfd, (struct sockaddr *) &client_addr, &len);
        printf("accept\n");
       	pthread_t thread;
        pthread_create(&thread, NULL, client_thread, &clientfd);
    }

#elif 0  //select
    fd_set rfds, rset;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    int maxfd = sockfd;

    while (1) {
        rset = rfds;
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (FD_ISSET(sockfd, &rset)) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int clientfd = accept(sockfd, (struct sockaddr *) &client_addr, &len);
            FD_SET(clientfd, &rfds);
            maxfd = max(maxfd, clientfd);
        }
        for (int i = sockfd + 1; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                char buffer[128];
                int count = recv(i, buffer, 128, 0);
                if (count == 0) {
                    printf("disconnect\n");
                    FD_CLR(i, &rfds);
                    close(i);
                    continue;
                }
                printf("sockfd:%d, clientfd:%d, buffer:%s\n", sockfd, i, buffer);
                send(i, buffer, count, 0);
            }
        }
    }

#elif 0  //poll
    struct pollfd fds[1024] = {0};
    fds[sockfd].fd = sockfd;
    fds[sockfd].events = POLLIN;

    int maxfd = sockfd;
    while (1) {
        int nready = poll(fds, maxfd + 1, -1);
        if (fds[sockfd].revents & POLLIN) {
            struct sockaddr_in clientaddr;
            socklen_t len = sizeof(clientaddr);
            int clientfd = accept(sockfd, (struct sockaddr *) &clientaddr, &len);
            fds[clientfd].fd = clientfd;
            fds[clientfd].events = POLLIN;
            maxfd = max(maxfd, clientfd);
        }
        for (int i = sockfd + 1; i <= maxfd; i++) {
            if (fds[i].revents & POLLIN) {
                char buffer[128];
                int count = recv(i, buffer, 128, 0);
                if (count == 0) {
                    printf("disconnect\n");
                    fds[i].fd = -1;
                    fds->events = 0;
                    close(i);
                    continue;
                }
                printf("sockfd:%d, clientfd:%d, buffer:%s\n", sockfd, i, buffer);
                send(i, buffer, count, 0);
            }
        }
    }

#else  //epoll

    int epfd = epoll_create(1);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
    struct epoll_event events[1024] = {0};
    while (1) {
        int nready = epoll_wait(epfd, events, 1024, -1);
        for (int i = 0; i < nready; i++) {
            int connfd = events[i].data.fd;
            if (sockfd == connfd) {
                struct sockaddr_in clientaddr;
                socklen_t len = sizeof(clientaddr);
                int clientfd = accept(sockfd, (struct sockaddr *) &clientaddr, &len);
                ev.events = EPOLLIN;
                ev.data.fd = clientfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
            } else if (events[i].events & EPOLLIN) {
                char buffer[128];
                int count = recv(connfd, buffer, 128, 0);
                if (count == 0) {
                    printf("disconnect\n");
                    epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);
                    close(connfd);
                    continue;
                }
                printf("sockfd:%d, clientfd:%d, buffer:%s\n", sockfd, connfd, buffer);
                send(connfd, buffer, count, 0);
            }
        }
    }

#endif

}
