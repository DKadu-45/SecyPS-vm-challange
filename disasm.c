#include "vm_core.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <program.bin>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "failed to open: %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > RAM_SIZE) {
        fprintf(stderr, "invalid file size\n");
        fclose(f);
        return 1;
    }
    uint8_t *buf = calloc(1, RAM_SIZE);
    size_t nread = fread(buf, 1, sz, f);
    (void)nread;
    fclose(f);

    printf("%-8s  %-16s  %s\n", "ADDR", "RAW BYTES", "MNEMONIC");
    printf("%-8s  %-16s  %s\n", "--------", "----------------", "--------");

    for (long addr = 0; addr + 3 < sz; addr += 4) {
        char line[128];
        disasm_instr(line, sizeof(line), (uint32_t)addr, buf);
        printf("%s\n", line);
    }

    free(buf);
    return 0;
}
