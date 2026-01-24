#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

// ELF相关定义
#define EI_NIDENT 16

// ELF文件头
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

// ELF节头
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

// ELF符号表项
typedef struct {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

typedef struct
{
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFF_HEADER;

typedef struct
{
    union
    {
        char Name[8];
        struct
        {
            uint32_t Zeroes;
            uint32_t Offset;
        } NameOffset;
    } Name;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
} COFF_SYMBOL;

typedef struct
{
    char *name;
    uint32_t value;
    int16_t section;
    uint8_t storageClass;
} Symbol;

typedef struct
{
    char *filepath;
    char *macro;
    Symbol *symbols;
    int symbolCount;
} ObjectFile;

static void free_symbols(Symbol *syms, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(syms[i].name);
    }
    free(syms);
}

static char *my_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d)
        memcpy(d, s, len);
    return d;
}

// 解析COFF格式对象文件
static int parse_coff(const char *filename, Symbol **outSymbols, int *outCount);

// 解析ELF格式对象文件
static int parse_elf(const char *filename, Symbol **outSymbols, int *outCount)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 0;
    }

    // 读取ELF头
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
    {
        fprintf(stderr, "Error reading ELF header from '%s'\n", filename);
        fclose(f);
        return 0;
    }

    // 验证ELF魔数
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
    {
        fprintf(stderr, "File '%s' is not a valid ELF file\n", filename);
        fclose(f);
        return 0;
    }

    // 检查是否是64位ELF（我们只支持64位）
    if (ehdr.e_ident[4] != 2) // ELFCLASS64 = 2
    {
        fprintf(stderr, "File '%s' is not a 64-bit ELF file (class=%d)\n", 
                filename, ehdr.e_ident[4]);
        fclose(f);
        return 0;
    }

    // 检查是否是小端（我们只支持小端）
    if (ehdr.e_ident[5] != 1) // ELFDATA2LSB = 1
    {
        fprintf(stderr, "File '%s' is not little-endian ELF (data=%d)\n", 
                filename, ehdr.e_ident[5]);
        fclose(f);
        return 0;
    }

    // 检查是否是对象文件（ET_REL = 1）
    if (ehdr.e_type != 1)
    {
        fprintf(stderr, "File '%s' is not a relocatable object file (type=%d)\n", 
                filename, ehdr.e_type);
        fclose(f);
        return 0;
    }

    // 读取节头表
    Elf64_Shdr *shdrs = malloc(ehdr.e_shnum * sizeof(Elf64_Shdr));
    if (!shdrs)
    {
        fprintf(stderr, "Memory allocation failed for section headers\n");
        fclose(f);
        return 0;
    }

    fseek(f, ehdr.e_shoff, SEEK_SET);
    if (fread(shdrs, sizeof(Elf64_Shdr), ehdr.e_shnum, f) != ehdr.e_shnum)
    {
        fprintf(stderr, "Error reading section headers from '%s'\n", filename);
        free(shdrs);
        fclose(f);
        return 0;
    }

    // 读取节头字符串表
    if (ehdr.e_shstrndx >= ehdr.e_shnum)
    {
        fprintf(stderr, "Invalid section header string table index in '%s'\n", filename);
        free(shdrs);
        fclose(f);
        return 0;
    }

    char *shstrtab = malloc(shdrs[ehdr.e_shstrndx].sh_size);
    if (!shstrtab)
    {
        fprintf(stderr, "Memory allocation failed for section header string table\n");
        free(shdrs);
        fclose(f);
        return 0;
    }

    fseek(f, shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    if (fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f) != shdrs[ehdr.e_shstrndx].sh_size)
    {
        fprintf(stderr, "Error reading section header string table from '%s'\n", filename);
        free(shstrtab);
        free(shdrs);
        fclose(f);
        return 0;
    }

    // 查找符号表（.symtab）和对应的字符串表（.strtab）
    Elf64_Shdr *symtab_shdr = NULL;
    Elf64_Shdr *strtab_shdr = NULL;
    
    for (int i = 0; i < ehdr.e_shnum; i++)
    {
        const char *name = shstrtab + shdrs[i].sh_name;
        if (strcmp(name, ".symtab") == 0)
        {
            symtab_shdr = &shdrs[i];
        }
        else if (strcmp(name, ".strtab") == 0)
        {
            strtab_shdr = &shdrs[i];
        }
    }

    if (!symtab_shdr || !strtab_shdr)
    {
        fprintf(stderr, "Symbol table or string table not found in '%s'\n", filename);
        free(shstrtab);
        free(shdrs);
        fclose(f);
        return 0;
    }

    // 读取字符串表
    char *strtab = malloc(strtab_shdr->sh_size);
    if (!strtab)
    {
        fprintf(stderr, "Memory allocation failed for string table\n");
        free(shstrtab);
        free(shdrs);
        fclose(f);
        return 0;
    }

    fseek(f, strtab_shdr->sh_offset, SEEK_SET);
    if (fread(strtab, 1, strtab_shdr->sh_size, f) != strtab_shdr->sh_size)
    {
        fprintf(stderr, "Error reading string table from '%s'\n", filename);
        free(strtab);
        free(shstrtab);
        free(shdrs);
        fclose(f);
        return 0;
    }

    // 计算符号数量
    size_t sym_count = symtab_shdr->sh_size / symtab_shdr->sh_entsize;
    
    // 分配符号数组（最多这么多符号）
    Symbol *symbols = malloc(sym_count * sizeof(Symbol));
    if (!symbols)
    {
        fprintf(stderr, "Memory allocation failed for symbols\n");
        free(strtab);
        free(shstrtab);
        free(shdrs);
        fclose(f);
        return 0;
    }

    // 读取符号表
    fseek(f, symtab_shdr->sh_offset, SEEK_SET);
    int symCount = 0;
    
    for (size_t i = 0; i < sym_count; i++)
    {
        Elf64_Sym sym;
        if (fread(&sym, sizeof(Elf64_Sym), 1, f) != 1)
        {
            fprintf(stderr, "Error reading symbol %zu from '%s'\n", i, filename);
            break;
        }

        // 跳过空名称的符号
        if (sym.st_name == 0)
            continue;

        // 获取符号名称
        if (sym.st_name >= strtab_shdr->sh_size)
        {
            fprintf(stderr, "Symbol name offset out of range in '%s'\n", filename);
            continue;
        }

        const char *symName = strtab + sym.st_name;

        // 只保留以 "_binary_" 开头的符号
        if (strncmp(symName, "_binary_", 8) == 0)
        {
            symbols[symCount].name = my_strdup(symName);
            symbols[symCount].value = (uint32_t)sym.st_value;
            symbols[symCount].section = (int16_t)sym.st_shndx;
            symbols[symCount].storageClass = 0; // ELF没有storage class概念
            symCount++;
        }
    }

    free(strtab);
    free(shstrtab);
    free(shdrs);
    fclose(f);

    *outSymbols = symbols;
    *outCount = symCount;
    return 1;
}

// 解析对象文件（自动检测格式）
static int parse_object_file(const char *filename, Symbol **outSymbols, int *outCount)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 0;
    }

    // 读取魔数
    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4)
    {
        fprintf(stderr, "Error reading magic number from '%s'\n", filename);
        fclose(f);
        return 0;
    }
    fclose(f);

    // 检测文件格式
    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
    {
        // ELF格式
        return parse_elf(filename, outSymbols, outCount);
    }
    else
    {
        // 假设是COFF格式
        // 重命名原来的parse_coff函数为parse_coff_internal，然后调用它
        return parse_coff(filename, outSymbols, outCount);
    }
}

// 原来的parse_coff函数，现在只处理COFF格式
static int parse_coff(const char *filename, Symbol **outSymbols, int *outCount)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 0;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fileSize < 4)
    {
        fprintf(stderr, "File '%s' is too small (%ld bytes)\n", filename, fileSize);
        fclose(f);
        return 0;
    }
    
    // 检查是否是ELF文件
    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4)
    {
        fprintf(stderr, "Error reading magic number from '%s'\n", filename);
        fclose(f);
        return 0;
    }
    
    // ELF魔数: 0x7F 'E' 'L' 'F'
    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
    {
        fprintf(stderr, "File '%s' appears to be in ELF format, not COFF format. This tool only supports COFF object files.\n", filename);
        fclose(f);
        return 0;
    }
    
    // 重置文件指针到开头
    fseek(f, 0, SEEK_SET);
    
    if (fileSize < (long)sizeof(COFF_HEADER))
    {
        fprintf(stderr, "File '%s' is too small to be a valid COFF file (%ld bytes)\n", filename, fileSize);
        fclose(f);
        return 0;
    }

    COFF_HEADER hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        fprintf(stderr, "Error reading COFF header from '%s'\n", filename);
        fclose(f);
        return 0;
    }
    
    // 简单的合理性检查
    if (hdr.NumberOfSymbols > 1000000)  // 不合理的符号数量
    {
        fprintf(stderr, "Suspicious number of symbols in '%s': %u\n", filename, hdr.NumberOfSymbols);
        fclose(f);
        return 0;
    }
    
    if (hdr.PointerToSymbolTable >= (uint32_t)fileSize)
    {
        fprintf(stderr, "Symbol table pointer out of range in '%s': %u (file size: %ld)\n", 
                filename, hdr.PointerToSymbolTable, fileSize);
        fclose(f);
        return 0;
    }

    // 跳转到符号表
    fseek(f, hdr.PointerToSymbolTable, SEEK_SET);

    // 读取字符串表大小
    uint32_t strTableSize = 0;
    fseek(f, hdr.PointerToSymbolTable + hdr.NumberOfSymbols * 18, SEEK_SET);
    if (fread(&strTableSize, 4, 1, f) != 1)
    {
        fprintf(stderr, "Error reading string table size from '%s'\n", filename);
        fclose(f);
        return 0;
    }

    // 读取字符串表
    char *strTable = NULL;
    if (strTableSize > 4)
    {
        strTable = malloc(strTableSize);
        if (!strTable)
        {
            fprintf(stderr, "Memory allocation failed for string table\n");
            fclose(f);
            return 0;
        }
        fseek(f, hdr.PointerToSymbolTable + hdr.NumberOfSymbols * 18, SEEK_SET);
        if (fread(strTable, 1, strTableSize, f) != strTableSize)
        {
            fprintf(stderr, "Error reading string table from '%s'\n", filename);
            free(strTable);
            fclose(f);
            return 0;
        }
    }

    // 分配符号数组
    Symbol *symbols = malloc(hdr.NumberOfSymbols * sizeof(Symbol));
    if (!symbols)
    {
        fprintf(stderr, "Memory allocation failed for symbols\n");
        free(strTable);
        fclose(f);
        return 0;
    }

    int symCount = 0;
    fseek(f, hdr.PointerToSymbolTable, SEEK_SET);
    for (uint32_t i = 0; i < hdr.NumberOfSymbols; i++)
    {
        COFF_SYMBOL sym;
        if (fread(&sym, 18, 1, f) != 1)
        {
            fprintf(stderr, "Error reading symbol %u from '%s'\n", i, filename);
            break;
        }

        char symName[256];
        if (sym.Name.NameOffset.Zeroes == 0)
        {
            // 长名称
            uint32_t offset = sym.Name.NameOffset.Offset;
            if (strTable && offset < strTableSize)
            {
                strcpy(symName, strTable + offset);
            }
            else
            {
                sprintf(symName, "?offset=%u", offset);
            }
        }
        else
        {
            // 短名称
            memcpy(symName, sym.Name.Name, 8);
            symName[8] = '\0';
            // 修剪尾随空格
            for (int j = 7; j >= 0; j--)
            {
                if (symName[j] == ' ')
                {
                    symName[j] = '\0';
                }
                else
                {
                    break;
                }
            }
        }

        // 只保留以 "_binary_" 开头的符号
        if (strncmp(symName, "_binary_", 8) == 0)
        {
            symbols[symCount].name = my_strdup(symName);
            symbols[symCount].value = sym.Value;
            symbols[symCount].section = sym.SectionNumber;
            symbols[symCount].storageClass = sym.StorageClass;
            symCount++;
        }

        // 跳过辅助符号
        if (sym.NumberOfAuxSymbols > 0)
        {
            fseek(f, sym.NumberOfAuxSymbols * 18, SEEK_CUR);
            i += sym.NumberOfAuxSymbols;
        }
    }

    free(strTable);
    fclose(f);

    *outSymbols = symbols;
    *outCount = symCount;
    return 1;
}

// 规范化路径：确保不以路径分隔符结尾，统一使用正斜杠
static void normalize_path(char *out, size_t outSize, const char *path)
{
    size_t len = strlen(path);
    if (len == 0)
    {
        out[0] = '\0';
        return;
    }
    
    // 统一使用正斜杠作为分隔符
    char sep = '/';
    
    // 复制路径并转换反斜杠为正斜杠
    strncpy(out, path, outSize - 1);
    out[outSize - 1] = '\0';
    
    // 将反斜杠转换为正斜杠
    for (char *p = out; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }
    
    // 移除尾随分隔符
    size_t outLen = strlen(out);
    while (outLen > 0 && out[outLen - 1] == sep)
    {
        out[outLen - 1] = '\0';
        outLen--;
    }
}

// 将字符串转换为大写
static void to_uppercase(char *str)
{
    for (char *p = str; *p; p++)
    {
        *p = toupper((unsigned char)*p);
    }
}

static void generate_header(const char *outDir, const char *baseName, const char *macro, Symbol *symbols, int count)
{
    char headerPath[1024];
    char normalizedDir[1024];
    
    // 规范化输出目录
    normalize_path(normalizedDir, sizeof(normalizedDir), outDir);
    
    // 统一使用正斜杠拼接路径
    snprintf(headerPath, sizeof(headerPath), "%s/%s.h", normalizedDir, baseName);

    FILE *h = fopen(headerPath, "w");
    if (!h)
    {
        fprintf(stderr, "Error creating header file '%s': %s\n", headerPath, strerror(errno));
        return;
    }

    // 创建清理后的宏名称（将点号替换为下划线）
    char cleanName[256];
    strncpy(cleanName, baseName, sizeof(cleanName) - 1);
    cleanName[sizeof(cleanName) - 1] = '\0';
    for (char *p = cleanName; *p; p++)
    {
        if (*p == '.')
            *p = '_';
    }
    
    // 转换为大写
    to_uppercase(cleanName);

    fprintf(h, "// Auto-generated header from %s.o\n", baseName);
    fprintf(h, "#ifndef _INCLUDE_%s_H_\n", cleanName);
    fprintf(h, "#define _INCLUDE_%s_H_\n\n", cleanName);

    for (int i = 0; i < count; i++)
    {
        const char *name = symbols[i].name;
        // 根据后缀确定类型
        if (strstr(name, "_size"))
        {
            fprintf(h, "extern const unsigned int %s;\n", name);
        }
        else if (strstr(name, "_start") || strstr(name, "_end"))
        {
            fprintf(h, "extern const unsigned char %s[];\n", name);
        }
        else
        {
            fprintf(h, "extern const unsigned char %s[];\n", name);
        }
    }

    if (macro && macro[0])
    {
        fprintf(h, "\n// Macros for convenience\n");
        for (int i = 0; i < count; i++)
        {
            const char *name = symbols[i].name;
            // 提取后缀
            const char *suffix = strrchr(name, '_');
            if (suffix)
            {
                suffix++; // 跳过下划线
                char macroName[256];
                snprintf(macroName, sizeof(macroName), "%s_%s", macro, suffix);
                // 将宏名称转换为大写
                to_uppercase(macroName);
                fprintf(h, "#define %s %s\n", macroName, name);
            }
        }
    }

    fprintf(h, "\n#endif // _INCLUDE_%s_H_\n", cleanName);
    fclose(h);
    printf("Generated header: %s\n", headerPath);
}

static void generate_combined_header(const char *outDir, const char *headerName, ObjectFile *files, int fileCount)
{
    char headerPath[1024];
    char normalizedDir[1024];
    
    // 规范化输出目录
    normalize_path(normalizedDir, sizeof(normalizedDir), outDir);
    
    // 检查headerName是否已经以.h结尾
    size_t nameLen = strlen(headerName);
    int hasExtension = (nameLen >= 2 && strcmp(headerName + nameLen - 2, ".h") == 0);
    
    // 统一使用正斜杠拼接路径
    if (hasExtension)
    {
        snprintf(headerPath, sizeof(headerPath), "%s/%s", normalizedDir, headerName);
    }
    else
    {
        snprintf(headerPath, sizeof(headerPath), "%s/%s.h", normalizedDir, headerName);
    }

    FILE *h = fopen(headerPath, "w");
    if (!h)
    {
        fprintf(stderr, "Error creating header file '%s': %s\n", headerPath, strerror(errno));
        return;
    }

    // 清理头文件名用于宏定义
    char cleanName[256];
    strncpy(cleanName, headerName, sizeof(cleanName) - 1);
    cleanName[sizeof(cleanName) - 1] = '\0';
    for (char *p = cleanName; *p; p++)
    {
        if (*p == '.')
            *p = '_';
    }
    
    // 转换为大写
    to_uppercase(cleanName);

    fprintf(h, "// Auto-generated combined header from %d object files\n", fileCount);
    fprintf(h, "#ifndef _INCLUDE_%s_H_\n", cleanName);
    fprintf(h, "#define _INCLUDE_%s_H_\n\n", cleanName);

    // 收集所有符号
    int totalSymbols = 0;
    for (int f = 0; f < fileCount; f++)
    {
        totalSymbols += files[f].symbolCount;
    }

    // 输出所有符号
    for (int f = 0; f < fileCount; f++)
    {
        if (files[f].symbolCount > 0)
        {
            // 规范化文件路径用于输出
            char normalizedFilePath[1024];
            normalize_path(normalizedFilePath, sizeof(normalizedFilePath), files[f].filepath);
            fprintf(h, "// From %s\n", normalizedFilePath);
            for (int i = 0; i < files[f].symbolCount; i++)
            {
                const char *name = files[f].symbols[i].name;
                if (strstr(name, "_size"))
                {
                    fprintf(h, "extern const unsigned int %s;\n", name);
                }
                else if (strstr(name, "_start") || strstr(name, "_end"))
                {
                    fprintf(h, "extern const unsigned char %s[];\n", name);
                }
                else
                {
                    fprintf(h, "extern const unsigned char %s[];\n", name);
                }
            }
            fprintf(h, "\n");
        }
    }

    // 输出宏定义（如果有）
    int hasMacros = 0;
    for (int f = 0; f < fileCount; f++)
    {
        if (files[f].macro && files[f].macro[0])
        {
            hasMacros = 1;
            break;
        }
    }
    if (hasMacros)
    {
        fprintf(h, "// Macros for convenience\n");
        for (int f = 0; f < fileCount; f++)
        {
            if (files[f].macro && files[f].macro[0] && files[f].symbolCount > 0)
            {
                // 规范化文件路径用于输出
                char normalizedFilePath[1024];
                normalize_path(normalizedFilePath, sizeof(normalizedFilePath), files[f].filepath);
                fprintf(h, "// From %s\n", normalizedFilePath);
                for (int i = 0; i < files[f].symbolCount; i++)
                {
                    const char *name = files[f].symbols[i].name;
                    const char *suffix = strrchr(name, '_');
                    if (suffix)
                    {
                        suffix++;
                        char macroName[256];
                        snprintf(macroName, sizeof(macroName), "%s_%s", files[f].macro, suffix);
                        // 将宏名称转换为大写
                        to_uppercase(macroName);
                        fprintf(h, "#define %s %s\n", macroName, name);
                    }
                }
            }
        }
    }

    fprintf(h, "\n#endif // _INCLUDE_%s_H_\n", cleanName);
    fclose(h);
    printf("Generated combined header: %s\n", headerPath);
}

static char *basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        slash = strrchr(path, '\\');
    const char *start = slash ? slash + 1 : path;
    char *base = my_strdup(start);
    // 移除扩展名 .o
    char *dot = strrchr(base, '.');
    if (dot && (strcmp(dot, ".o") == 0 || strcmp(dot, ".obj") == 0))
    {
        *dot = '\0';
    }
    return base;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s -d <output_dir> [-n <header_name>] <file1.o> [macro1] <file2.o> [macro2] ...\n", argv[0]);
        fprintf(stderr, "If -n is specified, all symbols are combined into one header file.\n");
        fprintf(stderr, "Otherwise, each .o file gets its own header.\n");
        return 1;
    }

    const char *outDir = NULL;
    const char *outName = NULL;
    int i = 1;
    while (i < argc)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing argument for -d\n");
                return 1;
            }
            outDir = argv[i + 1];
            i += 2;
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing argument for -n\n");
                return 1;
            }
            outName = argv[i + 1];
            i += 2;
        }
        else
        {
            break;
        }
    }

    if (!outDir)
    {
        fprintf(stderr, "Output directory not specified (use -d)\n");
        return 1;
    }

    // 创建输出目录
    if (mkdir(outDir, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Failed to create directory '%s': %s\n", outDir, strerror(errno));
        return 1;
    }

    // 解析文件列表
    int fileCount = 0;
    ObjectFile *files = malloc((argc - i) * sizeof(ObjectFile)); // 最多这么多
    if (!files)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    while (i < argc)
    {
        const char *filepath = argv[i];
        i++;
        const char *macro = "";
        if (i < argc && argv[i][0] != '-')
        {
            macro = argv[i];
            i++;
        }

        files[fileCount].filepath = my_strdup(filepath);
        files[fileCount].macro = my_strdup(macro);
        files[fileCount].symbols = NULL;
        files[fileCount].symbolCount = 0;

        if (!parse_object_file(filepath, &files[fileCount].symbols, &files[fileCount].symbolCount))
        {
            fprintf(stderr, "Failed to parse '%s', skipping\n", filepath);
            free(files[fileCount].filepath);
            free(files[fileCount].macro);
            continue;
        }

        fileCount++;
    }

    if (fileCount == 0)
    {
        fprintf(stderr, "No valid object files to process\n");
        free(files);
        return 1;
    }

    // 生成头文件
    if (outName)
    {
        // 合并模式
        generate_combined_header(outDir, outName, files, fileCount);
    }
    else
    {
        // 单独模式
        for (int f = 0; f < fileCount; f++)
        {
            char *base = basename(files[f].filepath);
            generate_header(outDir, base, files[f].macro, files[f].symbols, files[f].symbolCount);
            free(base);
        }
    }

    // 清理
    for (int f = 0; f < fileCount; f++)
    {
        free(files[f].filepath);
        free(files[f].macro);
        free_symbols(files[f].symbols, files[f].symbolCount);
    }
    free(files);

    return 0;
}
