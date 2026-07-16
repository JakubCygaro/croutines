#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum CState {
    READY,
    BLOCKED,
    DONE,
} CState;

typedef struct Coroutine Coroutine;

typedef void* Ctx_p;

typedef void (*poll_fn)(Coroutine* self, Ctx_p ctx);

typedef struct Coroutine {
    void* state;
    CState c_state;
    poll_fn poll;
} Coroutine;

typedef struct Message {
    char data[128];
    int from_id;
    int to_id;
    struct Message* prev;
    struct Message* next;
} Message;

typedef enum ProcFlags {
    MESSAGE_PENDING = 1,
    AWAITING_MESSAGE = 1 << 1,
} ProcFlags;
typedef struct Process {
    Coroutine co;
    int flags;
    Message* msg;
    char* recv_buf;
    struct Process *next, *prev;
    int id;
} Process;

typedef struct MessageQueue {
    Message* head;
    Message* tail;
} MessageQueue;

typedef struct Context {
    int c_id;
    jmp_buf* restart;
    MessageQueue* msg_queue;
    Process* procs;
} Context;

void append_msg(MessageQueue* queue, int from, int to, char msg[128])
{
    Message* new = calloc(1, sizeof(Message));
    memcpy(
        new->data,
        msg,
        128);
    new->from_id = from;
    new->to_id = to;
    if (!queue->head) {
        queue->head = new;
        queue->tail = new;
        queue->head->next = NULL;
        queue->head->prev = NULL;
    } else {
        queue->tail->next = new;
        new->prev = queue->tail;
        queue->tail = new;
        new->next = NULL;
    }
}

void send(Coroutine* self, Ctx_p ctx, int recipent_id, char msg[128])
{
    Context* context = (Context*)ctx;
    self->c_state = BLOCKED;

    append_msg(context->msg_queue, context->c_id, recipent_id, msg);
    context->procs[recipent_id - 1].flags |= MESSAGE_PENDING;
    longjmp(*context->restart, context->c_id);
}
void recv_impl(Coroutine* self, Process* proc, char out[128])
{
    proc->recv_buf = out;
    if ((proc->flags & MESSAGE_PENDING) == MESSAGE_PENDING || proc->msg != NULL) {
        memcpy(proc->recv_buf,
            proc->msg->data,
            128);
        self->c_state = READY;
        free(proc->msg);
        proc->msg = NULL;
        proc->recv_buf = NULL;
    } else {
        proc->flags |= AWAITING_MESSAGE;
        self->c_state = BLOCKED;
    }
}
void recv(Coroutine* self, Ctx_p ctx, char out[128])
{
    Context* context = (Context*)ctx;
    Process* proc = &context->procs[context->c_id - 1];
    recv_impl(self, proc, out);
    longjmp(*context->restart, context->c_id);
}

typedef enum SenderStateEnum {
    SBeforeSent,
    SAfterSent,
    SAfterResponse,
} SenderStateEnum;

typedef struct SenderState {
    SenderStateEnum sstate;
    char* buf;
} SenderState;

void sender(Coroutine* self, Ctx_p ctx)
{
    SenderState* state = (SenderState*)self->state;
    char msg[128] = "Hello, World!";
    switch (state->sstate) {
    case SBeforeSent:
        printf("SENDER => Sending message: '%s'\n", msg);
        state->sstate = SAfterSent;
        send(self, ctx, 2, msg);
        break;
    case SAfterSent:
        printf("SENDER => Awaiting response\n");
        state->sstate = SAfterResponse;
        recv(self, ctx, state->buf);
        break;
    case SAfterResponse:
        printf("SENDER => Got response: '%s'\n", state->buf);
        printf("SENDER => DONE\n");
        self->c_state = DONE;
        break;
    }
}

Coroutine make_sender()
{
    SenderState* state = calloc(1, sizeof(SenderState));
    state->buf = calloc(128, sizeof(char));
    state->sstate = SBeforeSent;
    return (Coroutine) {
        .c_state = READY,
        .state = state,
        .poll = sender,
    };
}
typedef enum ReceiverStateEnum {
    RBeforeReceive,
    RAfterReceive,
    RAfterRespond,
} ReceiverStateEnum;

typedef struct ReceiverState {
    ReceiverStateEnum state_enum;
    char* buf;
} ReceiverState;

void receiver(Coroutine* self, Ctx_p ctx)
{
    ReceiverState* state = self->state;
    char resp[128] = "This is a response message";
    switch (state->state_enum) {
    case RBeforeReceive:
        state->state_enum = RAfterReceive;
        recv(self, ctx, state->buf);
        break;
    case RAfterReceive:
        printf("RECEIVER => Received message: '%s'\n",
            state->buf);
        printf("RECEIVER => Responding with: '%s'\n",
            resp);
        state->state_enum = RAfterRespond;
        send(self, ctx, 1, resp);
        break;
    case RAfterRespond:
        printf("RECEIVER => DONE\n");
        self->c_state = DONE;
        break;
    }
}

Coroutine make_receiver()
{
    ReceiverState* state = calloc(1, sizeof(ReceiverState));
    state->buf = calloc(128, sizeof(char));
    state->state_enum = RBeforeReceive;
    return (Coroutine) {
        .c_state = READY,
        .state = state,
        .poll = receiver,
    };
}

typedef struct ProcQueue {
    Process* head;
    Process* tail;
} ProcQueue;

void append_proc(ProcQueue* queue, Process* proc)
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
Process* pop_proc(ProcQueue* queue)
{
    if (!queue->head) {
        return NULL;
    }
    Process* pop = queue->head;
    queue->head = queue->head->next;
    return pop;
}
Message* pop_msg(MessageQueue* queue)
{
    if (!queue->head) {
        return NULL;
    }
    Message* pop = queue->head;
    queue->head = queue->head->next;
    return pop;
}

void deliver_messages(MessageQueue* mq, Process* procs, int procs_sz)
{
    while (mq->head) {
        Message* msg = pop_msg(mq);
        Process* to = &procs[msg->to_id - 1];
        Process* from = &procs[msg->from_id - 1];
        to->msg = msg;
        to->co.c_state = READY;
        from->co.c_state = READY;
        if (to->recv_buf) {
            recv_impl(&to->co, to, to->recv_buf);
        }
    }
}

int main(void)
{
    MessageQueue mq = { };
    Process coroutines[2] = { };
    coroutines[0] = (Process) {
        .co = make_sender(),
        .flags = 0,
        .msg = NULL,
        .id = 1,
    };
    coroutines[1] = (Process) {
        .co = make_receiver(),
        .flags = 0,
        .msg = NULL,
        .id = 2,
    };
    ProcQueue pqueue = { };
    jmp_buf jmp_buffer = { };
    append_proc(&pqueue, &coroutines[0]);
    append_proc(&pqueue, &coroutines[1]);
    while (1) {
        int ret = setjmp(jmp_buffer);
        if (ret != 0) {
            append_proc(&pqueue, &coroutines[ret - 1]);
        }
        deliver_messages(&mq, coroutines, 2);
        Process* next = { };
        next = pop_proc(&pqueue);
        if (!next) {
            break;
        }
        if (next->co.c_state == BLOCKED) {
            if ((next->flags & MESSAGE_PENDING) != MESSAGE_PENDING) {
                append_proc(&pqueue, next);
                continue;
            }
        }
        if (next->co.c_state == DONE) {
            continue;
        }
        Context ctx = {
            .procs = coroutines,
            .c_id = next->id,
            .msg_queue = &mq,
            .restart = &jmp_buffer,
        };
        next->co.poll(
            &next->co,
            &ctx);
    }
    return 0;
}
