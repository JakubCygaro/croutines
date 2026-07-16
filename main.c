#define CCO_IMPLEMENTATION
#include "cco.h"
#include <stdio.h>
#include <stdlib.h>

void* make_spawn1()
{
    return NULL;
}
void spawn1(cco_Coroutine* self, cco_Ctx_p ctx)
{
    printf("SPAWN1 => I have been spawned\n");
    cco_return(self, ctx);
}

typedef enum SenderStateEnum {
    SBeforeSent,
    SAfterSent,
    SAfterResponse,
} SenderStateEnum;

typedef struct SenderState {
    SenderStateEnum sstate;
    char* buf;
    cco_Co_handle* send_to;
} SenderState;

void sender(cco_Coroutine* self, cco_Ctx_p ctx)
{
    SenderState* state = (SenderState*)self->state;
    char msg[128] = "Hello, World!";
    switch (state->sstate) {
    case SBeforeSent:
        printf("SENDER => Spawning spawn1\n");
        cco_spawn(
            ctx,
            cco_make_coroutine(
                make_spawn1(),
                spawn1));
        printf("SENDER => Sending message: '%s'\n", msg);
        state->sstate = SAfterSent;
        cco_send(self, ctx, *state->send_to, msg);
        break;
    case SAfterSent:
        printf("SENDER => Awaiting response\n");
        state->sstate = SAfterResponse;
        cco_recv(self, ctx, state->buf);
        break;
    case SAfterResponse:
        printf("SENDER => Got response: '%s'\n", state->buf);
        printf("SENDER => DONE\n");
        cco_return(self, ctx);
        break;
    }
}

void* make_sender(cco_Co_handle* send_to)
{
    SenderState* state = calloc(1, sizeof(SenderState));
    state->buf = calloc(128, sizeof(char));
    state->sstate = SBeforeSent;
    state->send_to = send_to;
    return state;
}

typedef enum ReceiverStateEnum {
    RBeforeReceive,
    RAfterReceive,
    RAfterRespond,
} ReceiverStateEnum;

typedef struct ReceiverState {
    ReceiverStateEnum state_enum;
    char* buf;
    cco_Co_handle* receive_from;
} ReceiverState;

void receiver(cco_Coroutine* self, cco_Ctx_p ctx)
{
    ReceiverState* state = self->state;
    char resp[128] = "This is a response message";
    switch (state->state_enum) {
    case RBeforeReceive:
        state->state_enum = RAfterReceive;
        cco_recv(self, ctx, state->buf);
        break;
    case RAfterReceive:
        printf("RECEIVER => Received message: '%s'\n",
            state->buf);
        printf("RECEIVER => Responding with: '%s'\n",
            resp);
        state->state_enum = RAfterRespond;
        cco_send(self, ctx, *state->receive_from, resp);
        break;
    case RAfterRespond:
        printf("RECEIVER => DONE\n");
        cco_return(self, ctx);
        break;
    }
}

void* make_receiver(cco_Co_handle* recieve_from)
{
    ReceiverState* state = calloc(1, sizeof(ReceiverState));
    state->buf = calloc(128, sizeof(char));
    state->state_enum = RBeforeReceive;
    state->receive_from = recieve_from;
    return state;
}

int main(void)
{
    cco_Sched sched = cco_Sched_new();
    cco_Co_handle s = { };
    cco_Co_handle r = { };
    s = cco_Sched_add_coroutine(
        &sched,
        cco_make_coroutine(
            make_sender(&r),
            sender));
    r = cco_Sched_add_coroutine(
        &sched,
        cco_make_coroutine(
            make_receiver(&s),
            receiver));
    cco_Sched_run(&sched);
    cco_Sched_free(sched);
    return 0;
}
