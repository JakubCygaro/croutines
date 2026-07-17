#include "cco.h"

typedef struct cco_Process {
    cco_Coroutine co;
    int flags;
    cco_Message* msg;
    char* recv_buf;
    struct cco_Process *next, *prev;
    int id;
} cco_Process;

typedef struct cco_ProcQueue {
    cco_Process* head;
    cco_Process* tail;
} cco_ProcQueue;

typedef struct MessageQueue {
    cco_Message* head;
    cco_Message* tail;
} cco_MessageQueue;

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
static void cco_recv_impl(cco_Coroutine* self, cco_Process* proc, cco_message_t out)
{
    proc->recv_buf = out;
    if ((proc->flags & cco_MESSAGE_PENDING) == cco_MESSAGE_PENDING || proc->msg != NULL) {
        memcpy(proc->recv_buf,
            proc->msg->data,
            CCO_MESSAGE_SIZE);
        self->c_state = cco_READY;
        free(proc->msg);
        proc->msg = NULL;
        proc->recv_buf = NULL;
    } else {
        proc->flags |= cco_AWAITING_MESSAGE;
        self->c_state = cco_BLOCKED;
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

static void cco_deliver_messages(
    cco_MessageQueue* mq,
    cco_Process** procs,
    int procs_sz)
{
    cco_Message* delay_stack[CCO_DELAY_STACK_SIZE] = { };
    int dp = -1;
    while (mq->head) {
        cco_Message* msg = cco_pop_msg(mq);
        cco_Process* to = procs[msg->to_id - 1];
        cco_Process* from = procs[msg->from_id - 1];
        if (to->msg) {
            delay_stack[++dp] = msg;
            continue;
        } else {
            to->msg = msg;
        }
        to->co.c_state = cco_READY;
        from->co.c_state = cco_READY;
        if (to->recv_buf) {
            cco_recv_impl(&to->co, to, to->recv_buf);
        }
    }
    while (dp >= 0) {
        cco_Message* delayed = delay_stack[--dp];
        cco_append_msg(mq, delayed->from_id, delayed->to_id, delayed);
    }
}
static void cco_Process_free(cco_Process* proc)
{
    if (proc->msg)
        free(proc->msg);
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
    cco_Process* proc = (cco_Process*)malloc(sizeof(cco_Process));
    *proc = (cco_Process) { };
    proc->co = co;
    sched->procs[sched->proc_count++] = proc;
    proc->id = sched->proc_count;
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
void cco_Sched_run(cco_Sched* sched)
{
    jmp_buf jmp_buffer = { };
    while (1) {
        int ret = setjmp(jmp_buffer);
        if (ret != 0) {
            cco_append_proc(&sched->pqueue, sched->procs[ret - 1]);
        }
        cco_deliver_messages(&sched->mq, sched->procs, sched->proc_count);
        cco_Process* next = { };
        next = cco_pop_proc(&sched->pqueue);
        if (!next) {
            break;
        }
        if (next->co.c_state == cco_BLOCKED) {
            if ((next->flags & cco_MESSAGE_PENDING) != cco_MESSAGE_PENDING) {
                cco_append_proc(&sched->pqueue, next);
                continue;
            }
        }
        if (next->co.c_state == cco_DONE) {
            int _this = next->id - 1;
            int _last = sched->proc_count - 1;
            sched->procs[_this] = sched->procs[_last];
            sched->procs[_last] = NULL;
            sched->proc_count--;
            cco_Process_free(next);
            continue;
        }
        cco_Context ctx = {
            .c_id = next->id,
            .restart = &jmp_buffer,
            .msg_queue = &sched->mq,
            .procs = sched->procs,
            .sched = sched,
        };
        next->co.poll(
            &next->co,
            &ctx);
    }
}
void cco_recv(cco_Coroutine* self, cco_Ctx_p ctx, cco_message_t out)
{
    cco_Context* context = (cco_Context*)ctx;
    cco_Process* proc = context->procs[context->c_id - 1];
    cco_recv_impl(self, proc, out);
    cco_yield(self, ctx);
    // longjmp(*context->restart, context->c_id);
}
void cco_yield_impl(cco_Coroutine* co, cco_Ctx_p ctx)
{
    cco_Context* context = (cco_Context*)ctx;
    longjmp(*context->restart, context->c_id);
}
void cco_send(cco_Coroutine* self, cco_Ctx_p ctx, cco_Co_handle co, cco_message_t msg)
{
    cco_Context* context = (cco_Context*)ctx;
    self->c_state = cco_BLOCKED;

    cco_Message* new_msg = (cco_Message*)calloc(1, sizeof(cco_Message));
    memcpy(
        new_msg->data,
        msg,
        CCO_MESSAGE_SIZE);
    int recipent_id = (long)co;
    cco_append_msg(context->msg_queue, context->c_id, recipent_id, new_msg);
    context->procs[recipent_id - 1]->flags |= cco_MESSAGE_PENDING;
    longjmp(*context->restart, context->c_id);
}
cco_Co_handle cco_spawn(cco_Ctx_p ctx, cco_Coroutine spawn)
{
    cco_Context* context = (cco_Context*)ctx;
    return cco_Sched_add_coroutine(context->sched, spawn);
}
