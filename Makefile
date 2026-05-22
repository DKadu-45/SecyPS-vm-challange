CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11

.PHONY: all clean

all: disasm dbg

disasm: disasm.c vm_core.h
	$(CC) $(CFLAGS) -o disasm disasm.c

dbg: dbg.c vm_core.h
	$(CC) $(CFLAGS) -o dbg dbg.c

clean:
	rm -f disasm dbg
