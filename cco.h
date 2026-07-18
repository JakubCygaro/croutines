#ifndef CCO_H
#define CCO_H
#include <memory.h>
#include <setjmp.h>
#include <stdlib.h>

#define CCO_SCHED_INIT_PROC_COUNT 10
// size of a message to be passed
#define CCO_MESSAGE_SIZE 128
// size for the stack of delayed messages
#define CCO_DELAY_STACK_SIZE 128
// size of a coroutine stack
#define CCO_CO_STACKF_SIZE 1024


typedef enum cco_CState {
    cco_READY,
    cco_BLOCKED,
    cco_DONE,
} cco_CState;

typedef struct cco_Coroutine cco_Coroutine;
typedef char cco_message_t[CCO_MESSAGE_SIZE];
typedef void* cco_Ctx_p;
typedef struct cco_Process cco_Process;
typedef struct cco_ProcQueue cco_ProcQueue;
typedef struct cco_Sched cco_Sched;
typedef void* cco_Co_handle;

typedef void (*cco_poll_fn)(cco_Coroutine* self, cco_Ctx_p ctx);

typedef struct cco_Coroutine {
    void* state;
    cco_CState c_state;
    cco_poll_fn start;
} cco_Coroutine;

#define cco_make_coroutine(STATE, START) \
    (cco_Coroutine)                      \
    {                                    \
        .state = STATE,                  \
        .c_state = cco_READY,            \
        .start = START                   \
    }


cco_Sched* cco_Sched_new();
cco_Co_handle cco_Sched_add_coroutine(cco_Sched* sched, cco_Coroutine co);
void cco_Sched_run(cco_Sched* sched);
void cco_Sched_free(cco_Sched* sched);

void cco_send(cco_Coroutine* self, cco_Ctx_p ctx, cco_Co_handle co, cco_message_t msg);
void cco_recv(cco_Coroutine* self, cco_Ctx_p ctx, cco_message_t out);

void cco_yield(cco_Coroutine* co, cco_Ctx_p ctx);
void cco_return(cco_Coroutine* co, cco_Ctx_p ctx);

cco_Co_handle cco_spawn(cco_Ctx_p ctx, cco_Coroutine spawn);

#endif
