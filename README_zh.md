# chconv - 字符编码转换工具

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

chconv 是一个命令行工具，用于自动检测文件的字符编码并将其转换为指定的目标编码（默认为 UTF-8）。

## 功能特性

- 自动检测文件编码格式（支持多种编码）
- 支持单个文件或整个目录的批量转换
- 可递归处理子目录
- 支持按文件后缀名过滤
- 支持多种目标编码格式（通过 libiconv）
- 提供试运行模式，预览将要执行的操作
- 跨平台支持（Windows/Linux/macOS）

## 编译要求

- C++17 或更高版本
- 依赖库：
  - [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/) - 用于字符编码检测
  - [libiconv](https://www.gnu.org/software/libiconv/) - 用于字符编码转换

## 使用方法

### 基本语法

```bash
chconv [选项] -i <输入文件/目录> -o <输出文件/目录>
```

### 常用选项

| 选项 | 简写 | 描述 |
|------|------|------|
| --input | -i | 输入文件或目录（必需） |
| --output | -o | 输出文件或目录（必需） |
| --to | -t | 目标编码格式（默认：UTF-8） |
| --verbose | -v | 显示详细输出 |
| --recursive | -r | 递归处理目录 |
| --dry-run | -d | 仅显示将要执行的操作，不实际转换 |
| --suffix | -s | 指定要处理的文件后缀 |

### 示例

1. 转换单个文件到 UTF-8：
   ```bash
   chconv -i input.txt -o output.txt
   ```

2. 将整个目录中的文件转换为 GBK 编码：
   ```bash
   chconv -r -t GBK -i ./source_dir -o ./target_dir
   ```

3. 仅转换 .log 文件：
   ```bash
   chconv -r -s .log -i ./logs -o ./converted_logs
   ```

4. 试运行模式（预览操作但不实际转换）：
   ```bash
   chconv -r -d -i ./source -o ./target
   ```

## 支持的编码格式

chconv 支持所有 libiconv 支持的编码格式，包括但不限于：
- UTF-8, UTF-16, UTF-32
- ASCII
- ISO-8859 系列
- Windows 系列 (CP1252, CP936, 等)
- 中文编码 (GB2312, GBK, GB18030, Big5)
- 日文编码 (Shift_JIS, EUC-JP)
- 韩文编码 (EUC-KR)

完整列表请参考 [libiconv 文档](https://www.gnu.org/savannah-checkouts/gnu/libiconv/)
