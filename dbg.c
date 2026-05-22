#include "vm_core.h"
#include <ctype.h>

/* ─── Breakpoint set (simple fixed-size) ──────────────────────────────── */
#define MAX_BREAKPOINTS 64

typedef struct {
    uint32_t addr[MAX_BREAKPOINTS];
    int      count;
} BreakSet;

static int bp_has(const BreakSet *bs, uint32_t addr) {
    for (int i = 0; i < bs->count; i++)
        if (bs->addr[i] == addr) return 1;
    return 0;
}
static int bp_add(BreakSet *bs, uint32_t addr) {
    if (bp_has(bs, addr)) return 0;
    if (bs->count >= MAX_BREAKPOINTS) return -1;
    bs->addr[bs->count++] = addr;
    return 0;
}
static void bp_del(BreakSet *bs, uint32_t addr) {
    for (int i = 0; i < bs->count; i++) {
        if (bs->addr[i] == addr) {
            bs->addr[i] = bs->addr[--bs->count];
            return;
        }
    }
}

/* ─── Print registers ─────────────────────────────────────────────────── */
static void print_regs(const VM *vm) {
    for (int i = 0; i < NUM_REGS; i++)
        printf("  R%d = %10u (0x%08x)\n", i, vm->r[i], vm->r[i]);
    printf("  PC = 0x%04x\n", vm->pc);
    printf("  SP = 0x%04x\n", vm->sp);
    printf("  ZF = %u  SF = %u\n", vm->zf, vm->sf);
}

/* ─── Hex dump of RAM ─────────────────────────────────────────────────── */
static void mem_dump(const VM *vm, uint32_t start, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t addr = (start + i) & 0xFFFF;
        if (i % 16 == 0) printf("  0x%04x: ", addr);
        printf("%02x ", vm->mem[addr]);
        if (i % 16 == 15 || i == len - 1) printf("\n");
    }
}

/* ─── Disassemble instruction at current PC ───────────────────────────── */
static void dis_at(const VM *vm, uint32_t addr) {
    if (addr + 3 >= RAM_SIZE) { printf("  <out of range>\n"); return; }
    char buf[128];
    disasm_instr(buf, sizeof(buf), addr, vm->mem);
    printf("  %s\n", buf);
}

/* ─── Trim whitespace from string in place ───────────────────────────── */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) *--e = '\0';
    return s;
}

/* ─── REPL ────────────────────────────────────────────────────────────── */
int main(void) {
    VM       vm;
    BreakSet bs = {.count = 0};
    char     prog_path[512] = "";
    int      loaded = 0;

    memset(&vm, 0, sizeof(vm));

    printf("PClub VM Debugger  (type 'help' for commands)\n");

    char line[1024];
    while (1) {
        printf("dbg> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        char *cmd = trim(line);
        if (!*cmd) continue;

        /* ── help ─────────────────────────────────────────────────────── */
        if (strcmp(cmd, "help") == 0) {
            printf(
                "  load <file>       Load binary program\n"
                "  run               Run until halt or breakpoint\n"
                "  step              Execute one instruction\n"
                "  cont              Continue from current PC\n"
                "  cont <N>          Continue N steps from breakpoint\n"
                "  break <addr>      Set breakpoint (hex or decimal)\n"
                "  unbreak <addr>    Remove breakpoint\n"
                "  regs              Print registers\n"
                "  mem <start> <len> Hex dump RAM\n"
                "  dis               Disassemble at current PC\n"
                "  dis <addr>        Disassemble at address\n"
                "  restart           Reload current program\n"
                "  quit              Exit\n"
            );
            continue;
        }

        /* ── quit ─────────────────────────────────────────────────────── */
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) break;

        /* ── load ─────────────────────────────────────────────────────── */
        if (strncmp(cmd, "load ", 5) == 0) {
            char *path = trim(cmd + 5);
            if (vm_load(&vm, path) < 0) {
                printf("  error: cannot open '%s'\n", path);
            } else {
                strncpy(prog_path, path, sizeof(prog_path)-1);
                loaded = 1;
                bs.count = 0;
                printf("  loaded '%s'\n", path);
            }
            continue;
        }

        /* ── restart ──────────────────────────────────────────────────── */
        if (strcmp(cmd, "restart") == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            if (vm_load(&vm, prog_path) < 0)
                printf("  error reloading '%s'\n", prog_path);
            else
                printf("  reloaded '%s'\n", prog_path);
            continue;
        }

        /* ── regs ─────────────────────────────────────────────────────── */
        if (strcmp(cmd, "regs") == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            print_regs(&vm);
            continue;
        }

        /* ── step ─────────────────────────────────────────────────────── */
        if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            if (vm.halted) { printf("  vm halted (exit %d)\n", vm.exit_code); continue; }
            dis_at(&vm, vm.pc);
            int r = vm_step(&vm);
            if (r == 1) printf("  halted (exit 0)\n");
            else if (r == 2) printf("  fault (exit 2)\n");
            continue;
        }

        /* ── run / cont ───────────────────────────────────────────────── */
        if (strncmp(cmd, "run", 3) == 0 || strncmp(cmd, "cont", 4) == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            if (vm.halted) { printf("  vm halted (exit %d)\n", vm.exit_code); continue; }

            /* optional step count for "cont N" */
            long max_steps = -1;
            char *sp2 = strchr(cmd, ' ');
            if (sp2) { char *arg = trim(sp2); if (*arg) max_steps = strtol(arg, NULL, 0); }

            long steps = 0;
            /* If we are already sitting ON a breakpoint (just stopped here),
               execute that instruction first so we make progress. */
            int at_bp_start = bp_has(&bs, vm.pc);

            while (!vm.halted) {
                if (max_steps >= 0 && steps >= max_steps) {
                    printf("  paused after %ld steps at PC=0x%04x\n", steps, vm.pc);
                    break;
                }
                /* Skip breakpoint check only for the very first step if we started on one */
                if (!(steps == 0 && at_bp_start) && bp_has(&bs, vm.pc)) {
                    printf("  breakpoint hit at PC=0x%04x\n", vm.pc);
                    dis_at(&vm, vm.pc);
                    break;
                }
                int r = vm_step(&vm);
                steps++;
                if (r == 1) { printf("  halted (exit 0) after %ld steps\n", steps); break; }
                if (r == 2) { printf("  fault  (exit 2) at PC=0x%04x\n", vm.pc); break; }
            }
            continue;
        }

        /* ── break ────────────────────────────────────────────────────── */
        if (strncmp(cmd, "break ", 6) == 0) {
            uint32_t addr = (uint32_t)strtoul(trim(cmd+6), NULL, 0);
            if (bp_add(&bs, addr) == 0)
                printf("  breakpoint set at 0x%04x\n", addr);
            else
                printf("  breakpoint table full\n");
            continue;
        }

        /* ── unbreak ──────────────────────────────────────────────────── */
        if (strncmp(cmd, "unbreak ", 8) == 0) {
            uint32_t addr = (uint32_t)strtoul(trim(cmd+8), NULL, 0);
            bp_del(&bs, addr);
            printf("  breakpoint removed from 0x%04x\n", addr);
            continue;
        }

        /* ── mem ──────────────────────────────────────────────────────── */
        if (strncmp(cmd, "mem ", 4) == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            uint32_t start=0, len=16;
            sscanf(cmd+4, "%i %i", (int*)&start, (int*)&len);
            mem_dump(&vm, start, len);
            continue;
        }

        /* ── dis ──────────────────────────────────────────────────────── */
        if (strcmp(cmd, "dis") == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            dis_at(&vm, vm.pc);
            continue;
        }
        if (strncmp(cmd, "dis ", 4) == 0) {
            if (!loaded) { printf("  no program loaded\n"); continue; }
            uint32_t addr = (uint32_t)strtoul(trim(cmd+4), NULL, 0);
            dis_at(&vm, addr);
            continue;
        }

        printf("  unknown command: '%s'  (try 'help')\n", cmd);
    }

    return 0;
}
