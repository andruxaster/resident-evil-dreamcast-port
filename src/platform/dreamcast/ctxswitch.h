// ctxswitch.h - minimal SH-4 cooperative context switch for the task scheduler.
//
// Layout is shared with ctxswitch.S; keep the offsets in sync.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct re1_ctx {
    uint32_t r[8];      // 0x00  r8..r15 (r15 = sp)
    uint32_t pr;        // 0x20  return address = resume point
    uint32_t fr[4];     // 0x24  fr12..fr15
    uint32_t fpscr;     // 0x34
    uint32_t mach;      // 0x38
    uint32_t macl;      // 0x3C
} re1_ctx_t;            // 0x40

// Save the current callee-saved state into *save, load *load and continue
// there. Returns when something swaps back into *save.
void re1_ctx_swap(re1_ctx_t* save, const re1_ctx_t* load);

// Current FPSCR, so a new context starts in the same FPU mode (PR/SZ bits).
uint32_t re1_ctx_get_fpscr(void);

// KallistiOS asserts on every reschedule that the running thread's sp is not
// below kthread_t::stack ("Thread stack underrun"). Task stacks live in the
// heap, below the kernel thread's stack, so every switch must tell KOS which
// stack it is moving to. begin disables IRQs and sets the bounds (NULL base =
// back to the kernel thread's own stack); end restores the IRQ state. Both
// halves must bracket re1_ctx_swap, so no interrupt can see sp and the bounds
// disagree. Implemented in platform.cpp.
uint32_t re1_ctx_begin_switch(void* stackBase, uint32_t stackSize);
void     re1_ctx_end_switch(uint32_t irqState);

// Prepare *ctx so that swapping into it calls entry() on the given stack.
// entry must never return.
static inline void re1_ctx_init(re1_ctx_t* ctx, void* stackTop, void (*entry)(void))
{
    for (int i = 0; i < 8; ++i) ctx->r[i] = 0;
    for (int i = 0; i < 4; ++i) ctx->fr[i] = 0;
    ctx->r[7]  = (uint32_t)(uintptr_t)stackTop;     // r15
    ctx->r[6]  = 0;                                 // r14 (frame pointer)
    ctx->pr    = (uint32_t)(uintptr_t)entry;
    ctx->fpscr = re1_ctx_get_fpscr();
    ctx->mach  = 0;
    ctx->macl  = 0;
}

#ifdef __cplusplus
}
#endif
