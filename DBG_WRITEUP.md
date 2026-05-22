# DBG_WRITEUP.md — Debugger Implementation

## Architecture

The implementation is split across two source files and one shared header:

```
vm_core.h   — VM state struct, 4-byte instruction decoder, single-step executor,
              disassemble-to-string helper, binary loader
disasm.c    — standalone disassembler (thin wrapper around vm_core.h)
dbg.c       — interactive REPL: breakpoint manager, all CLI commands
```

### vm_core.h

**`VM` struct** holds:
- `r[8]` — R0–R7 (uint32_t)
- `pc`, `sp` — program counter and stack pointer
- `zf`, `sf` — zero flag and sign/less-than flag
- `mem[65536]` — 64 KiB RAM (program loaded at address 0)
- `halted`, `exit_code`

**`decode()`** splits 4 bytes into `{op, a, b, c, imm16}` where `imm16 = b|(c<<8)`.

**`vm_step()`** executes one instruction — handles all 17 opcodes, updates PC, flags, stack. Returns `0`=ok, `1`=halted normally, `2`=fault.

**`disasm_instr()`** decodes one instruction into a formatted string: `"0x0000  03 00 05 00  MOV  R0, 5"`. Used by both `disasm` and the `dis` command in `dbg`.

**`vm_load()`** — `fopen` + `fread` into `mem[0]`, zero-initialises rest, sets `sp=0xFFFC`.

### Emulator correctness — all 17 opcodes

| Opcode | Key implementation note |
|--------|------------------------|
| `RET`    | `next_pc = mem32[SP]; SP += 4` |
| `SUB`    | 32-bit wrap; sets ZF=(result==0), SF=(Ra<Rb) |
| `JZ`     | `if (zf) next_pc = imm16` |
| `MOV imm`| `Ra = imm16` |
| `LOAD`   | `Ra = mem32[Rb]` (little-endian 4-byte read) |
| `PUSH`   | `SP -= 4; mem32[SP] = Ra` |
| `CMP`    | Sets ZF and SF; Ra **unchanged** |
| `MOV Rb` | `Ra = Rb` (reg-to-reg, opcode 0x07) |
| `POP`    | `Ra = mem32[SP]; SP += 4` |
| `JMP`    | `next_pc = imm16` (unconditional) |
| `STORE`  | `mem32[Ra] = Rb` |
| `XOR`    | `Ra ^= Rb; ZF = (Ra==0)` |
| `CALL`   | `SP -= 4; mem32[SP] = PC+4; next_pc = imm16` |
| `HALT`   | `halted = 1; exit_code = 0; return 1` |
| `JNZ`    | `if (!zf) next_pc = imm16` |
| `ADD`    | `Ra = (Ra + Rb) & 0xFFFFFFFF` |
| `OUT`    | `printf("%u\n", Ra)` |

### REPL design

`dbg.c` runs a `while(1)` loop reading lines from stdin. Each line is whitespace-trimmed then matched with `strcmp`/`strncmp`. No dependencies beyond libc.

- **`run` / `cont`**: tight loop over `vm_step()`, stopping on halt, fault, or a breakpoint address matching `vm->pc`. Breakpoint is checked at the *top* of each iteration (after at least 1 step) so the instruction at the breakpoint address is not executed before the user can inspect state.
- **`cont N`**: passes a max-step count, stops after exactly N steps.
- **`break` / `unbreak`**: fixed array of up to 64 `uint32_t` addresses.
- **`restart`**: calls `vm_load()` again with the saved path; resets VM state but keeps breakpoints.

---

## Example Session: probe_01

`probe_01.bin` implements a countdown loop from 5 to 1. This session demonstrates
breakpoints, stepping, register inspection, memory dump, and restart.

```
$ ./dbg
PClub VM Debugger  (type 'help' for commands)

dbg> load probe_01.bin
  loaded 'probe_01.bin'

dbg> regs
  R0 =          0 (0x00000000)
  R1 =          0 (0x00000000)
  R2 =          0 (0x00000000)
  R3 =          0 (0x00000000)
  R4 =          0 (0x00000000)
  R5 =          0 (0x00000000)
  R6 =          0 (0x00000000)
  R7 =          0 (0x00000000)
  PC = 0x0000
  SP = 0xfffc
  ZF = 0  SF = 0

dbg> dis
  0x0000  03 00 05 00       MOV  R0, 5

dbg> break 0x0008
  breakpoint set at 0x0008

dbg> run
  breakpoint hit at PC=0x0008
  0x0008  10 00 00 00       OUT  R0

dbg> regs
  R0 =          5 (0x00000005)
  R1 =          0 (0x00000000)
  R2 =          0 (0x00000000)
  ...
  PC = 0x0008
  SP = 0xfffc
  ZF = 0  SF = 0

dbg> step
  0x0008  10 00 00 00       OUT  R0
5
dbg> regs
  R0 =          5 (0x00000005)
  ...
  PC = 0x000c
  ZF = 0  SF = 0

dbg> cont
  breakpoint hit at PC=0x0008
  0x0008  10 00 00 00       OUT  R0

dbg> unbreak 0x0008
  breakpoint removed from 0x0008

dbg> restart
  reloaded 'probe_01.bin'

dbg> break 0x0014
  breakpoint set at 0x0014

dbg> run
5
  breakpoint hit at PC=0x0014
  0x0014  06 00 02 00       CMP  R0, R2

dbg> mem 0 32
  0x0000: 03 00 05 00 03 02 00 00 10 00 00 00 03 01 01 00
  0x0010: 01 00 01 00 06 00 02 00 0e 00 08 00 0d 00 00 00

dbg> quit
```

**Walk-through:**
1. Fresh load: all registers 0, PC=0x0000, SP=0xFFFC.
2. `dis` shows first instruction: `MOV R0, 5`.
3. Breakpoint at `0x0008` (the `OUT R0` = top of loop). `run` executes MOV R0,5 → MOV R2,0, then hits breakpoint. R0=5 confirmed.
4. `step` executes `OUT R0`, printing `5`. PC advances to `0x000c`.
5. `cont` runs: SUB R0,R1 → CMP R0,R2 → JNZ back → breakpoint fires again (R0=4 now).
6. `unbreak 0x0008` + `restart` resets the VM cleanly.
7. New breakpoint at `0x0014` (the `CMP` instruction). `run` executes the full first OUT (prints 5) then stops before the CMP. `mem 0 32` confirms program bytes in RAM.

**Key observations from this session:**
- The loop body is `OUT → MOV R1,1 → SUB R0,R1 → CMP R0,R2 → JNZ`.
- R2=0 is set once and never modified; the loop exits when R0 reaches 0 (ZF=1, JNZ not taken).
- SP remains 0xFFFC throughout — no PUSH/POP/CALL used in this program.

---

## Commands Reference

| Command | Description |
|---------|-------------|
| `load <file>` | Load binary into RAM at address 0; reset all state |
| `run` | Run from current PC until halt or breakpoint |
| `step` | Disassemble + execute one instruction |
| `cont` | Continue from current PC (alias for run after a stop) |
| `cont N` | Advance exactly N steps then pause |
| `break <addr>` | Set breakpoint (hex `0x..` or decimal) |
| `unbreak <addr>` | Remove breakpoint |
| `regs` | Print R0–R7, PC, SP, ZF, SF |
| `mem <start> <len>` | Hex dump of RAM (byte addresses, hex or decimal) |
| `dis` | Disassemble instruction at current PC |
| `dis <addr>` | Disassemble instruction at given address |
| `restart` | Reload binary from disk; reset VM state (keeps breakpoints) |
| `quit` | Exit |
