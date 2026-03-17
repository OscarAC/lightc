#include <lightc/library.h>
#include <lightc/syscall.h>
#include <lightc/heap.h>
#include <lightc/string.h>

/* ========================================================================
 * ELF64 types and structures (internal — not exposed in the header)
 * ======================================================================== */

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

/* ELF header */
typedef struct {
    uint8_t    e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off  e_phoff;
    Elf64_Off  e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

/* Program header */
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

/* Dynamic section entry */
typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr  d_ptr;
    } d_un;
} Elf64_Dyn;

/* Symbol table entry */
typedef struct {
    Elf64_Word  st_name;
    uint8_t     st_info;
    uint8_t     st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
} Elf64_Sym;

/* Relocation entry with addend */
typedef struct {
    Elf64_Addr    r_offset;
    Elf64_Xword   r_info;
    Elf64_Sxword  r_addend;
} Elf64_Rela;

/* ========================================================================
 * ELF constants
 * ======================================================================== */

/* ELF identification */
#define ELFMAG0    0x7f
#define ELFMAG1    'E'
#define ELFMAG2    'L'
#define ELFMAG3    'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_DYN     3

/* Machine types */
#define EM_X86_64  62
#define EM_AARCH64 183

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2

/* Program header flags */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Dynamic tags */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_GNU_HASH     0x6ffffef5
#define DT_RELACOUNT    0x6ffffff9

/* Symbol binding/type extraction */
#define ELF64_ST_BIND(i)  ((i) >> 4)
#define ELF64_ST_TYPE(i)  ((i) & 0xf)
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STT_FUNC   2
#define STT_OBJECT 1
#define SHN_UNDEF  0

/* Relocation types (x86_64) */
#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

/* Relocation types (aarch64) */
#define R_AARCH64_NONE      0
#define R_AARCH64_RELATIVE  1027
#define R_AARCH64_GLOB_DAT  1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_ABS64     257

/* Relocation info extraction */
#define ELF64_R_SYM(i)   ((i) >> 32)
#define ELF64_R_TYPE(i)  ((i) & 0xffffffff)

/* ========================================================================
 * Library struct
 * ======================================================================== */

#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(x) ((x) & ~((uint64_t)(PAGE_SIZE - 1)))
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & ~((uint64_t)(PAGE_SIZE - 1)))

struct lc_library {
    uint8_t *base;           /* mmap'd base address */
    size_t   total_size;     /* total mmap'd size */

    /* Parsed from DYNAMIC segment */
    Elf64_Sym  *symtab;      /* symbol table */
    char       *strtab;      /* string table */
    size_t      strtab_size;
    Elf64_Rela *rela;        /* relocation table */
    size_t      rela_count;

    /* ELF hash table (for symbol lookup) */
    uint32_t   *elf_hash;

    /* GNU hash table (for fast symbol lookup) */
    uint32_t   *gnu_hash;

    /* Init/fini */
    void      **init_array;
    size_t      init_array_count;
    void      **fini_array;
    size_t      fini_array_count;
    void       (*init_func)(void);
    void       (*fini_func)(void);
};

/* ========================================================================
 * Error handling
 * ======================================================================== */

static const char *last_error = NULL;

static void set_error(const char *msg) {
    last_error = msg;
}

const char *lc_library_error(void) {
    const char *err = last_error;
    last_error = NULL;
    return err;
}

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/* Read exactly `count` bytes from `fd`, retrying on short reads. */
static bool read_all(int32_t fd, void *buf, size_t count) {
    uint8_t *p = (uint8_t *)buf;
    while (count > 0) {
        lc_sysret n = lc_kernel_read_bytes(fd, p, count);
        if (n <= 0) return false;
        p += n;
        count -= (size_t)n;
    }
    return true;
}

/* Convert ELF program header flags to mmap prot flags. */
static int32_t prot_from_elf_flags(uint32_t p_flags) {
    int32_t prot = 0;
    if (p_flags & PF_R) prot |= PROT_READ;
    if (p_flags & PF_W) prot |= PROT_WRITE;
    if (p_flags & PF_X) prot |= PROT_EXEC;
    return prot;
}

/* Simple string comparison (null-terminated). */
static bool str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

/* ELF hash function (for DT_HASH lookup). */
static uint32_t elf_hash_function(const char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* ========================================================================
 * ELF validation
 * ======================================================================== */

static bool validate_elf_header(const Elf64_Ehdr *ehdr) {
    /* Check magic bytes */
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        set_error("not an ELF file (bad magic)");
        return false;
    }

    /* Must be 64-bit */
    if (ehdr->e_ident[4] != ELFCLASS64) {
        set_error("not a 64-bit ELF file");
        return false;
    }

    /* Must be little-endian */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        set_error("not a little-endian ELF file");
        return false;
    }

    /* Must be a shared object (ET_DYN) */
    if (ehdr->e_type != ET_DYN) {
        set_error("not a shared object (expected ET_DYN)");
        return false;
    }

    /* Check machine type matches current arch */
#if defined(__x86_64__)
    if (ehdr->e_machine != EM_X86_64) {
        set_error("ELF machine type is not x86_64");
        return false;
    }
#elif defined(__aarch64__)
    if (ehdr->e_machine != EM_AARCH64) {
        set_error("ELF machine type is not aarch64");
        return false;
    }
#endif

    return true;
}

/* ========================================================================
 * Relocation processing
 * ======================================================================== */

/* Forward declaration — defined in the symbol lookup section below. */
static size_t get_symbol_count(lc_library *lib);

static bool process_relocations(lc_library *lib) {
    uint8_t *base = lib->base;
    size_t sym_count = get_symbol_count(lib);

    for (size_t i = 0; i < lib->rela_count; i++) {
        Elf64_Rela *rela = &lib->rela[i];
        uint64_t *target = (uint64_t *)(base + rela->r_offset);
        uint32_t type = ELF64_R_TYPE(rela->r_info);
        uint32_t sym_idx = ELF64_R_SYM(rela->r_info);

        /* Validate sym_idx against symbol table size */
        if (sym_idx != 0 && sym_count > 0 && sym_idx >= sym_count) continue;

#if defined(__x86_64__)
        switch (type) {
            case R_X86_64_NONE:
                break;

            case R_X86_64_RELATIVE:
                *target = (uint64_t)base + rela->r_addend;
                break;

            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT: {
                Elf64_Sym *sym = &lib->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    *target = (uint64_t)base + sym->st_value + rela->r_addend;
                }
                break;
            }

            case R_X86_64_64: {
                Elf64_Sym *sym = &lib->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    *target = (uint64_t)base + sym->st_value + rela->r_addend;
                }
                break;
            }

            default:
                /* Unknown relocation type — skip it */
                break;
        }
#elif defined(__aarch64__)
        switch (type) {
            case R_AARCH64_NONE:
                break;

            case R_AARCH64_RELATIVE:
                *target = (uint64_t)base + rela->r_addend;
                break;

            case R_AARCH64_GLOB_DAT:
            case R_AARCH64_JUMP_SLOT: {
                Elf64_Sym *sym = &lib->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    *target = (uint64_t)base + sym->st_value + rela->r_addend;
                }
                break;
            }

            case R_AARCH64_ABS64: {
                Elf64_Sym *sym = &lib->symtab[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    *target = (uint64_t)base + sym->st_value + rela->r_addend;
                }
                break;
            }

            default:
                break;
        }
#endif
    }
    return true;
}

/* ========================================================================
 * Symbol lookup
 * ======================================================================== */

/* GNU hash function (djb2-style, used by DT_GNU_HASH). */
static uint32_t gnu_hash_function(const char *name) {
    uint32_t h = 5381;
    while (*name) {
        h = (h << 5) + h + (uint8_t)*name++;
    }
    return h;
}

/*
 * GNU hash table layout:
 *   uint32_t nbuckets
 *   uint32_t symndx        — first symbol index in chains
 *   uint32_t maskwords     — number of Bloom filter words
 *   uint32_t shift2        — Bloom filter shift
 *   uint64_t bloom[maskwords]  (Elf64: 64-bit words)
 *   uint32_t buckets[nbuckets]
 *   uint32_t chains[...]   — one per symbol starting at symndx
 *
 * Chain values have bit 0 set on the last entry in the chain.
 */
static void *find_symbol_with_gnu_hash(lc_library *lib, const char *name) {
    uint32_t *hash_table = lib->gnu_hash;

    uint32_t nbuckets  = hash_table[0];
    uint32_t symndx    = hash_table[1];
    uint32_t maskwords = hash_table[2];
    /* hash_table[3] is shift2, used for Bloom filter — we skip Bloom check */

    /* Bloom filter: 64-bit words start at &hash_table[4] */
    uint64_t *bloom = (uint64_t *)&hash_table[4];

    /* Buckets follow the Bloom filter */
    uint32_t *buckets = (uint32_t *)(bloom + maskwords);

    /* Chains follow the buckets */
    uint32_t *chains = buckets + nbuckets;

    uint32_t hash = gnu_hash_function(name);

    /* Bloom filter check (optional, skip for simplicity — go straight to bucket) */

    uint32_t bucket_idx = hash % nbuckets;
    uint32_t sym_idx = buckets[bucket_idx];

    if (sym_idx == 0) return NULL; /* empty bucket */
    if (sym_idx < symndx) return NULL; /* shouldn't happen */

    /* Walk the chain */
    uint32_t chain_idx = sym_idx - symndx;
    uint32_t hash1 = hash | 1; /* set bit 0 for comparison (chains store hash with bit 0 as end marker) */

    for (;;) {
        uint32_t chain_val = chains[chain_idx];

        /* Compare hashes (ignoring bit 0 which is the end-of-chain marker) */
        if ((chain_val | 1) == hash1) {
            /* Hash matches — compare the actual name */
            Elf64_Sym *sym = &lib->symtab[symndx + chain_idx];
            if (sym->st_name < lib->strtab_size &&
                str_equal(lib->strtab + sym->st_name, name)) {
                if (sym->st_shndx != SHN_UNDEF) {
                    return (void *)(lib->base + sym->st_value);
                }
            }
        }

        /* Bit 0 set means end of chain */
        if (chain_val & 1) break;
        chain_idx++;
    }

    return NULL;
}

/* Look up a symbol using the ELF hash table (DT_HASH). */
static void *find_symbol_with_elf_hash(lc_library *lib, const char *name) {
    uint32_t *hash_table = lib->elf_hash;
    uint32_t nbucket = hash_table[0];
    /* nchain = hash_table[1] — also the number of symbols */
    uint32_t *buckets = &hash_table[2];
    uint32_t *chains  = &hash_table[2 + nbucket];

    uint32_t hash = elf_hash_function(name);
    uint32_t idx = buckets[hash % nbucket];

    while (idx != 0) {
        Elf64_Sym *sym = &lib->symtab[idx];
        if (sym->st_name < lib->strtab_size &&
            str_equal(lib->strtab + sym->st_name, name)) {
            if (sym->st_shndx != SHN_UNDEF) {
                return (void *)(lib->base + sym->st_value);
            }
        }
        idx = chains[idx];
    }
    return NULL;
}

/* Determine the total number of symbols using available hash tables.
 * With DT_HASH: nchain gives exact count.
 * With DT_GNU_HASH: walk buckets/chains to find the highest symbol index. */
static size_t get_symbol_count(lc_library *lib) {
    if (lib->elf_hash) {
        return lib->elf_hash[1]; /* nchain */
    }

    if (lib->gnu_hash) {
        uint32_t *hash_table = lib->gnu_hash;
        uint32_t nbuckets  = hash_table[0];
        uint32_t symndx    = hash_table[1];
        uint32_t maskwords = hash_table[2];

        uint64_t *bloom = (uint64_t *)&hash_table[4];
        uint32_t *buckets = (uint32_t *)(bloom + maskwords);
        uint32_t *chains  = buckets + nbuckets;

        /* Find the highest occupied bucket */
        uint32_t max_sym = symndx;
        for (uint32_t i = 0; i < nbuckets; i++) {
            if (buckets[i] > max_sym) {
                max_sym = buckets[i];
            }
        }

        if (max_sym < symndx) return symndx; /* no symbols in hash */

        /* Walk the chain from max_sym to find the end */
        uint32_t chain_idx = max_sym - symndx;
        while (!(chains[chain_idx] & 1)) {
            chain_idx++;
        }
        /* Total symbols = symndx + chain_idx + 1 */
        return symndx + chain_idx + 1;
    }

    return 0;
}

/* Look up a symbol by linear scan of the symbol table. */
static void *find_symbol_linear_scan(lc_library *lib, const char *name) {
    size_t sym_count = get_symbol_count(lib);

    for (size_t i = 1; i < sym_count; i++) { /* skip index 0 (STN_UNDEF) */
        Elf64_Sym *sym = &lib->symtab[i];
        uint8_t bind = ELF64_ST_BIND(sym->st_info);

        if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
        if (sym->st_shndx == SHN_UNDEF) continue;
        if (sym->st_name >= lib->strtab_size) continue;

        if (str_equal(lib->strtab + sym->st_name, name)) {
            return (void *)(lib->base + sym->st_value);
        }
    }
    return NULL;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

lc_library *lc_library_open(const char *path) {
    last_error = NULL;

    /* --- Step 1: Open the file --- */
    lc_sysret fd_ret = lc_kernel_open_file(path, O_RDONLY, 0);
    if (fd_ret < 0) {
        set_error("could not open file");
        return NULL;
    }
    int32_t fd = (int32_t)fd_ret;

    /* --- Step 2: Read and validate ELF header --- */
    Elf64_Ehdr ehdr;
    if (!read_all(fd, &ehdr, sizeof(ehdr))) {
        set_error("could not read ELF header");
        lc_kernel_close_file(fd);
        return NULL;
    }
    if (!validate_elf_header(&ehdr)) {
        lc_kernel_close_file(fd);
        return NULL;
    }

    /* --- Step 3: Read program headers --- */
    size_t phdr_table_size = (size_t)ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr *phdrs = (Elf64_Phdr *)lc_heap_allocate(phdr_table_size);
    if (!phdrs) {
        set_error("could not allocate memory for program headers");
        lc_kernel_close_file(fd);
        return NULL;
    }

    if (lc_kernel_seek_position(fd, (int64_t)ehdr.e_phoff, SEEK_SET) < 0) {
        set_error("could not seek to program headers");
        lc_heap_free(phdrs);
        lc_kernel_close_file(fd);
        return NULL;
    }
    if (!read_all(fd, phdrs, phdr_table_size)) {
        set_error("could not read program headers");
        lc_heap_free(phdrs);
        lc_kernel_close_file(fd);
        return NULL;
    }

    /* --- Step 4: Compute total address span of LOAD segments --- */
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    bool found_load = false;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        found_load = true;
        if (phdrs[i].p_vaddr < min_vaddr) {
            min_vaddr = phdrs[i].p_vaddr;
        }
        uint64_t end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        if (end > max_vaddr) {
            max_vaddr = end;
        }
    }

    if (!found_load) {
        set_error("no LOAD segments found in ELF file");
        lc_heap_free(phdrs);
        lc_kernel_close_file(fd);
        return NULL;
    }

    /* Align min_vaddr down and max_vaddr up to page boundaries */
    min_vaddr = PAGE_ALIGN_DOWN(min_vaddr);
    size_t total_size = (size_t)PAGE_ALIGN_UP(max_vaddr - min_vaddr);

    /* --- Step 5: Map a single contiguous anonymous region --- */
    void *base_ptr = lc_kernel_map_memory(
        NULL, total_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
    if (base_ptr == MAP_FAILED) {
        set_error("mmap failed for library region");
        lc_heap_free(phdrs);
        lc_kernel_close_file(fd);
        return NULL;
    }
    uint8_t *base = (uint8_t *)base_ptr;

    /* Zero the entire region (MAP_ANONYMOUS should do this, but be safe for .bss) */

    /* --- Step 6: Read each LOAD segment into the mapped region --- */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        if (phdrs[i].p_filesz == 0) continue;

        uint64_t offset_in_map = phdrs[i].p_vaddr - min_vaddr;

        if (lc_kernel_seek_position(fd, (int64_t)phdrs[i].p_offset, SEEK_SET) < 0) {
            set_error("could not seek to LOAD segment");
            lc_kernel_unmap_memory(base, total_size);
            lc_heap_free(phdrs);
            lc_kernel_close_file(fd);
            return NULL;
        }
        if (!read_all(fd, base + offset_in_map, (size_t)phdrs[i].p_filesz)) {
            set_error("could not read LOAD segment data");
            lc_kernel_unmap_memory(base, total_size);
            lc_heap_free(phdrs);
            lc_kernel_close_file(fd);
            return NULL;
        }
        /* .bss (memsz > filesz) is already zero from MAP_ANONYMOUS */
    }

    /* Done with the file */
    lc_kernel_close_file(fd);

    /* --- Step 7: Set correct page permissions with mprotect --- */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        uint64_t seg_start = phdrs[i].p_vaddr - min_vaddr;
        uint64_t seg_end   = seg_start + phdrs[i].p_memsz;

        uint64_t page_start = PAGE_ALIGN_DOWN(seg_start);
        uint64_t page_end   = PAGE_ALIGN_UP(seg_end);

        int32_t prot = prot_from_elf_flags(phdrs[i].p_flags);

        lc_sysret ret = lc_kernel_protect_memory(
            base + page_start,
            (size_t)(page_end - page_start),
            prot
        );
        if (ret < 0) {
            set_error("mprotect failed for LOAD segment");
            lc_kernel_unmap_memory(base, total_size);
            lc_heap_free(phdrs);
            return NULL;
        }
    }

    /* --- Step 8: Allocate and initialize the library struct --- */
    lc_library *lib = (lc_library *)lc_heap_allocate_zeroed(sizeof(lc_library));
    if (!lib) {
        set_error("could not allocate library struct");
        lc_kernel_unmap_memory(base, total_size);
        lc_heap_free(phdrs);
        return NULL;
    }
    lib->base = base;
    lib->total_size = total_size;

    /* --- Step 9: Find and parse the DYNAMIC segment --- */
    Elf64_Dyn *dynamic = NULL;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dynamic = (Elf64_Dyn *)(base + phdrs[i].p_vaddr - min_vaddr);
            break;
        }
    }

    lc_heap_free(phdrs);

    if (!dynamic) {
        set_error("no DYNAMIC segment found");
        lc_kernel_unmap_memory(base, total_size);
        lc_heap_free(lib);
        return NULL;
    }

    /* Walk the dynamic table. All d_ptr values are virtual addresses
     * relative to the library's load base (for ET_DYN). */
    size_t rela_total_size = 0;
    size_t rela_entry_size = 0;

    for (Elf64_Dyn *dyn = dynamic; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_SYMTAB:
                lib->symtab = (Elf64_Sym *)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_STRTAB:
                lib->strtab = (char *)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_STRSZ:
                lib->strtab_size = (size_t)dyn->d_un.d_val;
                break;
            case DT_RELA:
                lib->rela = (Elf64_Rela *)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_RELASZ:
                rela_total_size = (size_t)dyn->d_un.d_val;
                break;
            case DT_RELAENT:
                rela_entry_size = (size_t)dyn->d_un.d_val;
                break;
            case DT_HASH:
                lib->elf_hash = (uint32_t *)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_GNU_HASH:
                lib->gnu_hash = (uint32_t *)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_INIT:
                lib->init_func = (void (*)(void))(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_FINI:
                lib->fini_func = (void (*)(void))(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_INIT_ARRAY:
                lib->init_array = (void **)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_INIT_ARRAYSZ:
                lib->init_array_count = (size_t)dyn->d_un.d_val / sizeof(void *);
                break;
            case DT_FINI_ARRAY:
                lib->fini_array = (void **)(base + dyn->d_un.d_ptr - min_vaddr);
                break;
            case DT_FINI_ARRAYSZ:
                lib->fini_array_count = (size_t)dyn->d_un.d_val / sizeof(void *);
                break;
            default:
                break;
        }
    }

    /* Compute relocation count */
    if (rela_entry_size > 0 && rela_total_size > 0) {
        lib->rela_count = rela_total_size / rela_entry_size;
    }

    /* --- Step 10: Process relocations --- */
    if (lib->rela && lib->rela_count > 0) {
        /* Need the data segment writable for relocations.
         * It should still be writable at this point since we set permissions above
         * with the correct flags from the ELF. If not, we may need to temporarily
         * make it writable. For typical .so files, the data segment is already RW. */
        if (!process_relocations(lib)) {
            set_error("relocation processing failed");
            lc_kernel_unmap_memory(base, total_size);
            lc_heap_free(lib);
            return NULL;
        }
    }

    /* --- Step 11: Run constructors --- */
    if (lib->init_func) {
        lib->init_func();
    }
    if (lib->init_array) {
        for (size_t i = 0; i < lib->init_array_count; i++) {
            if (lib->init_array[i]) {
                typedef void (*init_fn)(void);
                init_fn fn = (init_fn)lib->init_array[i];
                fn();
            }
        }
    }

    return lib;
}

void *lc_library_find_symbol(lc_library *lib, const char *name) {
    if (!lib || !name) return NULL;
    if (!lib->symtab || !lib->strtab) {
        set_error("library has no symbol table");
        return NULL;
    }

    /* Try GNU hash table first (fast O(1) lookup, most common) */
    if (lib->gnu_hash) {
        return find_symbol_with_gnu_hash(lib, name);
    }

    /* Try ELF hash table (older format, still fast) */
    if (lib->elf_hash) {
        return find_symbol_with_elf_hash(lib, name);
    }

    /* Fall back to linear scan */
    return find_symbol_linear_scan(lib, name);
}

void lc_library_close(lc_library *lib) {
    if (!lib) return;

    /* Run destructors in reverse order */
    if (lib->fini_array) {
        for (size_t i = lib->fini_array_count; i > 0; i--) {
            if (lib->fini_array[i - 1]) {
                typedef void (*fini_fn)(void);
                fini_fn fn = (fini_fn)lib->fini_array[i - 1];
                fn();
            }
        }
    }
    if (lib->fini_func) {
        lib->fini_func();
    }

    /* Unmap the loaded library */
    if (lib->base && lib->total_size > 0) {
        lc_kernel_unmap_memory(lib->base, lib->total_size);
    }

    /* Free the library struct */
    lc_heap_free(lib);
}
