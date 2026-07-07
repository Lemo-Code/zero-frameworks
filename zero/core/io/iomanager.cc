/**
 * @file iomanager.cc
 * @brief I/O 管理器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "iomanager.h"
#include "zero/core/base/macro.h"
#include "zero/core/log/log.h"
#include "zero/core/util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

enum EpollCtlOp {
};

static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl) \
        case ctl: \
            return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default:
            return os << (int)op;
    }
#undef XX
}

static std::ostream& operator<< (std::ostream& os, EPOLL_EVENTS events) {
    if(!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E) \
    if(events & E) { \
        if(!first) { \
            os << "|"; \
        } \
        os << #E; \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch(event) {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            ZERO_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
    ctx.expire_at = 0;
    ctx.cancel_flag = nullptr;
}

void IOManager::onIoWaitAdded(uint64_t expire_at) {
    if(!expire_at) {
        return;
    }
    if(m_nextIoExpire == (uint64_t)-1 || expire_at < m_nextIoExpire) {
        m_nextIoExpire = expire_at;
        m_ioExpireDirty = false;
        tickle();
    }
}

void IOManager::onIoWaitRemoved(uint64_t expire_at) {
    if(expire_at && m_nextIoExpire == expire_at) {
        m_ioExpireDirty = true;
    }
}

void IOManager::refreshNextIoExpire() {
    uint64_t next = (uint64_t)-1;
    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        FdContext* fd_ctx = m_fdContexts[i];
        if(!fd_ctx) {
            continue;
        }
        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if(fd_ctx->read.expire_at &&
                (next == (uint64_t)-1 || fd_ctx->read.expire_at < next)) {
            next = fd_ctx->read.expire_at;
        }
        if(fd_ctx->write.expire_at &&
                (next == (uint64_t)-1 || fd_ctx->write.expire_at < next)) {
            next = fd_ctx->write.expire_at;
        }
    }
    m_nextIoExpire = next;
    m_ioExpireDirty = false;
}

uint64_t IOManager::getNextIoExpire() {
    if(m_ioExpireDirty) {
        refreshNextIoExpire();
    }
    if(m_nextIoExpire == (uint64_t)-1) {
        return (uint64_t)-1;
    }
    uint64_t now_ms = GetCurrentMS();
    if(now_ms >= m_nextIoExpire) {
        return 0;
    }
    return m_nextIoExpire - now_ms;
}

void IOManager::checkIoExpired() {
    if(m_ioExpireDirty) {
        refreshNextIoExpire();
    }
    uint64_t now_ms = GetCurrentMS();
    if(m_nextIoExpire == (uint64_t)-1 || now_ms < m_nextIoExpire) {
        return;
    }

    struct ExpiredItem {
        int fd;
        Event event;
        int* cancel_flag;
    };
    std::vector<ExpiredItem> expired;
    RWMutexType::ReadLock lock(m_mutex);
    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        FdContext* fd_ctx = m_fdContexts[i];
        if(!fd_ctx) {
            continue;
        }
        FdContext::MutexType::Lock lock2(fd_ctx->mutex);
        if((fd_ctx->events & READ) && fd_ctx->read.expire_at
                && fd_ctx->read.expire_at <= now_ms) {
            expired.push_back({fd_ctx->fd, READ, fd_ctx->read.cancel_flag});
        }
        if((fd_ctx->events & WRITE) && fd_ctx->write.expire_at
                && fd_ctx->write.expire_at <= now_ms) {
            expired.push_back({fd_ctx->fd, WRITE, fd_ctx->write.cancel_flag});
        }
    }
    lock.unlock();

    for(const auto& item : expired) {
        if(item.cancel_flag) {
            *item.cancel_flag = ETIMEDOUT;
        }
        cancelEvent(item.fd, item.event);
    }
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    //ZERO_LOG_INFO(g_logger) << "fd=" << fd
    //    << " triggerEvent event=" << event
    //    << " events=" << events;
    ZERO_ASSERT(events & event);
    //if(ZERO_UNLIKELY(!(event & event))) {
    //    return;
    //}
    events = (Event)(events & ~event);
    EventContext& ctx = getContext(event);
    int thread = (int)zero::GetThreadId();
    if(ctx.cb) {
        ctx.scheduler->schedule(&ctx.cb, thread);
    } else {
        ctx.scheduler->schedule(&ctx.fiber, thread);
    }
    ctx.scheduler = nullptr;
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    :Scheduler(threads, use_caller, name) {
    m_epfd = epoll_create(5000);
    ZERO_ASSERT(m_epfd > 0);

    int rt = pipe(m_tickleFds);
    ZERO_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    ZERO_ASSERT(!rt);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    ZERO_ASSERT(!rt);

    contextResize(32);

    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb,
                         uint64_t timeout_ms, int* cancel_flag) {
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(ZERO_UNLIKELY(fd_ctx->events & event)) {
        ZERO_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                    << " event=" << (EPOLL_EVENTS)event
                    << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        ZERO_ASSERT(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        ZERO_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events="
            << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    ++m_pendingEventCount;
    fd_ctx->events = (Event)(fd_ctx->events | event);
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    ZERO_ASSERT(!event_ctx.scheduler
                && !event_ctx.fiber
                && !event_ctx.cb);

    event_ctx.scheduler = Scheduler::GetThis();
    if(cb) {
        event_ctx.cb.swap(cb);
    } else {
        event_ctx.fiber = Fiber::GetThis();
        ZERO_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC
                      ,"state=" << event_ctx.fiber->getState());
    }

    uint64_t expire_at = 0;
    if(timeout_ms != (uint64_t)-1) {
        expire_at = GetCurrentMS() + timeout_ms;
    }
    event_ctx.expire_at = expire_at;
    event_ctx.cancel_flag = expire_at ? cancel_flag : nullptr;
    lock2.unlock();
    if(expire_at) {
        onIoWaitAdded(expire_at);
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(ZERO_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        ZERO_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --m_pendingEventCount;
    fd_ctx->events = new_events;
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    uint64_t expire_at = event_ctx.expire_at;
    fd_ctx->resetContext(event_ctx);
    lock2.unlock();
    if(expire_at) {
        onIoWaitRemoved(expire_at);
    }
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(ZERO_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        ZERO_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    uint64_t expire_at = event_ctx.expire_at;
    event_ctx.expire_at = 0;
    // Notify the waiting fiber that this I/O was deliberately cancelled.
    // checkIoExpired may have already set the flag to ETIMEDOUT — only
    // overwrite it when we are cancelling explicitly (flag still zero).
    if (event_ctx.cancel_flag && *event_ctx.cancel_flag == 0) {
        *event_ctx.cancel_flag = ECANCELED;
    }
    event_ctx.cancel_flag = nullptr;
    fd_ctx->triggerEvent(event);
    --m_pendingEventCount;
    lock2.unlock();
    if(expire_at) {
        onIoWaitRemoved(expire_at);
    }
    return true;
}

bool IOManager::cancelAll(int fd) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        ZERO_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    if(fd_ctx->events & READ) {
        FdContext::EventContext& read_ctx = fd_ctx->getContext(READ);
        uint64_t expire_at = read_ctx.expire_at;
        read_ctx.expire_at = 0;
        if (read_ctx.cancel_flag && *read_ctx.cancel_flag == 0) {
            *read_ctx.cancel_flag = ECANCELED;
        }
        read_ctx.cancel_flag = nullptr;
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
        if(expire_at) {
            onIoWaitRemoved(expire_at);
        }
    }
    if(fd_ctx->events & WRITE) {
        FdContext::EventContext& write_ctx = fd_ctx->getContext(WRITE);
        uint64_t expire_at = write_ctx.expire_at;
        write_ctx.expire_at = 0;
        if (write_ctx.cancel_flag && *write_ctx.cancel_flag == 0) {
            *write_ctx.cancel_flag = ECANCELED;
        }
        write_ctx.cancel_flag = nullptr;
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
        if(expire_at) {
            onIoWaitRemoved(expire_at);
        }
    }

    ZERO_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager* IOManager::GetThis() {
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

void IOManager::tickle() {
    if(!hasIdleThreads()) {
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);
    ZERO_ASSERT(rt == 1);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    uint64_t io_timeout = getNextIoExpire();
    if(timeout == (uint64_t)-1) {
        timeout = io_timeout;
    } else if(io_timeout != (uint64_t)-1) {
        timeout = std::min(timeout, io_timeout);
    }
    return timeout == (uint64_t)-1
        && m_pendingEventCount == 0
        && Scheduler::stopping();

}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

void IOManager::idle() {
    ZERO_LOG_DEBUG(g_logger) << "idle";
    const uint64_t MAX_EVNETS = 256;
    epoll_event* events = new epoll_event[MAX_EVNETS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
        delete[] ptr;
    });

    while(true) {
        uint64_t next_timeout = 0;
        if(ZERO_UNLIKELY(stopping(next_timeout))) {
            ZERO_LOG_INFO(g_logger) << "name=" << getName()
                                     << " idle stopping exit";
            break;
        }

        int rt = 0;
        do {
            static const int MAX_TIMEOUT = 3000;
            if(next_timeout != ~0ull) {
                next_timeout = (int)next_timeout > MAX_TIMEOUT
                                ? MAX_TIMEOUT : next_timeout;
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            if(rt < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while(true);

        std::vector<std::function<void()> > cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            //ZERO_LOG_DEBUG(g_logger) << "on timer cbs.size=" << cbs.size();
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        //if(ZERO_UNLIKELY(rt == MAX_EVNETS)) {
        //    ZERO_LOG_INFO(g_logger) << "epoll wait events=" << rt;
        //}

        for(int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];
            if(event.data.fd == m_tickleFds[0]) {
                uint8_t dummy[256];
                while(read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            if(event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if(event.events & EPOLLIN) {
                real_events |= READ;
            }
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if(rt2) {
                ZERO_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                    << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            //ZERO_LOG_INFO(g_logger) << " fd=" << fd_ctx->fd << " events=" << fd_ctx->events
            //                         << " real_events=" << real_events;
            if(real_events & READ) {
                FdContext::EventContext& read_ctx = fd_ctx->getContext(READ);
                uint64_t expire_at = read_ctx.expire_at;
                read_ctx.expire_at = 0;
                read_ctx.cancel_flag = nullptr;
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
                if(expire_at) {
                    onIoWaitRemoved(expire_at);
                }
            }
            if(real_events & WRITE) {
                FdContext::EventContext& write_ctx = fd_ctx->getContext(WRITE);
                uint64_t expire_at = write_ctx.expire_at;
                write_ctx.expire_at = 0;
                write_ctx.cancel_flag = nullptr;
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
                if(expire_at) {
                    onIoWaitRemoved(expire_at);
                }
            }
        }

        if(rt <= 0) {
            checkIoExpired();
        }

        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->swapOut();
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}
