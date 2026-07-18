# C coroutines

A simple experiment in creating C coroutines with message passing and longjmp.

## Usage

Calling `cco_yield()` suspends the calling procedure if it is a coroutine and moves it to
the back of the scheduling queue. A call to `cco_return()` marks the coroutine as 
finished and is mandatory as the last call in a coroutine function.

A couroutine can `send()` a message (`cco_message_t`) to a different couroutine
via a handle (`cco_message_t`) to that coroutine. It will then get blocked untill the message is 
delivered. A couroutine can also `recv()` a message, this action can block it if there 
is no pending message delivered. The couroutine will remain blocked untill a message is sent 
to it.

Messages are passed rendez-vous style - that means a `send()` call unblocks only when
the recipents `recv()` is called and vice versa.

## Implementation details

A couple of functions are implemented in x86_64 assembly, written with the Flat Assembler 
[FASM](https://flatassembler.net/). Their usage is documented in the source code but the
high-level overview is as follows:

The scheduler uses `setjmp` in its running loop to return from coroutines. Before calling `longjmp`
`cco_yield_impl` is responsible for saving the state of the stack and a return address
to the function that called `cco_yield`, `cco_return` or `cco_block`. Then the scheduler
saves the stack of the coroutine in `cco_store` via `cco_save_stack`.
How much of the stack is saved is defined with `CCO_CO_STACKF_SIZE` and by default it
is a kilobyte. 
Then the next coroutine in the queue is scheduled to run. `cco_stage` is used to either return into 
a coroutine or call it for the first time via `start`. Returning is done via 
`cco_load_stack` which restores the state of the stack before a call to `cco_yield`, 
`cco_return` or `cco_block`. Then `cco_yield_return` is called which restores saved
registers and jumps into the saved return address inside the coroutine.
