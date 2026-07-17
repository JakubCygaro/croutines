#ifndef CCO_H
#define CCO_H
#include <memory.h>
#include <setjmp.h>
#include <stdlib.h>
#define CCO_SCHED_INIT_PROC_COUNT 10
#define CCO_MESSAGE_SIZE 128
#define CCO_DELAY_STACK_SIZE 128

typedef char cco_message_t[CCO_MESSAGE_SIZE];

typedef enum cco_CState {
    cco_READY,
    cco_BLOCKED,
    cco_DONE,
} cco_CState;

typedef struct cco_Coroutine cco_Coroutine;

typedef void* cco_Ctx_p;

typedef void (*cco_poll_fn)(cco_Coroutine* self, cco_Ctx_p ctx);

typedef struct cco_Coroutine {
    void* state;
    cco_CState c_state;
    cco_poll_fn poll;
} cco_Coroutine;

#define cco_make_coroutine(STATE, POLL) \
    (cco_Coroutine)                     \
    {                                   \
        .state = STATE,                 \
        .c_state = cco_READY,           \
        .poll = POLL                    \
    }

#define cco_yield(cco_Coroutine_ptr, cco_Ctx_p) \
    cco_Coroutine_ptr->c_state = cco_READY;     \
    cco_yield_impl(cco_Coroutine_ptr, cco_Ctx_p)

#define cco_return(cco_Coroutine_ptr, cco_Ctx_p) \
    cco_Coroutine_ptr->c_state = cco_DONE;       \
    cco_yield_impl(cco_Coroutine_ptr, cco_Ctx_p)

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

typedef struct cco_Process cco_Process;
typedef struct cco_ProcQueue cco_ProcQueue;
typedef struct cco_Sched cco_Sched;

typedef void* cco_Co_handle;

cco_Sched* cco_Sched_new();
cco_Co_handle cco_Sched_add_coroutine(cco_Sched* sched, cco_Coroutine co);
void cco_Sched_run(cco_Sched* sched);
void cco_Sched_free(cco_Sched* sched);

void cco_send(cco_Coroutine* self, cco_Ctx_p ctx, cco_Co_handle co, cco_message_t msg);
void cco_recv(cco_Coroutine* self, cco_Ctx_p ctx, cco_message_t out);

void cco_yield_impl(cco_Coroutine* co, cco_Ctx_p ctx);
// void cco_yield(cco_Coroutine* co, cco_Ctx_p ctx);
// void cco_return(cco_Coroutine* co, cco_Ctx_p ctx);

cco_Co_handle cco_spawn(cco_Ctx_p ctx, cco_Coroutine spawn);

#endif
