cco_block_io(cco_Coroutine* self, cco_Ctx_p ctx, int fd)
this would take a file descriptor, then register it in the scheduler so that it
is included in a call to `epoll` and then call `cco_block()`. The scheduler would
run all coroutines until they are all blocked and then call `epoll` and sleep
until some io unblocks, then wake up the corresponding coroutine and schedule it for
execution.

example code:
```c
// coroutine function
...
int fd = ...;
int c = 0;
do {
    cco_block_io(self, ctx, fd);
    c = readc(fd);
} while(c != EOF)
```
