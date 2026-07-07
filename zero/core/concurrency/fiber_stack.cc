/**
 * @file fiber_stack.cc
 * @brief 协程栈实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "fiber_stack.h"
#include "zero/core/config/config.h"

#include <sys/mman.h>
#include <unistd.h>

#include <vector>

namespace zero {

static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

static ConfigVar<uint32_t>::ptr g_fiber_stack_cache_max =
    Config::Lookup<uint32_t>("fiber.stack.cache_max", 64, "fiber stack freelist max per thread");

static size_t PageSize() {
    static const size_t kPageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return kPageSize;
}

static size_t MappedBytes(size_t stack_size) {
    const size_t page_size = PageSize();
    const size_t usable = (stack_size + page_size - 1) / page_size * page_size;
    return usable + page_size;
}

static void* MmapStack(size_t stack_size) {
    const size_t page_size = PageSize();
    const size_t mapped = MappedBytes(stack_size);
    void* base = ::mmap(nullptr, mapped, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(base == MAP_FAILED) {
        return nullptr;
    }
    if(::mprotect(base, page_size, PROT_NONE) != 0) {
        ::munmap(base, mapped);
        return nullptr;
    }
    return static_cast<char*>(base) + page_size;
}

static void MunmapStack(void* sp, size_t stack_size) {
    const size_t page_size = PageSize();
    void* base = static_cast<char*>(sp) - page_size;
    ::munmap(base, MappedBytes(stack_size));
}

static thread_local std::vector<void*> t_freelist;

void* FiberStackAlloc(size_t size) {
    const uint32_t default_size = g_fiber_stack_size->getValue();
    if(size == default_size && !t_freelist.empty()) {
        void* sp = t_freelist.back();
        t_freelist.pop_back();
        return sp;
    }
    return MmapStack(size);
}

void FiberStackDealloc(void* sp, size_t size) {
    if(!sp) {
        return;
    }
    const uint32_t default_size = g_fiber_stack_size->getValue();
    const uint32_t cache_max = g_fiber_stack_cache_max->getValue();
    if(size == default_size && t_freelist.size() < cache_max) {
        t_freelist.push_back(sp);
        return;
    }
    MunmapStack(sp, size);
}

} // namespace zero
