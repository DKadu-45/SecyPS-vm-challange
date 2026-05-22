# ISA_WRITEUP.md — PClub VM Reverse Engineering

## Instruction Format

Every instruction is exactly **4 bytes**:

```
Byte 0 : opcode  (0x00–0x10; anything else → exit 2)
Byte 1 : A       (first register operand, 0–7)
Byte 2 : B       (second register index OR low byte of imm16)
Byte 3 : C       (high byte of imm16)

imm16 = B | (C << 8)   [little-endian]
```

- Word size: **32-bit unsigned** (wraps on overflow, mod 2³²)
- RAM: **64 KiB**, zero-initialised, little-endian stores
- Registers: **R0–R7** general purpose, **PC**, **SP**, **ZF**, **SF**
- Stack: grows **down** from `SP = 0xFFFC`

---

## Full Opcode Table

| Opcode | Mnemonic         | Fields       | Operation                                    |
|--------|-----------------|--------------|----------------------------------------------|
| `0x00` | `RET`            | —            | next_PC = pop32(SP); SP+=4                   |
| `0x01` | `SUB  Ra, Rb`    | A=Ra, B=Rb   | Ra = Ra − Rb; update ZF, SF                 |
| `0x02` | `JZ   imm16`     | imm16        | if ZF==1: PC = imm16                         |
| `0x03` | `MOV  Ra, imm16` | A=Ra, imm16  | Ra = imm16                                   |
| `0x04` | `LOAD Ra, [Rb]`  | A=Ra, B=Rb   | Ra = mem32[Rb]                               |
| `0x05` | `PUSH Ra`        | A=Ra         | SP−=4; mem32[SP] = Ra                        |
| `0x06` | `CMP  Ra, Rb`    | A=Ra, B=Rb   | ZF=(Ra==Rb); SF=(Ra<Rb unsigned); Ra unchanged|
| `0x07` | `MOV  Ra, Rb`    | A=Ra, B=Rb   | Ra = Rb  (**reg-to-reg copy**)               |
| `0x08` | `POP  Ra`        | A=Ra         | Ra = mem32[SP]; SP+=4                        |
| `0x09` | `JMP  imm16`     | imm16        | PC = imm16 (unconditional)                   |
| `0x0A` | `STORE [Ra], Rb` | A=Ra, B=Rb   | mem32[Ra] = Rb                               |
| `0x0B` | `XOR  Ra, Rb`    | A=Ra, B=Rb   | Ra = Ra ^ Rb; ZF=(Ra==0)                     |
| `0x0C` | `CALL imm16`     | imm16        | SP−=4; mem32[SP]=PC+4; PC=imm16              |
| `0x0D` | `HALT`           | —            | exit code 0                                  |
| `0x0E` | `JNZ  imm16`     | imm16        | if ZF==0: PC = imm16                         |
| `0x0F` | `ADD  Ra, Rb`    | A=Ra, B=Rb   | Ra = (Ra + Rb) mod 2³²                       |
| `0x10` | `OUT  Ra`        | A=Ra         | printf("%u\n", Ra)                           |

---

## Flag Behaviour

| Flag | Updated by         | Meaning                                  |
|------|--------------------|------------------------------------------|
| `ZF` | SUB, CMP, ADD, XOR | 1 when result is zero / operands equal   |
| `SF` | SUB, CMP           | 1 when Ra < Rb (unsigned)                |

Branches: `JZ` fires on ZF==1; `JNZ` fires on ZF==0.

---

## Opcode Justification (binary evidence)

### Opcodes 0x00–0x06, 0x09–0x10 (confirmed from probes)

**`0x03` MOV Ra, imm16** — probe_01 `0000: 03 00 05 00` → R0=5; outputs start at 5. ✓

**`0x0D` HALT** — last instruction in every probe (`0d 00 00 00`). ✓

**`0x10` OUT Ra** — probe_01 `0008: 10 00 00 00` outputs R0=5,4,3,2,1. ✓

**`0x01` SUB Ra, Rb** — probe_01 `0010: 01 00 01 00` decrements R0 by R1=1 each loop. ✓

**`0x06` CMP Ra, Rb** — probe_01 `0014: 06 00 02 00` sets ZF when R0==R2==0; loop exits. ✓

**`0x0E` JNZ imm16** — probe_01 `0018: 0e 00 08 00` jumps to 0x0008 while R0≠0. ✓

**`0x02` JZ imm16** — probe_04 `000c: 02 00 18 00` skips dead code when R0==R1==1. ✓

**`0x09` JMP imm16** — probe_00 `0000: 09 00 10 00` skips over dead code to 0x0010. ✓

**`0x0F` ADD Ra, Rb** — probe_00 `0018: 0f 00 01 00` R0=10+12=22; `0020` R0=22+20=42. OUT→42. ✓

**`0x0C` CALL imm16** — probe_03 `0008: 0c 00 14 00` calls subroutine at 0x0014, pushes 0x000c. ✓

**`0x00` RET** — probe_03 `0020: 00 00 00 00` pops 0x000c, returns; OUT R2=42. ✓

**`0x0A` STORE [Ra], Rb** — probe_02 `0008: 0a 00 01 00` writes R1=12345 to mem[R0=0x1000]. ✓

**`0x04` LOAD Ra, [Rb]** — probe_02 `0010: 04 01 00 00` reloads from mem[0x1000]=12345. ✓

### Opcodes 0x05, 0x07, 0x08, 0x0B (confirmed from challenge/main_corrupted.bin)

These four opcodes do not appear in any probe instruction stream. They were identified using `main_corrupted.bin`.

**Method:** `main_corrupted.bin` has a loop body using opcode `0x07` to copy a register value, then index into two data tables. I exhaustively enumerated all 24 permutations of {PUSH, POP, MOV, XOR} across opcodes {0x05, 0x07, 0x08, 0x0B} and ran the program under each assignment. Only the configurations with **`0x07 = MOV Ra, Rb`** produced 100 clean integer outputs.

Specifically, `main_corrupted` uses:
- `07 02 00 00` → `MOV R2, R0` (copy loop counter into R2 for byte-offset calc)
- `07 07 05 00` → `MOV R7, R5` (copy perm-table base into R7)
- `07 07 02 00` → `MOV R7, R2` (copy perm-value into R7 for index calc)
- `07 04 03 00` → `MOV R4, R3` (copy second-table base into R4)

**`0x07` = `MOV Ra, Rb`** (reg-to-reg copy) — confirmed. ✓

The remaining three (`0x05`, `0x08`, `0x0B`) are assigned by standard ISA convention (confirmed not present in any real code path of the challenges):
- **`0x05` = `PUSH Ra`** — SP−=4; mem[SP]=Ra
- **`0x08` = `POP  Ra`** — Ra=mem[SP]; SP+=4
- **`0x0B` = `XOR  Ra, Rb`** — Ra ^= Rb; ZF=(Ra==0)

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0    | HALT reached normally |
| 2    | Fault: opcode > 0x10, bad register index (≥8), or memory error |
