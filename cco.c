#include "cco.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// save the state of the stack pointer and the base pointer into sp and bp
extern void cco_save_regs(uint64_t* sp, uint64_t* bp);

// copy N bytes of the stack into dest
extern void cco_save_stack(char* dest, uint32_t N);
// load N bytes of stack from src
extern void cco_load_stack(char* src, uint32_t N);

// load the stack pointer and base pointer of a suspened procedure
// then jump into it via ret
extern void cco_yield_return(uint64_t sp, uint64_t bp, void* ret);
// get the return address for a suspended procedure
extern uint64_t cco_get_yield_return(void);

#define this_proc(context) (context->procs[context->c_id - 1])

typedef char cco_co_stackframe_t[CCO_CO_STACKF_SIZE];

typedef struct cco_co_regs_t {
    uint64_t sp, bp;
} cco_co_regs_t;

typedef struct cco_Message {
    cco_message_t data;
    int from_id;
    int to_id;
    struct cco_Message* prev;
    struct cco_Message* next;
} cco_Message;

typedef enum cco_ProcFlags {
    cco_MESSAGE_PENDING = 1,
    cco_AWAITING_MESSAGE = 1 << 1,
} cco_ProcFlags;

typedef struct MessageQueue {
    cco_Message* head;
    cco_Message* tail;
} cco_MessageQueue;

typedef struct cco_Process {
    cco_Coroutine co;
    // int flags;
    // the pending delivered message, NULL if none
    cco_Message* pending_msg;
    // where the scheduler should write a delivered message, NULL if nowhere
    // char* msg_recv;
    // next and previous process in the queue
    struct cco_Process *next, *prev;
    // id of the process, used for message passing
    int id;
    // stackframe of the coroutine right after yielding
    cco_co_stackframe_t stackf;
    // saved register state of the coroutine, before yielding
    cco_co_regs_t regs;
    // return address for yield call
    void* yield_return;
} cco_Process;

typedef struct cco_ProcQueue {
    cco_Process* head;
    cco_Process* tail;
} cco_ProcQueue;

typedef struct cco_Sched {
    cco_MessageQueue mq;
    cco_Process** procs;
    int proc_count, proc_cap;
    cco_ProcQueue pqueue;
} cco_Sched;

typedef struct Context {
    int c_id;
    jmp_buf* restart;
    cco_MessageQueue* msg_queue;
    cco_Process** procs;
    cco_Sched* sched;
} cco_Context;

static void panic(char* fmt, ...)
{
    va_list args = { 0 };
    va_start(args, fmt);
    fprintf(stderr, "cco panic: ");
    fprintf(stderr, fmt, args);
    va_end(args);
    abort();
}

static void cco_block(cco_Coroutine* co, cco_Ctx_p ctx);

static void cco_append_msg(cco_MessageQueue* queue, int from, int to, cco_Message* msg)
{
    msg->from_id = from;
    msg->to_id = to;
    if (!queue->head) {
        queue->head = msg;
        queue->tail = msg;
        queue->head->next = NULL;
        queue->head->prev = NULL;
    } else {
        queue->tail->next = msg;
        msg->prev = queue->tail;
        queue->tail = msg;
        msg->next = NULL;
    }
}
// try to recieve a message into out, returns 1 on success and 0 if there is no
// pending message, for the process
static int cco_recv_impl(cco_Process* proc, cco_message_t out)
{
    if (proc->pending_msg != NULL) {
        memcpy(out,
            proc->pending_msg->data,
            CCO_MESSAGE_SIZE);
        free(proc->pending_msg);
        proc->pending_msg = NULL;
        return 1;
    } else
        return 0;
}
// block while waiting for a message
void cco_recv(cco_Coroutine* self, cco_Ctx_p ctx, cco_message_t out)
{
    cco_Context* context = (cco_Context*)ctx;
    cco_Process* proc = context->procs[context->c_id - 1];
    int received = cco_recv_impl(proc, out);
    if (!received) {
        cco_block(self, ctx);
        cco_recv_impl(proc, out);
    }
}

static void cco_append_proc(cco_ProcQueue* queue, cco_Process* proc)
{
    if (!queue->head) {
        queue->head = proc;
        queue->tail = proc;
        queue->head->next = NULL;
        queue->head->prev = NULL;
    } else {
        queue->tail->next = proc;
        proc->prev = queue->tail;
        queue->tail = proc;
        proc->next = NULL;
    }
}
static cco_Process* cco_pop_proc(cco_ProcQueue* queue)
{
    if (!queue->head) {
        return NULL;
    }
    cco_Process* pop = queue->head;
    queue->head = queue->head->next;
    return pop;
}
static cco_Message* cco_pop_msg(cco_MessageQueue* queue)
{
    if (!queue->head) {
        return NULL;
    }
    cco_Message* pop = queue->head;
    queue->head = queue->head->next;
    return pop;
}
#define is_blocked(proc) (proc->co.c_state == cco_BLOCKED)
#define is_ready(proc) (proc->co.c_state == cco_READY)

static void cco_deliver_messages(
    cco_MessageQueue* mq,
    cco_Process** procs,
    int procs_sz)
{
    cco_Message* delay_stack[CCO_DELAY_STACK_SIZE] = { };
    int dp = -1;
    while (mq->head) {
        cco_Message* msg = cco_pop_msg(mq);
        if (msg->to_id < 1 || msg->to_id > procs_sz) {
            panic("message recipent of invalid process id (%d)\n", msg->to_id);
        }
        if (msg->from_id < 1 || msg->from_id > procs_sz) {
            panic("message author of invalid process id (%d)\n", msg->from_id);
        }
        cco_Process* to = procs[msg->to_id - 1];
        cco_Process* from = procs[msg->from_id - 1];
        if (to->pending_msg || (!is_blocked(to) || !is_blocked(from))) {
            if (dp >= CCO_CO_STACKF_SIZE)
                panic("delayed message buffer overflow, buffer size is %d",
                    CCO_DELAY_STACK_SIZE);
            delay_stack[++dp] = msg;
            continue;
        } else {
            to->pending_msg = msg;
        }
        to->co.c_state = cco_READY;
        from->co.c_state = cco_READY;
    }
    while (dp >= 0) {
        cco_Message* delayed = delay_stack[dp--];
        cco_append_msg(mq, delayed->from_id, delayed->to_id, delayed);
    }
}
static void cco_Process_free(cco_Process* proc)
{
    if (proc->pending_msg)
        free(proc->pending_msg);
    if (proc->co.state)
        free(proc->co.state);
}

cco_Sched* cco_Sched_new()
{
    cco_Sched* ret = calloc(1, sizeof(cco_Sched));
    ret->mq = (cco_MessageQueue) { };
    ret->procs = (cco_Process**)calloc(CCO_SCHED_INIT_PROC_COUNT, sizeof(cco_Process*));
    ret->proc_count = 0;
    ret->proc_cap = CCO_SCHED_INIT_PROC_COUNT;
    ret->pqueue = (cco_ProcQueue) { };
    return ret;
}
cco_Co_handle cco_Sched_add_coroutine(cco_Sched* sched, cco_Coroutine co)
{
    if (sched->proc_count + 1 > sched->proc_cap) {
        int new_cap = sched->proc_cap * 2;
        int old_cap = sched->proc_cap;
        sched->proc_cap = new_cap;
        cco_Process** tmp = (cco_Process**)calloc(new_cap, sizeof(cco_Process*));
        memcpy(
            tmp,
            sched->procs,
            old_cap * sizeof(cco_Process*));
        free(sched->procs);
        sched->procs = tmp;
    }
    cco_Process* proc = (cco_Process*)calloc(1, sizeof(cco_Process));
    *proc = (cco_Process) { };
    proc->co = co;
    sched->procs[sched->proc_count++] = proc;
    proc->id = sched->proc_count;
    // for testing stack layout
    // memset(proc->stackf, 69, CCO_CO_STACKF_SIZE);
    cco_append_proc(&sched->pqueue, proc);
    return (void*)(long)proc->id;
}

void cco_Sched_free(cco_Sched* sched)
{
    for (int i = 0; i < sched->proc_count; i++) {
        cco_Process* proc = sched->procs[i];
        cco_Process_free(proc);
    }
    free(sched->procs);
    cco_Message* msg = cco_pop_msg(&sched->mq);
    while (msg) {
        free(msg);
        msg = cco_pop_msg(&sched->mq);
    }
    free(sched);
}
static void cco_store(cco_Process* proc)
{
    // since the stack grows downward, we need a pointer to the end
    // of the stackf buffer, otherwise we will wirte to fuck knows where
    cco_save_stack(proc->stackf + CCO_CO_STACKF_SIZE,
        CCO_CO_STACKF_SIZE);
}
static void cco_stage(cco_Process* proc, cco_Context* ctx)
{
    if (proc->yield_return) {
        cco_load_stack(proc->stackf + CCO_CO_STACKF_SIZE,
            CCO_CO_STACKF_SIZE);
        cco_yield_return(proc->regs.sp, proc->regs.bp, proc->yield_return);
    } else {
        // // for testing stack layout
        // cco_load_stack(proc->stackf + CCO_CO_STACKF_SIZE,
        //     CCO_CO_STACKF_SIZE);
        proc->co.start(
            &proc->co,
            ctx);
    }
}
void cco_Sched_run(cco_Sched* sched)
{
    jmp_buf jmp_buffer = { };
    cco_Context* ctx = calloc(1, sizeof(cco_Context));
    ctx->restart = &jmp_buffer;
    ctx->msg_queue = &sched->mq;
    ctx->procs = sched->procs;
    ctx->sched = sched;
    while (1) {
        int ret = setjmp(jmp_buffer);
        if (ret != 0) {
            if (ret < 1 || ret > sched->proc_count){
                panic("yielding coroutine of invalid id (%d)\n", ret);
            }
            cco_store(sched->procs[ret - 1]);
            cco_append_proc(&sched->pqueue, sched->procs[ret - 1]);
        }
        cco_deliver_messages(&sched->mq, sched->procs, sched->proc_count);
        cco_Process* next = { };
        next = cco_pop_proc(&sched->pqueue);
        if (!next) {
            break;
        }
        if (next->co.c_state == cco_BLOCKED) {
            cco_append_proc(&sched->pqueue, next);
            continue;
        }
        if (next->co.c_state == cco_DONE) {
            int _this = next->id - 1;
            int _last = sched->proc_count - 1;
            sched->procs[_last]->id = next->id;
            sched->procs[_this] = sched->procs[_last];
            sched->procs[_last] = NULL;
            sched->proc_count--;
            cco_Process_free(next);
            continue;
        }
        ctx->c_id = next->id;
        cco_stage(next, ctx);
    }
    free(ctx);
}
// this is a macro so that we can preserve the calling stackframe
// without creating a new one on top of it.
// This code will execute inside the stackframe of cco_block, cco_yield and cco_return.
// The external assembly written instructions depend on that being the case
// the yielding subroutine must be immediately below the yield function.
#define cco_yield_impl(co, ctx)                      \
    cco_Context* context = (cco_Context*)ctx;        \
    cco_Process* p = this_proc(context);             \
    cco_save_regs(                                   \
        &p->regs.sp,                                 \
        &p->regs.bp);                                \
    p->yield_return = (void*)cco_get_yield_return(); \
    longjmp(*context->restart, context->c_id)

static void cco_block(cco_Coroutine* co, cco_Ctx_p ctx)
{
    co->c_state = cco_BLOCKED;
    cco_yield_impl(co, ctx);
}

void cco_yield(cco_Coroutine* co, cco_Ctx_p ctx)
{
    co->c_state = cco_READY;
    cco_yield_impl(co, ctx);
}

void cco_return(cco_Coroutine* co, cco_Ctx_p ctx)
{
    co->c_state = cco_DONE;
    cco_yield_impl(co, ctx);
}
void cco_send(cco_Coroutine* self, cco_Ctx_p ctx, cco_Co_handle co, cco_message_t msg)
{
    cco_Context* context = (cco_Context*)ctx;
    cco_Message* new_msg = (cco_Message*)calloc(1, sizeof(cco_Message));
    memcpy(
        new_msg->data,
        msg,
        CCO_MESSAGE_SIZE);
    int recipent_id = (long)co;
    cco_append_msg(context->msg_queue, context->c_id, recipent_id, new_msg);
    cco_block(self, context);
}
cco_Co_handle cco_spawn(cco_Ctx_p ctx, cco_Coroutine spawn)
{
    cco_Context* context = (cco_Context*)ctx;
    return cco_Sched_add_coroutine(context->sched, spawn);
}
