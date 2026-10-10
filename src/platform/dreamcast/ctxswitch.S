! ctxswitch.S - SH-4 cooperative context switch (see ctxswitch.h).
!
! SH GCC ABI: r4 = first argument, r5 = second. Callee-saved: r8-r14, r15
! (sp), pr, fr12-fr15; fpscr is preserved across calls. mach/macl are saved
! too - harmless, and safe under -mrenesas where they are callee-saved.
! C symbols carry a leading underscore on sh-elf.

    .text

    .align  2
    .global _re1_ctx_swap
_re1_ctx_swap:
    ! ---- save current state into *r4 ----
    mov.l   r8,  @(0,r4)
    mov.l   r9,  @(4,r4)
    mov.l   r10, @(8,r4)
    mov.l   r11, @(12,r4)
    mov.l   r12, @(16,r4)
    mov.l   r13, @(20,r4)
    mov.l   r14, @(24,r4)
    mov.l   r15, @(28,r4)
    sts     pr, r0
    mov.l   r0,  @(32,r4)
    mov     r4,  r1
    add     #36, r1
    fmov.s  fr12, @r1
    add     #4,  r1
    fmov.s  fr13, @r1
    add     #4,  r1
    fmov.s  fr14, @r1
    add     #4,  r1
    fmov.s  fr15, @r1
    sts     fpscr, r0
    mov.l   r0,  @(52,r4)
    sts     mach, r0
    mov.l   r0,  @(56,r4)
    sts     macl, r0
    mov.l   r0,  @(60,r4)

    ! ---- load state from *r5 ----
    mov.l   @(0,r5),  r8
    mov.l   @(4,r5),  r9
    mov.l   @(8,r5),  r10
    mov.l   @(12,r5), r11
    mov.l   @(16,r5), r12
    mov.l   @(20,r5), r13
    mov.l   @(24,r5), r14
    mov.l   @(28,r5), r15
    mov.l   @(32,r5), r0
    lds     r0,  pr
    mov     r5,  r1
    add     #36, r1
    fmov.s  @r1+, fr12
    fmov.s  @r1+, fr13
    fmov.s  @r1+, fr14
    fmov.s  @r1+, fr15
    mov.l   @(56,r5), r0
    lds     r0,  mach
    mov.l   @(60,r5), r0
    lds     r0,  macl
    ! fpscr last: the fmov.s above must run in the mode they were saved in.
    mov.l   @(52,r5), r0
    lds     r0,  fpscr
    rts
    nop

    .align  2
    .global _re1_ctx_get_fpscr
_re1_ctx_get_fpscr:
    sts     fpscr, r0
    rts
    nop
