#select用法及总结
`select()` 实现一个简单的多客户端网络服务器，通过 `fd_set` 来管理文件描述符，
以实现非阻塞的多客户端连接处理
##实现步骤
###1. 文件描述符集合初始化:
   `fd_set rfds, rset;`  
   `FD_ZERO(&rfds);`    
   `FD_SET(sockfd, &rfds);`  
   `FD_ZERO` 用于清空文件描述符集合 `rfds`，即初始化。
   `FD_SET(sockfd, &rfds);` 将监听套接字 `sockfd` 加入到 `rfds` 集合中。
   这样 `select()` 会关注 `sockfd` 上的事件。  
###2. 使用 `select()`:  
   `rset = rfds;`  
   `int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);` 
   - 每次进入循环，将 `rfds` 赋值给 `rset`。这是因为 `select()` 调用会改变 `rset`，需要在每次循环开始时重置。  
   -  `select(maxfd + 1, &rset, NULL, NULL, NULL);`   
     监听 `rset` 中的文件描述符是否有事件发生。`maxfd + 1` 表示要检查的最大文件描述符的范围(个数)，`+1`操作是因为文件描述符从0开始。  
     `nready` 表示就绪文件描述符的数量。
###3. 处理新的客户端连接:
```c
  if (FD_ISSET(sockfd, &rset)) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int clientfd = accept(sockfd, (struct sockaddr *) &client_addr, &len);
    FD_SET(clientfd, &rfds);
    maxfd = max(maxfd, clientfd);
}
```
   - 如果 `sockfd` 有事件发生（即 `FD_ISSET(sockfd, &rset)` 为真），表示有新的客户端连接。  
   - 使用 `accept()` 接收新连接，返回客户端的 `clientfd`。  
   - 将新的 `clientfd` 加入到 `rfds` 中，以便后续的 `select()` 调用能监听该客户端的事件。  
   - 更新 `maxfd`，确保 `select()` 检查到新添加的客户端文件描述符。

### 4. 处理客户端数据:
```c
   for (int i = sockfd + 1; i <= maxfd; i++) {
        if (FD_ISSET(i, &rset)) {
            char buffer[128];
            int count = recv(i, buffer, 128, 0);
            if (count == 0) {
                printf("disconnect\n");
                FD_CLR(i, &rfds);
                close(i);
                break;
            }
            send(i, buffer, count, 0);
        }
    }
```
   - 循环从 `sockfd + 1` 到 `maxfd`，遍历所有可能的客户端文件描述符。
   - 如果 `FD_ISSET(i, &rset)` 为真，表示该客户端有数据可读。  
   - 通过 `recv()` 读取客户端的数据，存储在 `buffer` 中。  
   - 如果 `count == 0`，表示客户端已关闭连接。此时需要使用 `FD_CLR(i, &rfds)` 从集合中删除该文件描述符，并 `close(i)` 关闭该连接。  
   - 如果读取了有效数据，则通过 `send()` 将数据回传给客户端。  
   -` FD_CLR(i, &rfds)` 表示清除 `rfds` 中的 `i` 文件描述符,当客户端断开连接时，应用程序不再需要监听这个客户端的文件描述符。因此，调用 `FD_CLR()` 将该文件描述符从监听集合中移除。
##需要注意的问题 
###1. 对 `FD_CLR` 和 `FD_ISSET` 的注意:
- 关闭连接后，需要将对应的文件描述符从 `rfds` 中移除，否则下一次 `select()` 会继续关注已关闭的文件描述符，导致错误。
###2. 定义两个 `fd_set` `rfds` 和 `rset`的原因
- 主要是为了区分原始文件描述符集合和每次调用 select 时用来检测事件的集合。
  - `rfds` 是原始文件描述符集合，保存了当前需要监听的所有文件描述符，包括服务器套接字和所有客户端套接字。这个集合在程序的整个生命周期内都保持不变或根据客户端的连接和断开而更新。
  - `rset` 是临时集合，每次进入循环时，`rset` 都会被赋值为 `rfds`，然后传递给 `select()`。这样做的原因是，`select()` 调用会修改传入的 `fd_set`，只保留那些有事件发生的文件描述符。为了在下一次循环中重新检测所有文件描述符的状态，需要用 `rfds` 重置 `rset`。
##select的优缺点
###优点
- **简单易用**: `select()` 是一种跨平台的 I/O 多路复用机制，适合处理少量文件描述符的并发操作。
- **阻塞和非阻塞模式**: `select()` 可以轻松地在阻塞和非阻塞模式之间切换，灵活性较高。
###缺点
- **性能问题**: `select()` 会在每次调用时扫描所有传入的文件描述符集合，因此当文件描述符数量非常多时，性能会显著下降，尤其是在处理数千个文件描述符时。每次调用 `select()` 都需要重新遍历整个文件描述符集合，这使得它在处理大规模连接时不是很高效。具体来说，
  内核会遍历用户提供的文件描述符集合中的每一个文件描述符，检查它们的状态（如是否可读、可写、是否有异常等）。这种操作是线性的，意味着内核需要逐个检查集合中的每个文件描述符。内核只能通过轮询来监控文件描述符的状态，而不能通过异步事件通知的方式来高效处理这些操作。
  这在处理大量文件描述符时，会造成性能瓶颈，尤其是每次调用 `select()` 都需要重新检查文件描述符集合。 如果传入的文件描述符集合很大，`select()` 需要遍历这些文件描述符，这会导致内核的开销随着文件描述符数量的增加而增加。 
- **文件描述符的限制**: `select()` 对文件描述符的数量有最大限制，这个限制通常是 1024（取决于操作系统和内核设置）。如果需要监控更多的文件描述符，则必须调整内核的设置，或者考虑使用 `poll()` 或 `epoll()`。
#poll用法及总结
使用 `poll()` 来代替 `select()` 进行 I/O 多路复用。其核心功能是监听客户端的连接请求，并处理已连接客户端的读写操作。
##实现步骤
###1.定义文件描述符集合
```c
    struct pollfd fds[1024] = {0};  
    fds[sockfd].fd = sockfd;  
    fds[sockfd].events = POLLIN;  
```
- `struct pollfd` 是 `poll()` 系统调用使用的数据结构，用于描述待监控的文件描述符及其事件。
  - `fd` 字段表示文件描述符（比如监听套接字或者客户端套接字）。
  - `events` 字段表示等待事件类型。 `POLLIN` 表示监听文件描述符可读。`POLLOUT` 表示监听文件描述符可写。
  - `revents` 表示实际发生的事件类型。
- 创建一个结构体数组 fds 用于存放每个文件描述符及其状态。
- 将监听套接字 `sockfd` 添加到 `fds` 数组中，监控其状态。
- 设置监听套接字的事件为 `POLLIN`，即监听可读事件。
###2. 监听文件描述符
`int maxfd = sockfd;`  
`int nready = poll(fds, maxfd + 1, -1);`   
- `maxfd` 是当前监听的最大文件描述符，初始化为 `sockfd`。
- `poll`() 系统调用，它会阻塞直到其中一个文件描述符准备好。
- `fds` 是文件描述符集合，表示要监听的文件描述符及其事件。 
- `maxfd + 1` 表示文件描述符数量, `+1`是因为文件描述符从`0`开始，`poll()` 需要这个值来确定需要处理的文件描述符范围。
- -1 表示无限期阻塞，直到至少一个文件描述符准备好。
###3. 处理新的客户端连接
```c
    if (fds[sockfd].revents & POLLIN) { 
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);
        int clientfd = accept(sockfd, (struct sockaddr *) &clientaddr, &len);
        fds[clientfd].fd = clientfd;  
        fds[clientfd].events = POLLIN;  
        maxfd = max(maxfd, clientfd); 
    }
```
- 判断监听套接字 `sockfd` 是否有可读事件（即有新的客户端连接）。
- 如果是，使用 `accept()` 接受客户端连接，返回新的客户端套接字 `clientfd`。 将 `clientfd` 加入 `fds` 数组，设置其事件为 `POLLIN`（表示可读）。
- 跟新`maxfd` ,确保 `maxfd` 始终是当前已连接客户端中的最大文件描述符，使`poll()` 正确处理文件描述符范围。
###4. 处理客户端数据:
```c
    if (fds[i].revents & POLLIN) {
        ...
    }
```
`fds[i].revents & POLLIN`
- 检查某个文件描述符是否可以读取数据的条件
###5. 客户端断开连接
```c
    if (count == 0) {  
        printf("disconnect\n");
        fds[i].fd = -1; 
        fds[i].events = 0;  
        close(i);  
        break;  
    }
```
- 将 `fds[i].fd` 设置为 `-1`，表示该文件描述符不再有效。
- 清除 `fds[i].events = 0` 的事件标志。 
- 使用 `close(i)` 关闭文件描述符并释放与其相关的系统资源。
##需要注意的问题
###1. `FD_CLR()` 和 `close()` 的调用顺序
- 先调用 `FD_CLR()` 清除文件描述符的监听状态，因为我们不再需要监听已断开连接的客户端。 然后调用 `close()` 关闭文件描述符并释放与其相关的系统资源。
- 如果先调用 `close()`，该文件描述符就会立即失效，不能再用于任何系统调用（包括 I/O 多路复用）。因此，必须先清除文件描述符的监听状态，然后再关闭文件描述符，以避免可能的错误或资源泄漏。
###2. `revents` 的处理
- `poll()` 会修改 `pollfd` 数组中每个元素的 `revents` 字段，表示哪些文件描述符上发生了事件。如果不小心处理 `revents`，可能会导致错误的事件响应。
  在处理事件时，务必清楚地检查 revents 字段，只有在对应的事件发生时（如 POLLIN、POLLOUT 等），才进行相应的操作。还需要注意 revents 中的错误标志（如 POLLERR、POLLHUP），这些标志表示文件描述符出现了问题，
  应采取适当的错误处理措施。
#epoll用法及总结
`epoll`是为处理大批量文件描述符而作了改进的`poll`
##实现步骤
###1. 创建`epoll`实例
`int epfd = epoll_create(1);`
- `epoll_create(1)` 创建了一个 `epoll` 实例，返回一个 `epfd`（`epoll` 文件描述符）。参数 `1` 是一个建议大小，它告诉内核我们预计将会监听的文件描述符的数量。实际上，这个值在现代内核中已经不再严格重要。它的作用是向内核传递一个初始的优化建议。
###2.  设置 `sockfd` 为监听事件
```c
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
```
- 通过 `ev` 配置监听的事件类型。`ev.events = EPOLLIN` 表示监听 `sockfd` 是否有可读事件（即客户端是否发起连接请求）。
- `ev.data.fd = sockfd` 存储文件描述符，以便在后续处理中识别该文件描述符。
- `epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev)` 将 `sockfd` 加入到 `epoll` 实例中，表示我们关心 `sockfd` 上的可读事件（连接请求）。`epoll_ctl()` 用于管理 `epoll` 实例内的文件描述符，向 `epoll` 添加、修改或删除文件描述符的事件监控。
- `EPOLL_CTL_ADD` 是`epoll_ctl()` 的操作类型参数,表示将一个新的文件描述符（`sockfd`）添加到 `epoll` 实例中进行事件监听。
###3. 等待和处理事件
```c
    struct epoll_event events[1024] = {0};
    while (1) {
        int nready = epoll_wait(epfd, events, 1024, -1);
        for (int i = 0; i < nready; i++) {
            int connfd = events[i].data.fd;
            ...
        }
    }
```
- `struct epoll_event events[1024] = {0};`定义一个 `events` 数组用于存放 `epoll_wait` 返回的事件信息，最大容量为 `1024`。该数组将保存 `epoll` 监控到的所有文件描述符的状态。
- `epoll_wait(epfd, events, 1024, -1);`等待事件的发生，最多监听 `1024` 个文件描述符。`-1` 表示永久阻塞，直到有事件发生。
- `nready` 表示事件的数量，具体是指 `events` 数组中有多少个文件描述符上的事件已经就绪（即文件描述符上有数据可读、可写，或发生了错误）。
###4. 处理新连接（客户端连接）
```c
    if (sockfd == connfd) {
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);
        int clientfd = accept(sockfd, (struct sockaddr *) &clientaddr, &len);
        ev.events = EPOLLIN;
        ev.data.fd = clientfd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
    }
```
- 如果 `connfd` 是 `sockfd`，说明有一个新的客户端连接请求。
- `accept()` 接受来自客户端的连接，并返回新的连接文件描述符 `clientfd`。
- 将新连接 `clientfd` 加入 `epoll`，并且监听该连接的可读事件（即客户端是否有数据发送过来）。
###5. 处理客户端数据
```c
    else if (events[i].events & EPOLLIN) {
        ...
    }
```
- `events[i].events & EPOLLIN;`表示如果该事件是可读事件（客户端发来了数据），则进入处理。
###6. 关闭连接并删除 `epoll` 监控
- `epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, NULL);`从 `epoll` 实例中删除 `connfd`，停止监控该文件描述符。
- `close(connfd);`关闭客户端连接的文件描述符，释放资源。
##epoll优点
- **高效的事件驱动**：`epoll` 的主要优点是它在处理大量并发连接时的高效性。只有发生事件的文件描述符会被处理，避免了 `select` 或 `poll` 需要遍历所有文件描述符的问题。
- **支持边缘触发和水平触发**：`epoll` 提供了两种触发模式，支持边缘触发（ET）和水平触发（LT），可以根据需要选择更高效的触发模式。
  - **水平触发（LT）**:是 `epoll` 的一种事件触发模式。在 LT 模式下，当某个文件描述符上的事件就绪时，`epoll_wait()` 会返回这个事件，并且只要这个事件条件仍然满足，`epoll_wait()` 就会在每次调用时持续返回该事件。
    在 LT 模式下，`epoll` 会一直通知程序某个文件描述符处于就绪状态，直到该事件被处理。例如，如果某个文件描述符上有数据可以读取，只要数据没有被读取完，`epoll_wait()` 每次调用都会返回该文件描述符的 `EPOLLIN` 事件。
    - **优点**：
      - **不会丢事件**：文件描述符的状态一直处于就绪时，epoll_wait() 会反复通知，确保事件不会遗漏。 
      - **编程简单**：LT 模式适合需要逐步处理大批量数据的情况，不会因为一次未读完而失去通知。
     - **缺点**：
       - **效率较低**：由于文件描述符处于就绪状态时会不断被通知，这在并发量较大的场景下可能导致较多的重复通知，增加处理开销。
    - **适用场景**: LT 模式适用于大部分应用，尤其是在需要确保每一个 I/O 事件都能被检测到的场景。 
  - **边缘触发（ET）**:  是 `epoll` 的另一种触发模式，与水平触发（LT）模式不同。在 ET 模式下，`epoll` 只会在文件描述符的状态从未就绪变为就绪的“边缘”时触发事件通知。换句话说，ET 模式仅在检测到某个文件描述符的状态发生“边缘变化”（例如从无数据到有数据可读）
    时才触发事件，并且只通知一次。ET 模式要求在每次 `epoll_wait()` 返回事件后立即对文件描述符进行非阻塞 I/O 操作，尽可能读取或写入所有数据，以免错过后续的数据。在文件描述符就绪状态持续的情况下，`epoll` 不会重复通知程序，需要程序主动处理完所有数据或再次等待“边缘”变化。
    - **优点**:
      - 性能更高：减少重复通知，降低了系统调用的次数，适合高并发场景。
      - 适用于事件驱动：在事件频繁的环境中，ET 模式可高效利用 CPU 资源，不会因重复事件造成开销。 
    - **缺点**:
      - 编程复杂度高：ET 模式要求在每次事件触发后尽可能一次性完成数据处理，若数据未处理完（尤其是非阻塞模式下），可能导致数据丢失。
      - 容易出错：若编写不当，很容易错过后续数据或出现“饥饿”问题（文件描述符准备就绪却未被处理）。
    - **适用场景**:  ET 模式适合高性能、高并发应用，如 Web 服务器、聊天服务器等需要处理大量连接的场景。然而，它需要开发者更为细致的控制和维护。









   
