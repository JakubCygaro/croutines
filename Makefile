CFLAGS=-Wall -Wextra -g
OBJS=main.o libcco.a
ASM=fasm
ASMFLAGS=-s cco_asm.dbg
CCO_LIB_OBJS=cco.o cco_asm.o

all: libcco.a main

main: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

libcco.a: $(CCO_LIB_OBJS)
	ar rcs $@ $^

cco_asm.o: cco_asm.asm
	$(ASM) $(ASMFLAGS) $^ $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all
