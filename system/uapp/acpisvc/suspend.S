.comm x86_register_state, 128, 8

.section .text
.code64

.global x86_do_suspend
.type x86_do_suspend, STT_FUNC
x86_do_suspend:
    # TODO(teisenbe): We need to also save floating point registers
    mov %rdi, x86_register_state(%rip)
    leaq x86_register_state(%rip), %rdi
    mov %rsi, 8(%rdi)
    mov %rbp, 16(%rdi)
    mov %rbx, 24(%rdi)
    mov %rdx, 32(%rdi)
    mov %rcx, 40(%rdi)
    # Stash 0 as rax, so we return AE_OK if we do suspend
    movq $0, 48(%rdi)
    mov %r8, 56(%rdi)
    mov %r9, 64(%rdi)
    mov %r10, 72(%rdi)
    mov %r11, 80(%rdi)
    mov %r12, 88(%rdi)
    mov %r13, 96(%rdi)
    mov %r14, 104(%rdi)
    mov %r15, 112(%rdi)
    mov %rsp, 120(%rdi)

    # Transition to S3
    mov $3, %rdi
    sub $8, %rsp
    call AcpiEnterSleepState
    add $8, %rsp

.global x86_suspend_resume
x86_suspend_resume:
    leaq x86_register_state(%rip), %rdi
    mov 8(%rdi), %rsi
    mov 16(%rdi), %rbp
    mov 24(%rdi), %rbx
    mov 32(%rdi), %rdx
    mov 40(%rdi), %rcx
    mov 48(%rdi), %rax
    mov 56(%rdi), %r8
    mov 64(%rdi), %r9
    mov 72(%rdi), %r10
    mov 80(%rdi), %r11
    mov 88(%rdi), %r12
    mov 96(%rdi), %r13
    mov 104(%rdi), %r14
    mov 112(%rdi), %r15
    mov 120(%rdi), %rsp
    mov x86_register_state(%rip), %rdi

    ret
