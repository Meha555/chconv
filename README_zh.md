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

- C++20
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
| --suffix | -s | 指定要处理的文件后缀（支持正则表达式，多个模式用';'分隔） |
| --exclude | -e | 使用正则表达式排除要处理的文件、后缀或目录（用';'分隔） |

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

5. 使用正则表达式转换文件时排除特定文件、后缀或目录：
   ```bash
   chconv -r --exclude ".*\.log$|.*\.tmp$|node_modules|\.git" -i ./source_dir -o ./target_dir
   ```

### 正则表达式使用说明

`--exclude` 和 `--suffix` 选项都支持正则表达式，以实现更灵活的文件和目录匹配：

对于 `--exclude` 选项：

- `.*\.log$` - 匹配所有 .log 扩展名的文件
- `node_modules` - 匹配路径中包含 "node_modules" 的任何项
- `\.git` - 匹配路径中包含 ".git" 的任何项
- `.*\.(tmp|temp)$` - 匹配 .tmp 或 .temp 扩展名的文件

对于 `--suffix` 选项：

- `\.log$` - 匹配所有 .log 扩展名的文件
- `\.log$|\.txt$` - 匹配 .log 或 .txt 扩展名的文件
- `log$` - 匹配所有名称以 "log" 结尾的文件（包括 .log 文件）
- `\.log$;\.tmp$` - 匹配 .log 或 .tmp 扩展名的文件（使用';'分隔符）

当使用正则表达式与 `--suffix` 选项时，模式会同时与完整扩展名（包括点号）和不带点号的扩展名进行匹配。

多个模式可以使用 `|`（或）操作符在单个表达式中组合，或者用 `;` 分隔作为独立的表达式。

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
