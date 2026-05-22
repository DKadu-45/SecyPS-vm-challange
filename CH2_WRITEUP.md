# CH2_WRITEUP.md — Challenge 2: verify_corrupted.bin

## Observable Behaviour

`./vm challenge/verify_corrupted.bin` outputs `0`. The correct binary should output `1`.

## Analysis Method

### Step 1: Disassemble

```
./disasm challenge/verify_corrupted.bin
```

Real code is `0x0000–0x009c`. Data (the array to be verified) lives at `0x00a0`.

### Step 2: Understand the program

**Phase 1 — copy array (0x0000–0x0040):**
Copy 16 words from `mem[0xa0..]` into `mem[0x100..]`:
```
R0 = 0xa0    ; source base
R1 = 0x100   ; dest base
R2 = 0        ; byte offset
R3 = 0        ; iteration counter
Loop:
  R4 = MOV R0         ; R4 = src base
  R4 += R2            ; R4 = src base + offset
  R5 = LOAD [R4]      ; R5 = src[i]
  STORE [R1], R5      ; mem[R1] = src[i]
  R1 += 4; R2 += 4; R3 += 1
  CMP R3, 16; JNZ loop
```

**Phase 2 — sum array (0x0044–0x0070):**
```
R1 = 0x100, R2 = 0, R3 = 0
Loop:
  R4 = LOAD [R1]
  R3 += R4
  R1 += 4; R2 += 1; CMP R2,16; JNZ loop
```
Result: R3 = sum of all 16 words.

**Phase 3 — compare (0x0074–0x009c):**
```
R2 = 2556
CMP R3, R2        ; ZF=1 if sum==2556
MOV R0, 1
MOV R1, 0
JZ 0x008c         ; if match → OUT R0(=1)  → output 1
MOV R0, R1        ; else R0=0
OUT R0            ; output 0
HALT
```

The program outputs 1 **iff** the sum of the 16 data words equals **2556**.

### Step 3: Find the corruption

Data at `0x00a0` (16 words):
```
17, 34, 51, 68, 85, 102, 119, [137], 153, 170, 187, 204, 221, 238, 257, 514
```

The correct pattern is multiples of 17: `17×1, 17×2, …, 17×16`.
Correct values: `17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255, 272`.

Observed vs expected:
- `data[7]` = **137**, should be **136** = 17×8  (off by +1, at offset `0x00bc`)
- `data[14]` = 257 vs 255 — but these don't affect the sum check
- Actual sum = **2557**, expected = **2556**, difference = **1**

Only **one byte** differs: `0x00bc` holds `89 00 00 00` (=137) instead of `88 00 00 00` (=136).

### Step 4: Confirm with debugger

```
./dbg
dbg> load challenge/verify_corrupted.bin
dbg> break 0x0078       ; CMP R3, R2 (sum vs 2556)
dbg> run
dbg> regs               ; R3=2557, R2=2556 — ZF=0 after CMP → takes "not equal" path → OUT 0
dbg> mem 0x00bc 4       ; shows: 89 00 00 00  (137)
```

Expected: R3=2556 = R2 → ZF=1 → `JZ 0x008c` taken → OUT R0=1.

## The Fix

| Offset   | Original byte | Fixed byte | Explanation                       |
|----------|--------------|------------|-----------------------------------|
| `0x00bc` | `0x89` (137) | `0x88` (136) | data[7]: 137 → 136 = 17×8      |

## Verification

```
./vm bins/verify_fixed.bin
# 1
```
