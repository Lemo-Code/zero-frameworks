/**
 * @file worker.h
 * @brief Worker 线程定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_WORKER_H__
#define __ZERO_WORKER_H__

#include "mutex.h"
#include "zero/core/base/singleton.h"
#include "zero/core/log/log.h"
#include "zero/core/io/iomanager.h"

namespace zero {

class WorkerGroup : Noncopyable, public std::enable_shared_from_this<WorkerGroup> {
public:
    typedef std::shared_ptr<WorkerGroup> ptr;
    static WorkerGroup::ptr Create(uint32_t batch_size, zero::Scheduler* s = zero::Scheduler::GetThis()) {
        return std::make_shared<WorkerGroup>(batch_size, s);
    }

    WorkerGroup(uint32_t batch_size, zero::Scheduler* s = zero::Scheduler::GetThis());
    ~WorkerGroup();

    void schedule(std::function<void()> cb, int thread = -1);
    void waitAll();
private:
    void doWork(std::function<void()> cb);
private:
    uint32_t m_batchSize;
    bool m_finish;
    Scheduler* m_scheduler;
    FiberSemaphore m_sem;
};

class WorkerManager {
public:
    WorkerManager();
    void add(Scheduler::ptr s);
    Scheduler::ptr get(const std::string& name);
    IOManager::ptr getAsIOManager(const std::string& name);

    template<class FiberOrCb>
    void schedule(const std::string& name, FiberOrCb fc, int thread = -1) {
        auto s = get(name);
        if(s) {
            s->schedule(fc, thread);
        } else {
            static zero::Logger::ptr s_logger = ZERO_LOG_NAME("system");
            ZERO_LOG_ERROR(s_logger) << "schedule name=" << name
                << " not exists";
        }
    }

    template<class Iter>
    void schedule(const std::string& name, Iter begin, Iter end) {
        auto s = get(name);
        if(s) {
            s->schedule(begin, end);
        } else {
            static zero::Logger::ptr s_logger = ZERO_LOG_NAME("system");
            ZERO_LOG_ERROR(s_logger) << "schedule name=" << name
                << " not exists";
        }
    }

    bool init();
    bool init(const std::map<std::string, std::map<std::string, std::string> >& v);
    void stop();

    bool isStoped() const { return m_stop;}
    std::ostream& dump(std::ostream& os);

    uint32_t getCount();
private:
    std::map<std::string, std::vector<Scheduler::ptr> > m_datas;
    bool m_stop;
};

typedef zero::Singleton<WorkerManager> WorkerMgr;

}

#endif
