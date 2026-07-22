CFLAGS=-Wall -Wextra -g
OBJS=main.o libcco.a
ASM=fasm
ASMFLAGS=
CCO_LIB_OBJS=cco.o cco_asm.o
# LINUX_X86_68 , WINDOWS_X86_68
ARCH=LINUX_X86_68
AR=ar

all: libcco.a main

main: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

libcco.a: $(CCO_LIB_OBJS)
	$(AR) rcs $@ $^

cco_asm.o: cco_asm.asm
	$(ASM) $(ASMFLAGS) $^ $@ 1>/dev/null

cco_asm.asm:
	$(CC) -P -nostdinc -x c -E cco_asm.pre.asm -D$(ARCH) > $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all
