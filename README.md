# chconv - Character Encoding Conversion Tool

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

[中文](README_zh.md)

chconv is a command-line tool for automatically detecting file character encodings and converting them to a specified target encoding (default is UTF-8).

## Features

- Automatic detection of file encoding formats (supports multiple encodings)
- Batch conversion of single files or entire directories
- Recursive processing of subdirectories
- Filtering by file extension
- Support for multiple target encoding formats (via libiconv)
- Dry-run mode to preview operations before execution
- Cross-platform support (Windows/Linux/macOS)

## Build Requirements

- C++17 or higher
- Dependencies:
  - [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/) - for character encoding detection
  - [libiconv](https://www.gnu.org/software/libiconv/) - for character encoding conversion

## Usage

### Basic Syntax

```bash
chconv [options] -i <input file/directory> -o <output file/directory>
```

### Common Options

| Option | Short | Description |
|--------|-------|-------------|
| --input | -i | Input file or directory (required) |
| --output | -o | Output file or directory (required) |
| --to | -t | Target encoding format (default: UTF-8) |
| --verbose | -v | Show detailed output |
| --recursive | -r | Recursively process directories |
| --dry-run | -d | Show operations to be performed without actually converting |
| --suffix | -s | Specify file suffix to process |

### Examples

1. Convert a single file to UTF-8:
   ```bash
   chconv -i input.txt -o output.txt
   ```

2. Convert files in an entire directory to GBK encoding:
   ```bash
   chconv -r -t GBK -i ./source_dir -o ./target_dir
   ```

3. Convert only .log files:
   ```bash
   chconv -r -s .log -i ./logs -o ./converted_logs
   ```

4. Dry-run mode (preview operations without actual conversion):
   ```bash
   chconv -r -d -i ./source -o ./target
   ```

## Supported Encoding Formats

chconv supports all encoding formats supported by libiconv, including but not limited to:
- UTF-8, UTF-16, UTF-32
- ASCII
- ISO-8859 series
- Windows series (CP1252, CP936, etc.)
- Chinese encodings (GB2312, GBK, GB18030, Big5)
- Japanese encodings (Shift_JIS, EUC-JP)
- Korean encodings (EUC-KR)

For a complete list, please refer to the [libiconv documentation](https://www.gnu.org/savannah-checkouts/gnu/libiconv/)
