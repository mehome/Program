#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <functional>

#include <tools_cxx/noncopyable.h>
#include <tools_cxx/Timestamp.h>

#include "EventLoop.h"

class EventLoop; // 声明

class Channel : noncopyable{ // 不可复制
public:
    typedef std::function<void()> EventCallback; // 事件回调函数类型
    typedef std::function<void(Timestamp)> ReadEventCallback; // 读事件回调函数类型，需要时间戳参数

    Channel(EventLoop * loop, int fd);
    ~Channel();

    // get_xx is_xx set_xx
    void set_readCallback(const ReadEventCallback & rcb){
        m_readCallback = rcb;
    }

    void set_writeCallback(const EventCallback & wcb){
        m_writeCallback = wcb;
    }

    void set_closeCallback(const EventCallback & ccb){
        m_closeCallback = ccb;
    }

    void set_errorCallback(const EventCallback & ecb){
        m_errorCallback = ecb;
    }

    int get_fd() const {
        return m_fd;
    }

    int get_events() const {
        return m_events;
    }

    bool is_noneEvents() const {
        return m_events == k_noneEvent;
    }

    bool is_writeEvents() const {
        return m_events & k_writeEvent;
    }

    int get_revents() const {
        return m_revents;
    }

    void set_revents(int revents){
        m_revents = revents;
    }

    // for Poller
    int get_index() const {
        return m_index;
    }

    void set_index(int index){
        m_index = index;
    }

    EventLoop * get_ownerLoop() const {
        return m_loop;
    }

    void handleEvent(Timestamp receiveTime);

    void tie();

private:
    static const int k_noneEvent;   // 空事件常量
    static const int k_readEvent;   // 读事件常量
    static const int k_writeEvent;  // 写事件常量

    EventLoop * m_loop; // 记录所属的EventLoop对象
    const int m_fd;     // 负责的文件描述符，不负责关闭该fd
    int m_events;       // 关注的事件
    int m_revents;      // poll/epoll返回的事件
    int m_index;        // used by Poller

    ReadEventCallback m_readCallback;   // 读事件回调函数
    EventCallback m_writeCallback;      // 写事件回调函数
    EventCallback m_closeCallback;      // 连接关闭事件回调函数
    EventCallback m_errorCallback;      // 错误事件回调函数
};

#endif // CHANNEL_H_