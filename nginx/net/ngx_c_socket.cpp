/**
 * Socket类是通信功能的核心类(该类最终是否命名为Socket还在犹豫)
 * Socket类作为父类存在，提供通信基本功能
 * 子类继承该类之后，在子类中实现具体的业务逻辑
*/

#include <string.h> // memset
#include <errno.h> // errno
#include <unistd.h> // close
#include <pthread.h> // pthread_mutex_init pthread_mutex_destroy
#include <sys/socket.h> // socket setsockopt
#include <arpa/inet.h> // sockaddr_in

#include "ngx_c_socket.h"

#include "ngx_c_threadPool.h"
#include "app/ngx_log.h"
#include "app/ngx_c_conf.h"
#include "_include/ngx_func.h"
#include "_include/ngx_macro.h"
#include "misc/ngx_c_memoryPool.h"

Socket::Socket() : 
    m_portCount(0),
    m_epfd(-1),
    m_connectionPool(nullptr){}

Socket::~Socket(){
    // 释放监听套接字
    for(auto & sock : m_listenSokcetList){
        delete sock;
    }
    m_listenSokcetList.clear();
}

/********************************************************************
 * 监听套接字有关的函数
*********************************************************************/
int Socket::ngx_sockets_init(){
    ConfFileProcessor * confProcess = ConfFileProcessor::getInstance();
    m_portCount = confProcess->ngx_conf_getContent_int("PortCount", NGX_PROT_COUNT);

    int sockfd;
    struct sockaddr_in serv_addr;
    int port;
    char tmp_str[100];
    int ret; // 记录返回值

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    for(int i = 1; i <= m_portCount; ++ i){
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd < 0){
            ngx_log(NGX_LOG_FATAL, errno, "Socket::ngx_open_listening_sockets()中执行socket()失败, i = %d", i);
            return -1;
        }

        int reuseaddr = 1;
        ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuseaddr, sizeof(reuseaddr));
        if(ret < 0){
            ngx_log(NGX_LOG_ERR, errno, "Socket::ngx_open_listening_sockets()中执行setsockopt(SO_REUSEADDR)失败, i = %d", i);
            close(sockfd);                                               
            return -1;
        }

        ret = ngx_set_nonblocking(sockfd);
        if(ret < 0){
            ngx_log(NGX_LOG_ERR, errno, "Socket::ngx_open_listening_sockets()中执行ngx_set_nonblocking()失败, i = %d", i);
            close(sockfd);                                               
            return -1;
        }

        memset(tmp_str, 0, 100);
        sprintf(tmp_str, "Port%d", i);
        port = confProcess->ngx_conf_getContent_int(tmp_str);
        if(port < 0){
            if(m_portCount == 1){
                port = NGX_LISTEN_PORT;
            }
            else{
                ngx_log(NGX_LOG_FATAL, 0, "配置文件中没有提供Port%d", i);
                close(sockfd);                                               
                return -1;
            }
        }

        serv_addr.sin_port = htons(in_port_t(port));

        ret = bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        if(ret < 0){
            ngx_log(NGX_LOG_FATAL, errno, "Socket::ngx_open_listening_sockets()中执行bind()失败, i = %d", i);
            close(sockfd);                                               
            return -1;
        }

        ret = listen(sockfd, NGX_LISTEN_BACKLOG);
        if(ret < 0){
            ngx_log(NGX_LOG_FATAL, errno, "Socket::ngx_open_listening_sockets()中执行listen()失败, i = %d", i);
            close(sockfd);                                               
            return -1;
        }

        ngx_log(NGX_LOG_INFO, 0, "监听%d端口成功", port);
        ListenSocket * sock = new ListenSocket;
        sock->sockfd = sockfd;
        sock->port = port;
        sock->connection = nullptr;
        m_listenSokcetList.push_back(sock);
    }

    return 0;
}

void Socket::ngx_sockets_close(){
    for(auto & sock : m_listenSokcetList){
        close(sock->sockfd);
        ngx_log(NGX_LOG_INFO, 0, "监听端口%d已关闭", sock->port);
    }
}

/********************************************************************
 * epoll有关的函数
*********************************************************************/
int Socket::ngx_epoll_init(){
    ConfFileProcessor * confProcess = ConfFileProcessor::getInstance();
    int connectionSize = confProcess->ngx_conf_getContent_int("WorkerConnections", NGX_WORKER_CONNECTIONS);

    // [1] 创建epoll对象
    m_epfd = epoll_create(connectionSize);
    if(m_epfd < 0){
        ngx_log(NGX_LOG_FATAL, errno, "ngx_epoll_init()中调用epoll_create()函数失败");
        return -1;
    }

    // [2] 创建连接池
    m_connectionPool = new ConnectionPool(connectionSize);

    // [3] 为每个监听套接字绑定一个连接池中的连接
    TCPConnection * c;
    for(auto & sock : m_listenSokcetList){
        c = m_connectionPool->ngx_get_connection(sock->sockfd);
        if(c == nullptr){ // 直接错误返回，因为ngx_get_connection()函数中已经写过日志了
            return -1;
        }
        sock->connection = c;

        c->r_handler = &Socket::ngx_event_accept;

        if(ngx_epoll_addEvent(sock->sockfd, 1, 0, 0, EPOLL_CTL_ADD, c) < 0){
            return -1;
        }
    }

    return 0;
}

int Socket::ngx_epoll_addEvent(int sockfd, int r_event, int w_event, uint32_t otherFlags, uint32_t eventType, TCPConnection * c){
    struct epoll_event et;
    memset(&et, 0, sizeof(struct epoll_event));
    if(r_event == 1){
        et.events = EPOLLIN|EPOLLRDHUP;
    }
    else{
        // 其他事件类型待处理
    }

    if(otherFlags != 0){
        et.events |= otherFlags;
    }

    et.data.ptr = (void *)( (uintptr_t)c | c->instance);   //把对象弄进去，后续来事件时，用epoll_wait()后，这个对象能取出来用 
                                                             //但同时把一个 标志位【不是0就是1】弄进去

    if(epoll_ctl(m_epfd, eventType, sockfd, &et) < 0){
        ngx_log(NGX_LOG_FATAL, errno, "Socket::ngx_epoll_addEvent()中执行epoll_ctl()失败");
        return -1;
    }

    return 0;
}

int Socket::ngx_epoll_getEvent(int timer){
    int events = epoll_wait(m_epfd, m_events, NGX_MAX_EVENTS, timer);
    if(events < 0){ // 表示出错
        if(errno == EINTR){ // 信号中断错误
            ngx_log(NGX_LOG_INFO, errno, "Socket::ngx_epoll_processEvent()中因信号中断导致执行epoll_wait()失败");
            return 0; // 这种错误算作正常情况
        }
        else{
            ngx_log(NGX_LOG_FATAL, errno, "Socket::ngx_epoll_processEvent()中执行epoll_wait()失败");
            return -1;
        }
    }
    else if(events == 0){
        if(timer == -1){ // timer等于-1时会一直阻塞，直到事件发生，此时events不可能为0
            ngx_log(NGX_LOG_ERR, 0, "Socket::ngx_epoll_processEvent()中epoll_wait()没超时却没返回任何事件");
            return -1;
        }

        return 0;
    }

    // 收到了events个IO事件，下面进行处理
    TCPConnection * c;
    uintptr_t instance;
    uint32_t eventType;
    for(int i = 0; i < events; ++ i){
        c = (TCPConnection *)(m_events[i].data.ptr);
        instance = (uintptr_t) c & 1;
        c = (TCPConnection *) ((uintptr_t)c & (uintptr_t) ~1);

        if(c->sockfd == -1 || c->instance != instance){ // 表示该事件是过期事件
            ngx_log(NGX_LOG_DEBUG, 0, "Socket::ngx_epoll_processEvent()中遇到了过期事件");
            continue; // 对于过期事件不予处理
        }

        // 开始处理正常的事件
        eventType = m_events[i].events; // 事件类型
        if(eventType & (EPOLLERR | EPOLLHUP)){ // 错误类型
            eventType |= EPOLLIN | EPOLLOUT; // 增加读写标记
        }

        if(eventType & EPOLLIN){ // 读事件，两种情况：新客户端连入事件；已连接的客户端发送数据事件
            (this->*(c->r_handler))(c); // 如果是新客户端连接事件，那么执行ngx_event_accept(c);
                                        // 如果是已连接的客户端发送数据事件，那么执行ngx_event_recv(c)
        }

        if(eventType & EPOLLOUT){ // 写事件
            //后续增加...
        }
    }

    return 0;
}

/********************************************************************
 * 事件有关的函数
*********************************************************************/
void Socket::ngx_event_accept(TCPConnection * c){
    struct sockaddr cliAddr;
    socklen_t addrlen;
    int errnoCp; // 拷贝errno
    int sockfd; // TCP连接对应的socket描述符
    int accept4_flag; // 标记是否使用accept4，accept4函数是linux特有的扩展函数
    TCPConnection * newConnection; // 与新连接绑定

    addrlen = sizeof(struct sockaddr);
    while(1){
        if(accept4_flag){
            sockfd = accept4(c->sockfd, &cliAddr, &addrlen, SOCK_NONBLOCK);
        }
        else{
            sockfd = accept(c->sockfd, &cliAddr, &addrlen);
        }

        if(sockfd == -1){ // 发生了错误
            errnoCp = errno;
            if(accept4_flag && errnoCp == ENOSYS){ // 不支持accept4
                accept4_flag = 0;
                continue;
            }

            if(errnoCp == EAGAIN){ // accept()没准备好，这个EAGAIN错误和EWOULDBLOCK是一样的
                //如何处理...
            }

            if(errnoCp == ECONNABORTED){ // 对方意外关闭套接字
                //如何处理...
            }

            if(errnoCp == EMFILE || errnoCp == ENFILE){ // EMFILE进程文件描述符用尽，ENFILE??
                //如何处理...
            }

            ngx_log(NGX_LOG_FATAL, errnoCp, "ngx_event_accpet()函数中执行accept()函数失败");
            return;
        }

        // 处理accept成功的情况
        if(!accept4_flag){
            if(ngx_set_nonblocking(sockfd) < 0){
                ngx_log(NGX_LOG_ERR, errno, "ngx_event_accpet()中执行ngx_set_nonblocking()失败");
                close(sockfd);                                             
                return;
            }
        }

        newConnection = m_connectionPool->ngx_get_connection(sockfd);
        if(newConnection == nullptr){ // 直接错误返回，因为ngx_get_connection()函数中已经写过日志了
            close(sockfd);
            return;
        }
        // 需判断是否超过最大允许的连接数...

        memcpy(&newConnection->cliAddr, &cliAddr, addrlen);
        newConnection->w_ready = 1; // 已准备好写数据
        newConnection->r_handler = &Socket::ngx_event_recv;
        newConnection->curRecvPktState = RECV_PKT_HEADER; // 收包状态处于准备接收包头状态
        newConnection->recvIndex = newConnection->pktHeader;
        newConnection->recvLength = sizeof(PktHeader);
        
        if(ngx_epoll_addEvent(sockfd, 1, 0, 0, EPOLL_CTL_ADD, newConnection) < 0){ // LT模式
            ngx_event_close(newConnection);
            return;
        }

        break;
    }
}

void Socket::ngx_event_close(TCPConnection * c){
    close(c->sockfd);
    c->sockfd = -1; // 表征该连接已过期
    m_connectionPool->ngx_free_connection(c);
}

void Socket::ngx_event_recv(TCPConnection * c){
    ngx_log(NGX_LOG_DEBUG, 0, "执行了ngx_event_recv()函数");

    ssize_t len = recv(c->sockfd, c->recvIndex, c->recvLength, 0); // recv()系统调用，这里flags设置为0，作用同read()

    if(len < 0){ // 出错，通过errno判断错误类型
        ngx_log(NGX_LOG_ERR, errno, "ngx_event_recv()函数中调用recv()函数接收数据错误");

        int errnoCp = errno;
        if(errnoCp == EAGAIN || errnoCp == EWOULDBLOCK){ // 表示没有拿到数据
            /*
            一般只有在ET模式下才会出现该错误
            该错误类型常出现在ET模式下，ET模式下从缓存区中拿数据是要在循环中不断的调用recv()
            当recv()函数返回-1，并且errno = EAGAIN || EWOULDBLOCK，表示缓冲区中没有数据了，此次接收数据结束
             */
            return;
        }

        if(errnoCp == EINTR){ // 什么错误？？
            // ... 如何处理
            return;
        }

        if(errnoCp == ECONNRESET){ // 客户端没有正常的关闭连接，此时服务器端也应关闭连接
            ngx_event_close(c);
            return;
        }

        // ... 其他错误等待后续完善
    }

    if(len == 0){ // 表示客户端关闭连接，服务器端也应关闭连接
        ngx_event_close(c);
        return;
    }

    // 处理正确接收的情况，采用状态机
    if(c->curRecvPktState == RECV_PKT_HEADER){
        if(len == c->recvLength){ // 接收到了完整的包头
            ngx_pktHeader_parsing(c);
        }
        else{
            c->recvIndex += len;
            c->recvLength -= len;
        }
    }
    else if(c->curRecvPktState == RECV_PKT_BODY){
        if(len == c->recvLength){ // 接收到了完整的包体
            ngx_packet_handle(c);
        }
        else{
            c->recvIndex += len;
            c->recvLength -= len;
        }
    }
    else{
        ngx_log(NGX_LOG_ERR, 0, "ngx_event_recv()函数中TCP连接对象c处于无效的收包状态，%d", c->curRecvPktState);
    }
}

void Socket::ngx_pktHeader_parsing(TCPConnection * c){
    ngx_log(NGX_LOG_DEBUG, 0, "执行了ngx_pktHeader_parsing()函数");

    uint16_t pktHd_len = static_cast<uint16_t>(sizeof(PktHeader));

    PktHeader * pktHd = (PktHeader *)(c->pktHeader);
    uint16_t pktLen = ntohs(pktHd->len); // 拿到完整的包长信息
    if(pktLen < pktHd_len){ // 记录的包长小于包头长度，此时数据包无效
        c->curRecvPktState = RECV_PKT_HEADER;
        c->recvIndex = c->pktHeader;
        c->recvLength = pktHd_len;

        // ...这里处理有问题，没有将缓冲区中包体丢弃
        return;
    }

    if(pktLen > PKT_MAX_LEN){ // 对于大于PKT_MAX_LEN(10240)字节的包，认为也是无效包
        c->curRecvPktState = RECV_PKT_HEADER;
        c->recvIndex = c->pktHeader;
        c->recvLength = pktHd_len;

        // ...这里处理有问题，没有将缓冲区中包体丢弃
        return;
    }

    ngx_log(NGX_LOG_DEBUG, 0, "收到正确的数据包");

    // 处理正确的情况
    // [1] 分配内存，用于存放消息头+包头+包体
    uint8_t * buf = (uint8_t *)MemoryPool::getInstance()->ngx_alloc_memory(sizeof(struct MsgHeader) + pktLen);
    c->recvBuffer = buf;

    // [2] 填写消息头
    MsgHeader * msgHd = (MsgHeader *)buf;
    msgHd->c = c;
    msgHd->curSeq = c->curSeq;

    // [3] 填充包头
    buf += sizeof(MsgHeader);
    memcpy(buf, c->pktHeader, pktHd_len);

    if(pktLen == pktHd_len){ // 只有包头没有包体的情况，这种情况是允许的
        ngx_packet_handle(c);
    }
    else{
        c->curRecvPktState = RECV_PKT_BODY;
        c->recvIndex = buf + pktHd_len; // buf当前指向包头，后移sizeof(PktHeader)指向包体
        c->recvLength = pktLen - pktHd_len;
    }
}

void Socket::ngx_packet_handle(TCPConnection * c){
    ngx_log(NGX_LOG_DEBUG, 0, "执行了ngx_packet_handle()函数");

    // [1] 将接收到的完整的数据push到消息队列
    ThreadPool * tp = ThreadPool::getInstance();
    tp->ngx_msgQue_push(c->recvBuffer);

    // [2] 准备接收后续的数据
    c->recvBuffer = nullptr; // 对应的内存由消息队列接管
    c->curRecvPktState = RECV_PKT_HEADER;
    c->recvIndex = c->pktHeader;
    c->recvLength = sizeof(PktHeader);
}