# PClub VM Challenge

## Build

```bash
make          # builds ./disasm and ./dbg
```

Requires: gcc, C11.

## Usage

```bash
# Disassemble any .bin
./disasm examples/probe_00.bin

# Interactive debugger
./dbg
dbg> load examples/probe_01.bin
dbg> break 0x0008
dbg> run
dbg> regs
dbg> step
dbg> cont
dbg> mem 0 32
dbg> quit
```

## Files

| File | Description |
|------|-------------|
| `vm_core.h` | Shared ISA: VM struct, decoder, stepper, disassembler |
| `disasm.c` | Standalone disassembler |
| `dbg.c` | Interactive debugger / emulator |
| `ISA_WRITEUP.md` | Full opcode table + reverse-engineering justification |
| `DBG_WRITEUP.md` | Emulator design + example debugger session |
| `CH1_WRITEUP.md` | Challenge 1 fix writeup |
| `CH2_WRITEUP.md` | Challenge 2 fix writeup |
| `bins/` | Fixed binaries: `main_fixed.bin`, `verify_fixed.bin` |

## Validate

```bash
./vm examples/probe_00.bin   # → 42
./vm examples/probe_01.bin   # → 5 4 3 2 1
./vm examples/probe_02.bin   # → 12345
./vm examples/probe_03.bin   # → 42
./vm examples/probe_04.bin   # → 1 7 3

./vm bins/main_fixed.bin     # → 100 correct lines
./vm bins/verify_fixed.bin   # → 1
```
