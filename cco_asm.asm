format ELF64

public cco_save_regs
public cco_load_regs
public cco_save_stack
public cco_load_stack
public cco_yield_return
public cco_save_yield_return
public cco_get_yield_return

section '.text' executable

;; void cco_save_regs(uint64_t* sp, uint64_t* bp);
cco_save_regs:
    ; int3
    mov QWORD [rdi], rsp
    mov QWORD [rsi], rbp
    ret

;; void cco_load_regs(uint64_t sp, uint64_t bp);
cco_load_regs:
    ; int3
    mov rsp, rdi
    mov rbp, rsi
    ret

;; void cco_save_stack(char* into, uint32_t bytes);
cco_save_stack:
    ; int3
    mov rax, rsp ;; stack pointer of the previous frame
    sub rax, 8   ;; this is where the new stackframe would start
    mov ecx, esi ;; counter
    mov rdi, rdi ;; dest
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
    mov ecx, esi ;; counter
    mov rsi, rdi ;; source
    mov rdi, rax ;; dest -> where the current stackframe base would be
    std
    rep movsb
    cld
    ;; we do not pop rbp, as it was never pushed
    ret

;; void cco_save_yield_return(void** into);
cco_save_yield_return:
    ; int3
    mov rax, QWORD [rsp+8]
    mov QWORD [rsi], rax
    xor rax, rax
    ret

;; long cco_get_yield_return();
cco_get_yield_return:
    ; int3
    mov rax, QWORD [rbp+8]
    ret

;; void cco_yield_return(void* ret);
cco_yield_return:
    ; int3
    ; push rdi
    ; ret
    jmp rdi
