CFLAGS=-Wall -Wextra
OBJS=main.o

all: main

main: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all
