/**
 * @file fiber_stack.h
 * @brief 协程栈定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_FIBER_STACK_H__
#define __ZERO_FIBER_STACK_H__

#include <stddef.h>

namespace zero {

void* FiberStackAlloc(size_t size);
void FiberStackDealloc(void* sp, size_t size);

} // namespace zero

#endif
