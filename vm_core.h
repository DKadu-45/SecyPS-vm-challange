#ifndef VM_CORE_H
#define VM_CORE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── ISA constants ─────────────────────────────────────────────────────── */
#define OP_RET    0x00   /* RET              – return from subroutine        */
#define OP_SUB    0x01   /* SUB  Ra, Rb      – Ra = Ra - Rb                  */
#define OP_JZ     0x02   /* JZ   imm16       – jump if ZF == 1               */
#define OP_MOV    0x03   /* MOV  Ra, imm16   – Ra = imm16                    */
#define OP_LOAD   0x04   /* LOAD Ra, [Rb]    – Ra = mem[Rb]  (32-bit word)   */
#define OP_PUSH   0x05   /* PUSH Ra          – SP-=4; mem[SP]=Ra             */
#define OP_CMP    0x06   /* CMP  Ra, Rb      – set ZF/SF                     */
#define OP_MOV_R  0x07   /* MOV  Ra, Rb      – Ra = Rb  (reg-to-reg copy)    */
#define OP_POP    0x08   /* POP  Ra          – Ra=mem[SP]; SP+=4             */
#define OP_JMP    0x09   /* JMP  imm16       – unconditional jump             */
#define OP_STORE  0x0A   /* STORE [Ra], Rb   – mem[Ra] = Rb  (32-bit word)   */
#define OP_XOR    0x0B   /* XOR  Ra, Rb      – Ra = Ra ^ Rb                  */
#define OP_CALL   0x0C   /* CALL imm16       – call subroutine               */
#define OP_HALT   0x0D   /* HALT             – stop (exit 0)                 */
#define OP_JNZ    0x0E   /* JNZ  imm16       – jump if ZF == 0               */
#define OP_ADD    0x0F   /* ADD  Ra, Rb      – Ra = Ra + Rb                  */
#define OP_OUT    0x10   /* OUT  Ra          – print Ra as decimal + newline  */

#define NUM_REGS    8
#define RAM_SIZE    65536   /* 64 KiB */
#define STACK_START 65532   /* top of RAM, grows down (word-aligned) */

/* ─── VM state ──────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t r[NUM_REGS];  /* R0–R7                              */
    uint32_t pc;           /* program counter (byte address)     */
    uint32_t sp;           /* stack pointer                      */
    uint8_t  zf;           /* zero flag                          */
    uint8_t  sf;           /* sign/less-than flag                */
    uint8_t  mem[RAM_SIZE];/* 64 KiB RAM (little-endian stores)  */
    uint8_t  halted;       /* 1 = halted                         */
    int      exit_code;    /* 0 = halt, 2 = fault                */
} VM;

/* ─── Instruction decode ─────────────────────────────────────────────────── */
typedef struct {
    uint8_t  op;
    uint8_t  a;    /* first register field  */
    uint8_t  b;    /* second register field / low byte of imm16 */
    uint8_t  c;    /* high byte of imm16                        */
    uint16_t imm;  /* b | (c << 8)                              */
} Instr;

static inline Instr decode(const uint8_t *mem, uint32_t addr) {
    Instr i;
    i.op  = mem[addr + 0];
    i.a   = mem[addr + 1];
    i.b   = mem[addr + 2];
    i.c   = mem[addr + 3];
    i.imm = (uint16_t)(i.b | (i.c << 8));
    return i;
}

/* 32-bit word load/store from RAM (little-endian) */
static inline uint32_t mem_load32(const VM *vm, uint32_t addr) {
    addr &= 0xFFFF;
    return (uint32_t)vm->mem[addr]
         | ((uint32_t)vm->mem[(addr+1)&0xFFFF] << 8)
         | ((uint32_t)vm->mem[(addr+2)&0xFFFF] << 16)
         | ((uint32_t)vm->mem[(addr+3)&0xFFFF] << 24);
}

static inline void mem_store32(VM *vm, uint32_t addr, uint32_t val) {
    addr &= 0xFFFF;
    vm->mem[addr]              = (uint8_t)(val);
    vm->mem[(addr+1) & 0xFFFF] = (uint8_t)(val >> 8);
    vm->mem[(addr+2) & 0xFFFF] = (uint8_t)(val >> 16);
    vm->mem[(addr+3) & 0xFFFF] = (uint8_t)(val >> 24);
}

/* ─── Disassemble one instruction to a string ───────────────────────────── */
static inline int disasm_instr(char *buf, size_t bufsz,
                               uint32_t addr, const uint8_t *mem_bytes)
{
    Instr i = decode(mem_bytes, addr);
    const char *rname[8] = {"R0","R1","R2","R3","R4","R5","R6","R7"};

    /* raw bytes */
    char raw[32];
    snprintf(raw, sizeof(raw), "%02x %02x %02x %02x",
             i.op, i.a, i.b, i.c);

    char mnem[64] = "???";
    switch (i.op) {
        case OP_RET:    snprintf(mnem, sizeof(mnem), "RET"); break;
        case OP_SUB:    snprintf(mnem, sizeof(mnem), "SUB  %s, %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_JZ:     snprintf(mnem, sizeof(mnem), "JZ   0x%04x", i.imm); break;
        case OP_MOV:    snprintf(mnem, sizeof(mnem), "MOV  %s, %u",
                                 rname[i.a & 7], (unsigned)i.imm); break;
        case OP_LOAD:   snprintf(mnem, sizeof(mnem), "LOAD %s, [%s]",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_PUSH:   snprintf(mnem, sizeof(mnem), "PUSH %s", rname[i.a & 7]); break;
        case OP_CMP:    snprintf(mnem, sizeof(mnem), "CMP  %s, %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_MOV_R:  snprintf(mnem, sizeof(mnem), "MOV  %s, %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_POP:    snprintf(mnem, sizeof(mnem), "POP  %s", rname[i.a & 7]); break;
        case OP_JMP:    snprintf(mnem, sizeof(mnem), "JMP  0x%04x", i.imm); break;
        case OP_STORE:  snprintf(mnem, sizeof(mnem), "STORE [%s], %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_XOR:    snprintf(mnem, sizeof(mnem), "XOR  %s, %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_CALL:   snprintf(mnem, sizeof(mnem), "CALL 0x%04x", i.imm); break;
        case OP_HALT:   snprintf(mnem, sizeof(mnem), "HALT"); break;
        case OP_JNZ:    snprintf(mnem, sizeof(mnem), "JNZ  0x%04x", i.imm); break;
        case OP_ADD:    snprintf(mnem, sizeof(mnem), "ADD  %s, %s",
                                 rname[i.a & 7], rname[i.b & 7]); break;
        case OP_OUT:    snprintf(mnem, sizeof(mnem), "OUT  %s", rname[i.a & 7]); break;
        default:        snprintf(mnem, sizeof(mnem), "??? (0x%02x)", i.op); break;
    }

    return snprintf(buf, bufsz, "0x%04x  %-16s  %s", addr, raw, mnem);
}

/* ─── Load program binary into VM RAM at address 0 ──────────────────────── */
static inline int vm_load(VM *vm, const char *path) {
    memset(vm, 0, sizeof(VM));
    vm->sp = STACK_START;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0 || sz > RAM_SIZE) { fclose(f); return -1; }
    size_t nread = fread(vm->mem, 1, (size_t)sz, f);
    (void)nread;
    fclose(f);
    return 0;
}

/* ─── Execute ONE instruction; returns 0=ok, 1=halted, 2=fault ─────────── */
static inline int vm_step(VM *vm) {
    if (vm->halted) return vm->exit_code ? 2 : 1;

    /* Validate PC alignment and range */
    if (vm->pc + 3 >= RAM_SIZE) {
        vm->halted = 1; vm->exit_code = 2; return 2;
    }

    Instr i = decode(vm->mem, vm->pc);
    uint32_t next_pc = vm->pc + 4;

    /* Validate register indices */
    if (i.a >= NUM_REGS || (
        (i.op == OP_SUB || i.op == OP_CMP || i.op == OP_LOAD ||
         i.op == OP_STORE || i.op == OP_MOV_R || i.op == OP_XOR ||
         i.op == OP_ADD) && i.b >= NUM_REGS)) {
        vm->halted = 1; vm->exit_code = 2; return 2;
    }

    switch (i.op) {
        case OP_RET: {
            if (vm->sp >= STACK_START) { vm->halted=1; vm->exit_code=2; return 2; }
            vm->sp += 4;
            next_pc = mem_load32(vm, vm->sp - 4);
            break;
        }
        case OP_SUB: {
            uint32_t result = vm->r[i.a] - vm->r[i.b];
            vm->zf = (result == 0) ? 1 : 0;
            vm->sf = (vm->r[i.a] < vm->r[i.b]) ? 1 : 0;
            vm->r[i.a] = result;
            break;
        }
        case OP_JZ: {
            if (vm->zf) next_pc = i.imm;
            break;
        }
        case OP_MOV: {
            vm->r[i.a] = i.imm;
            break;
        }
        case OP_LOAD: {
            uint32_t addr = vm->r[i.b];
            vm->r[i.a] = mem_load32(vm, addr);
            break;
        }
        case OP_PUSH: {
            vm->sp -= 4;
            mem_store32(vm, vm->sp, vm->r[i.a]);
            break;
        }
        case OP_CMP: {
            vm->zf = (vm->r[i.a] == vm->r[i.b]) ? 1 : 0;
            vm->sf = (vm->r[i.a] <  vm->r[i.b]) ? 1 : 0;
            break;
        }
        case OP_MOV_R: {
            vm->r[i.a] = vm->r[i.b];
            break;
        }
        case OP_POP: {
            vm->r[i.a] = mem_load32(vm, vm->sp);
            vm->sp += 4;
            break;
        }
        case OP_JMP: {
            next_pc = i.imm;
            break;
        }
        case OP_STORE: {
            mem_store32(vm, vm->r[i.a], vm->r[i.b]);
            break;
        }
        case OP_XOR: {
            vm->r[i.a] ^= vm->r[i.b];
            vm->zf = (vm->r[i.a] == 0) ? 1 : 0;
            break;
        }
        case OP_CALL: {
            /* push return address, jump to imm16 */
            vm->sp -= 4;
            mem_store32(vm, vm->sp, next_pc);
            next_pc = i.imm;
            break;
        }
        case OP_HALT: {
            vm->halted = 1;
            vm->exit_code = 0;
            return 1;
        }
        case OP_JNZ: {
            if (!vm->zf) next_pc = i.imm;
            break;
        }
        case OP_ADD: {
            vm->r[i.a] = (vm->r[i.a] + vm->r[i.b]) & 0xFFFFFFFF;
            vm->zf = (vm->r[i.a] == 0) ? 1 : 0;
            break;
        }
        case OP_OUT: {
            printf("%u\n", vm->r[i.a]);
            break;
        }
        default: {
            vm->halted = 1; vm->exit_code = 2; return 2;
        }
    }

    vm->pc = next_pc;
    return 0;
}

#endif /* VM_CORE_H */
