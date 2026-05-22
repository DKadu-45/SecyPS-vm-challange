# CH1_WRITEUP.md — Challenge 1: main_corrupted.bin

## Observable Behaviour

`./vm challenge/main_corrupted.bin` prints 100 lines. The pattern is the arithmetic
sequence `7, 10, 13, 16, …` (step +3), except line 29 (0-indexed) which outputs `350`
instead of the expected `94`.

## Analysis Method

### Step 1: Disassemble

```
./disasm challenge/main_corrupted.bin
```

The actual program code occupies `0x0000–0x0068` (two HALTs mark the boundary).
Everything from `0x006c` onward is **data** — two lookup tables.

### Step 2: Understand the loop

```
0x0000  MOV  R3, 508          ; base of second table  (0x1fc)
0x0004  MOV  R5, 108          ; base of perm table    (0x6c)
0x0008  MOV  R0, 0            ; loop counter i = 0
0x000c  MOV  R1, 7            ; R1 = 7
0x0010  ADD  R1, R0           ; R1 = 7 + i
0x0014  ADD  R1, R0           ; R1 = 7 + 2i
0x0018  ADD  R1, R0           ; R1 = 7 + 3i   ← expected output
0x001c  MOV  R2, R0           ; R2 = i
0x0020  ADD  R2, R2           ; R2 = 2i
0x0024  ADD  R2, R2           ; R2 = 4i        (byte offset into perm table)
0x0028  MOV  R7, R5           ; R7 = perm base
0x002c  ADD  R7, R2           ; R7 = perm_base + 4i
0x0030  LOAD R2, [R7]         ; R2 = perm[i]
0x0034  MOV  R7, R2           ; R7 = perm[i]
0x0038  ADD  R7, R7           ; R7 = perm[i]*2
0x003c  ADD  R7, R7           ; R7 = perm[i]*4  (byte offset into second table)
0x0040  MOV  R4, R3           ; R4 = second table base
0x0044  ADD  R4, R7           ; R4 = base + perm[i]*4
0x0048  LOAD R4, [R4]         ; R4 = table2[perm[i]]   ← actual output
0x004c  CMP  R1, R4           ; (unused compare)
0x0050  OUT  R4               ; print R4
…
0x0064  JNZ  0x000c           ; loop until i==100
```

The program prints `table2[perm[i]]` for i=0..99. For the output to be `7+3i`,
we need `table2[perm[i]] == 7 + 3*i` for every i.

### Step 3: Locate the corruption

For `i=29`: `perm[29] = 16`, so we need `table2[16] == 7+3*29 = 94`.

- `table2[16]` is at byte address `0x1fc + 16*4 = 0x23c`
- `data[0x23c] = 350` (corrupt), should be `94`
- In hex: `5e 01 00 00` → `5e 00 00 00` (only byte `0x023d` is wrong: `0x01` → `0x00`)

### Step 4: Confirm with debugger

```
./dbg
dbg> load challenge/main_corrupted.bin
dbg> break 0x0050       ; OUT instruction
dbg> cont 29            ; advance 29 hits (iterations 0-28 are correct)
dbg> regs               ; R4=350, R1=94 — mismatch visible
dbg> mem 0x023c 4       ; shows: 5e 01 00 00 (350 in LE)
```

## The Fix

| Offset   | Original byte | Fixed byte | Explanation                     |
|----------|--------------|------------|---------------------------------|
| `0x023d` | `0x01`       | `0x00`     | table2[16]: 350 → 94 (=7+3×29) |

## Verification

After fix: all 100 outputs match `7 + 3*i` for i = 0..99.

```
./vm bins/main_fixed.bin | head -5
# 7
# 10
# 13
# 16
# 19

./vm bins/main_fixed.bin | sed -n '28,32p'
# 88
# 91
# 94    ← was 350
# 97
# 100
```
