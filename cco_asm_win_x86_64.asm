format MS64 COFF

public cco_save_regs
public cco_save_stack
public cco_load_stack
public cco_yield_return
public cco_get_yield_return

section '.text' code readable executable

;; void cco_save_regs(uint64_t* sp, uint64_t* bp);
cco_save_regs:
    ; int3
    ;; rsp now points to the return address
    pop rax
    ;; rbp now is the stackframe of cco_yield_impl
    ;; rbp+8 would point to rbp of the function that called cco_yield_impl
    ;; rbp+16 would point to the top of the stack of the function that called cco_yield_impl
    mov rbx, [rbp] ;; previous stackframe
    mov rcx, rbp
    add rcx, 16 ;; top of the previous stackframe
    mov QWORD [rcx], rcx ;; previous rsp
    mov QWORD [rdx], rbx ;; previous rbp
    jmp rax

;; void cco_load_regs(uint64_t sp, uint64_t bp);
cco_load_regs:
    ; int3
    mov rsp, rdx ;; load rsp
    mov rbp, rcx ;; load rbp
    ret

;; void cco_save_stack(char* into, uint32_t bytes);
cco_save_stack:
    ; int3
    mov rax, rsp ;; stack pointer of the previous frame
    sub rax, 8   ;; this is where the new stackframe would start
    mov rdi, rcx ;; dest
    mov ecx, edx ;; counter
    mov rsi, rax ;; source
    std
    rep movsb
    cld
    ;; we do not pop rbp, as it was never pushed
    ret

;; void cco_load_stack(char* from, uint32_t bytes);
cco_load_stack:
    ; int3
    mov rax, rsp ;; stack pointer of the previous frame
    sub rax, 8   ;; this is where the new stackframe would start
    mov rsi, rcx ;; source
    mov ecx, edx ;; counter
    mov rdi, rax ;; dest -> where the current stackframe base would be
    std
    rep movsb
    cld
    ;; we do not pop rbp, as it was never pushed
    ret

;; long cco_get_yield_return();
cco_get_yield_return:
    ; int3
    mov rax, QWORD [rbp+8]
    ret

;; void cco_yield_return(uint64_t sp, uint64_t bp, void* ret);
cco_yield_return:
    ; int3
    mov rsp, rcx ;; load rsp
    mov rbp, rdx ;; load rbp
    jmp r8 ;; jump back into the suspended procedure
