# 符号生成器 (SymbolGenerator)

## 概述
符号生成器是一个用于从 COFF 格式的目标文件（.o）中提取以 `_binary_` 开头的符号并自动生成对应头文件的工具。该工具专为处理着色器编译后的二进制嵌入符号设计，可在 Makefile 中集成使用。

## 用法
指令语法如下：
~~~shell
SymbolGenerator -d <输出目录> [-n <合并头文件名>] <文件1.o> [宏1] <文件2.o> [宏2] ...
~~~
实际使用示例为：
~~~shell
# 为每个文件生成单独头文件
# Windows
.\SymbolGenerator.exe -d ./generated ./shaders/default.frag.o MY_SHADER ./shaders/default.vert.o

# Linux
./SymbolGenerator.run -d ./generated ./shaders/default.frag.o MY_SHADER ./shaders/default.vert.o

# 将所有符号合并到一个头文件中
# Windows
.\SymbolGenerator.exe -d ./generated -n shader_symbols ./shaders/default.frag.o MY_SHADER ./shaders/default.vert.o

# Linux
./SymbolGenerator.run -d ./generated -n shader_symbols ./shaders/default.frag.o MY_SHADER ./shaders/default.vert.o
~~~
每个 `.o` 文件可以可选地跟随一个宏名称。如果提供了宏名称，生成的头文件中将包含对应的宏定义，方便在代码中使用简化的名称引用符号。

使用 `-n` 参数时，所有文件的符号将被合并到一个头文件中，文件名由 `-n` 参数指定。

## 生成的头文件示例

### 单独头文件模式
对于 `default.frag.o` 文件并指定宏 `MY_SHADER`，将生成 `default.frag.h` 文件，内容如下：
~~~c
// Auto-generated header from default.frag.o
#ifndef default_frag_SYMBOLS_H
#define default_frag_SYMBOLS_H

extern const unsigned int _binary_out_objs_shaders_default_frag_spv_size;
extern const unsigned char _binary_out_objs_shaders_default_frag_spv_end[];
extern const unsigned char _binary_out_objs_shaders_default_frag_spv_start[];

// Macros for convenience
#define MY_SHADER_size _binary_out_objs_shaders_default_frag_spv_size
#define MY_SHADER_end _binary_out_objs_shaders_default_frag_spv_end
#define MY_SHADER_start _binary_out_objs_shaders_default_frag_spv_start

#endif // default_frag_SYMBOLS_H
~~~

### 合并头文件模式
使用 `-n shader_symbols` 参数时，将生成 `shader_symbols.h` 文件，内容如下：
~~~c
// Auto-generated combined header from 2 object files
#ifndef shader_symbols_SYMBOLS_H
#define shader_symbols_SYMBOLS_H

// From test/shaders/default.frag.o
extern const unsigned int _binary_out_objs_shaders_default_frag_spv_size;
extern const unsigned char _binary_out_objs_shaders_default_frag_spv_end[];
extern const unsigned char _binary_out_objs_shaders_default_frag_spv_start[];

// From test/shaders/default.vert.o
extern const unsigned char _binary_out_objs_shaders_default_vert_spv_start[];
extern const unsigned int _binary_out_objs_shaders_default_vert_spv_size;
extern const unsigned char _binary_out_objs_shaders_default_vert_spv_end[];

// Macros for convenience
// From test/shaders/default.frag.o
#define MY_SHADER_size _binary_out_objs_shaders_default_frag_spv_size
#define MY_SHADER_end _binary_out_objs_shaders_default_frag_spv_end
#define MY_SHADER_start _binary_out_objs_shaders_default_frag_spv_start

#endif // shader_symbols_SYMBOLS_H
~~~

## 在 Makefile 中使用
将此辅助程序置于项目的 `bin` 文件夹下，并在 Makefile 中使用以下命令来生成头文件：

### 单独头文件模式
~~~makefile
# 示例：为所有着色器文件生成单独头文件
SHADER_OBJS = $(wildcard ./shaders/*.o)
HEADER_DIR = ./generated

generate-headers: $(SHADER_OBJS)
	./bin/SymbolGenerator.run -d $(HEADER_DIR) $^
~~~

### 合并头文件模式
~~~makefile
# 示例：将所有着色器符号合并到一个头文件中
SHADER_OBJS = $(wildcard ./shaders/*.o)
HEADER_DIR = ./generated
COMBINED_HEADER = shader_symbols

generate-combined-header: $(SHADER_OBJS)
	./bin/SymbolGenerator.run -d $(HEADER_DIR) -n $(COMBINED_HEADER) $^
~~~

## 编译
确保您具备 gcc 编译环境，然后运行对应的构建脚本即可。当前支持 Windows 和 Linux 平台：
- Windows: 运行 `build.bat`
- Linux: 运行 `build.sh`

编译后的结果为可执行二进制文件，可以直接将其复制并部署到你希望的位置。

## 技术细节
- 直接解析 COFF 文件格式，不依赖外部工具（如 `objdump`）
- 跨平台，仅使用标准 C 库
- 自动处理符号名称中的路径转换
- 头文件保护宏会自动将文件名中的点号替换为下划线，确保有效的 C 标识符

## 注意事项
- 仅处理以 `_binary_` 开头的符号，这是着色器二进制嵌入的典型命名约定
- 如果 `.o` 文件中没有符合条件的符号，仍会生成空头文件（仅包含头文件保护宏）
- 输出目录如果不存在会自动创建
