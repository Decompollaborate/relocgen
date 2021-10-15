#include <string.h>
#include <stdio.h>

#include "elf32/elf32.h"


static int g_romSize;


#define ROM_SEG_START_SUFFIX ".rom_start"
#define ROM_SEG_END_SUFFIX ".rom_end"

struct RomSegment
{
    const char *name;
    const void *data;
    int size;
    int romStart;
    int romEnd;
};


// reads a whole file into memory, and returns a pointer to the data
void *util_read_whole_file(const char *filename, size_t *pSize)
{
    FILE *file = fopen(filename, "rb");
    uint8_t *buffer;
    size_t size;

    if (file == NULL) {
        //util_fatal_error("failed to open file '%s' for reading: %s", filename, strerror(errno));
    }

    // get size
    fseek(file, 0, SEEK_END);
    size = ftell(file);

    // allocate buffer
    buffer = malloc(size + 1);

    // read file
    fseek(file, 0, SEEK_SET);
    if (fread(buffer, size, 1, file) != 1) {
        //util_fatal_error("error reading from file '%s': %s", filename, strerror(errno));
    }

    // null-terminate the buffer (in case of text files)
    buffer[size] = 0;

    fclose(file);

    if (pSize != NULL)
        *pSize = size;
    return buffer;
}


static int cmp_symbol_by_name(const void *a, const void *b)
{
    return strcmp(
        ((struct Elf32_Symbol *)a)->name,
        ((struct Elf32_Symbol *)b)->name);
}


static struct RomSegment *add_rom_segment(const char *name)
{
    /*
    int index = g_romSegmentsCount;

    g_romSegmentsCount++;
    g_romSegments = realloc(g_romSegments, g_romSegmentsCount * sizeof(*g_romSegments));
    g_romSegments[index].name = name;
    return &g_romSegments[index];
    */
    (void)name;
    return 0;
}


static int find_symbol_value(struct Elf32_Symbol *syms, int numsymbols, const char *name)
{
    struct Elf32_Symbol *sym;
    int lo, hi, mid, cmp;

    // Binary search for the symbol. We maintain the invariant that [lo, hi) is
    // the interval that remains to search.
    lo = 0;
    hi = numsymbols;
    while (lo < hi)
    {
        mid = lo + (hi - lo) / 2;
        sym = &syms[mid];
        cmp = strcmp(sym->name, name);

        if (cmp == 0)
            return (int) sym->value;
        else if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }

    //util_fatal_error("Symbol %s is not defined\n", name);
    return -1;
}

static int find_rom_address(struct Elf32_Symbol *syms, int numsymbols, const char *name, const char *suffix)
{
    (void)name;
    (void)suffix;
    char *symName /*= sprintf_alloc("_%sSegmentRom%s", name, suffix)*/ = NULL;
    int ret = find_symbol_value(syms, numsymbols, symName);
    free(symName);
    return ret;
}

static void parse_input_file(const char *filename)
{
    struct Elf32 elf;
    struct Elf32_Symbol *syms;
    const void *data;
    size_t size;
    int numRomSymbols;
    int i;

    data = util_read_whole_file(filename, &size);

    if (!elf32_init(&elf, data, size) || elf.machine != ELF_MACHINE_MIPS) {
        //util_fatal_error("%s is not a valid 32-bit MIPS ELF file", filename);
    }

    // sort all symbols that contain the substring "Rom" for fast access
    // (sorting all symbols costs 0.1s, might as well avoid that)
    syms = malloc(elf.numsymbols * sizeof(struct Elf32_Symbol));
    numRomSymbols = 0;
    for (i = 0; i < elf.numsymbols; i++)
    {
        if (!elf32_get_symbol(&elf, &syms[numRomSymbols], i)) {
            //util_fatal_error("invalid or corrupt ELF file");
        }
        if (strstr(syms[numRomSymbols].name, "Rom")) {
            numRomSymbols++;
        }
    }
    qsort(syms, numRomSymbols, sizeof(struct Elf32_Symbol), cmp_symbol_by_name);

    // get ROM segments
    // sections of type SHT_PROGBITS and  whose name is ..secname are considered ROM segments
    for (i = 0; i < elf.shnum; i++)
    {
        struct Elf32_Section sec;
        struct RomSegment *segment;

        if (!elf32_get_section(&elf, &sec, i)) {
            //util_fatal_error("invalid or corrupt ELF file");
        }

        if (sec.type == SHT_PROGBITS && sec.name[0] == '.' && sec.name[1] == '.'
        // HACK! ld sometimes marks NOLOAD sections as SHT_PROGBITS for no apparent reason,
        // so we must ignore the ..secname.bss sections explicitly
         && strchr(sec.name + 2, '.') == NULL)
        {
            segment = add_rom_segment(sec.name + 2);
            segment->data = elf.data + sec.offset;
            segment->romStart = find_rom_address(syms, numRomSymbols, segment->name, "Start");
            segment->romEnd = find_rom_address(syms, numRomSymbols, segment->name, "End");
        }
            
    }

    g_romSize = find_symbol_value(syms, numRomSymbols, "_RomSize");

    free(syms);
}


void test_func(const char *filename)
{
    struct Elf32 elf;
    struct Elf32_Symbol *syms;
    const void *data;
    size_t size;
    int numRomSymbols;
    int i;

    data = util_read_whole_file(filename, &size);

    if (!elf32_init(&elf, data, size) || elf.machine != ELF_MACHINE_MIPS) {
        fprintf(stderr, "err\n");
        exit(-1);
        //util_fatal_error("%s is not a valid 32-bit MIPS ELF file", filename);
    }

    // sort all symbols that contain the substring "Rom" for fast access
    // (sorting all symbols costs 0.1s, might as well avoid that)
    syms = malloc(elf.numsymbols * sizeof(struct Elf32_Symbol));
    numRomSymbols = 0;
    for (i = 0; i < elf.numsymbols; i++)
    {
        if (!elf32_get_symbol(&elf, &syms[numRomSymbols], i)) {
            fprintf(stderr, "err\n");
            exit(-1);
            //util_fatal_error("invalid or corrupt ELF file");
        }
        if (syms[numRomSymbols].shndx != 0) {
            printf("%s %i\n", syms[numRomSymbols].name, syms[numRomSymbols].value);
            printf("  info:  %i %i\n", ELF32_ST_BIND(syms[numRomSymbols].info), ELF32_ST_TYPE(syms[numRomSymbols].info));
            printf("  other: %i\n", syms[numRomSymbols].other);
            printf("  shndx: %i\n", syms[numRomSymbols].shndx);
        }
        if (strstr(syms[numRomSymbols].name, "Rom")) {
            numRomSymbols++;
        }
    }

    for (i = 1; i < elf.shnum; i++) {
        struct Elf32_Section sect;

        if (!elf32_get_section(&elf, &sect, i)) {
            fprintf(stderr, "err\n");
            exit(-1);
            //util_fatal_error("invalid or corrupt ELF file");
        }
        if (sect.type < SHT_LOOS) {
            printf("%i: %s\n", i, sect.name);
            printf("  type: 0x%X\n", sect.type);
            printf("  flags: %i\n", sect.flags);
            printf("  addr: %i\n", sect.addr);
            printf("  offset: %i\n", sect.offset);
            printf("  size: %i\n", sect.size);
            printf("  link: %i\n", sect.link);
            printf("  info: %i\n", sect.info);
            printf("  addralign: %i\n", sect.addralign);
            printf("  entsize: %i\n", sect.entsize);
        }

    }


    size_t relocCount = 0;

    printf("\nrel:\n");

    for (i = 0; i < elf.shnum; i++)
    {
        struct Elf32_Section sec;
        elf32_get_section(&elf, &sec, i);

        if (sec.type == SHT_REL)
        {
            printf("%i: \n", i);

            for (size_t j = 0; j < sec.size / sec.entsize; j++) {
                struct Elf32_Rel rel;

                if (!elf32_get_rel(&elf, &rel, &sec, j)) {
                    fprintf(stderr, "rel err\n");
                    exit(1);
                }

                struct Elf32_Symbol sym;
                if (!elf32_get_symbol(&elf, &sym, ELF32_R_SYM(rel.info))) {
                    fprintf(stderr, "error getting rel symbol\n");
                    exit(1);
                }

                if (sym.shndx != 0) {
                    printf(" j: %zu\n", j);
                    printf("  offset: 0x%04X\n", rel.offset);
                    printf("  SYM:    0x%04X\n", ELF32_R_SYM(rel.info));
                    printf("  TYPE:   0x%04X\n", ELF32_R_TYPE(rel.info));

                    printf("    %s %i\n", sym.name, sym.value);
                    printf("      info:  %i %i\n", ELF32_ST_BIND(sym.info), ELF32_ST_TYPE(sym.info));
                    printf("      other: %i\n", sym.other);
                    printf("      shndx: %i\n", sym.shndx);

                    relocCount++;
                }
            }
        }
    }

    printf("reloc count: %zu\n", relocCount);

#if 0
    qsort(syms, numRomSymbols, sizeof(struct Elf32_Symbol), cmp_symbol_by_name);

    // get ROM segments
    // sections of type SHT_PROGBITS and  whose name is ..secname are considered ROM segments
    for (i = 0; i < elf.shnum; i++)
    {
        struct Elf32_Section sec;
        struct RomSegment *segment;

        if (!elf32_get_section(&elf, &sec, i)) {
            //util_fatal_error("invalid or corrupt ELF file");
        }

        if (sec.type == SHT_PROGBITS && sec.name[0] == '.' && sec.name[1] == '.'
        // HACK! ld sometimes marks NOLOAD sections as SHT_PROGBITS for no apparent reason,
        // so we must ignore the ..secname.bss sections explicitly
         && strchr(sec.name + 2, '.') == NULL)
        {
            segment = add_rom_segment(sec.name + 2);
            segment->data = elf.data + sec.offset;
            segment->romStart = find_rom_address(syms, numRomSymbols, segment->name, "Start");
            segment->romEnd = find_rom_address(syms, numRomSymbols, segment->name, "End");
        }
            
    }

    g_romSize = find_symbol_value(syms, numRomSymbols, "_RomSize");
#endif
    free(syms);
}

int main(int argc, char** argv) {
    (void)argc;

    (void)parse_input_file;
    //parse_input_file(argv[1]);

    test_func(argv[1]);

    return 0;
}
