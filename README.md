# Flippy

A simple, single-file command-line tool for managing FAT12 floppy disk images and ISO 9660 CD/DVD images. It does not try to be an operating system. It just moves bits around so you do not have to.

Written in mostly C89 compatible C, with `stdint.h` as its only significant concession to modernity. It runs on Windows and Unix-like systems.

## What it does

Flippy allows you to create, inspect, modify, and extract files from two very old, very specific filesystem formats:

1.  **FAT12 Floppy Images**: Supports standard sizes from 160KB up to 2.88MB.
2.  **ISO 9660 Images**: Creates Level 1 compliant ISOs from directories and allows basic manipulation of existing ISOs.

It is a simple floppy thingy. It is also a simple CD thingy.

## Building

You need a C compiler. That is it.

**On Linux/macOS:**
```bash
gcc -o flippy main.c
```

**On Windows (MSVC):**
```cmd
cl main.c
```

**On Windows (MinGW/GCC):**
```cmd
gcc -o flippy.exe main.c
```

There are no external dependencies. No makefiles. No cmake. Just one file. If you can compile C, you can use Flippy.

## Usage

```text
Usage: flippy <command> [options]
```

### FAT12 Floppy Commands

*   `create-fd <image> [size]`
    Create a new FAT12 floppy image. Default size is 1440KB.
    Supported sizes: 160, 180, 320, 360, 720, 1440, 2880.

*   `list-fd <image> [-r]`
    List files in the image. Use `-r` for recursive listing.

*   `add-fd <image> <src_file> [dest_name]`
    Add a single file to the root directory of the image.

*   `add-dir-fd <image> <local_dir> [dest_path]`
    Add a local directory recursively into the image.

*   `extract-fd <image> <src_path> <dest_file>`
    Extract a single file from the image to your local filesystem.

*   `extract-dir-fd <image> <src_path> <dest_dir>`
    Extract a directory from the image to your local filesystem.

*   `delete-fd <image> <path>`
    Delete a file from the image.

*   `delete-dir-fd <image> <path>`
    Delete a directory recursively from the image.

### ISO 9660 Commands

*   `create-iso <source_dir> <output.iso> [label]`
    Create an ISO 9660 image from a local directory.

*   `list-iso <image.iso>`
    List files in the ISO image.

*   `extract-iso <image.iso> <src_path> <dest_file>`
    Extract a single file from the ISO.

*   `extract-dir-iso <image.iso> <src_path> <dest_dir>`
    Extract a directory from the ISO.

*   `delete-iso <image.iso> <path>`
    Delete a file from the ISO.

*   `delete-dir-iso <image.iso> <path>`
    Delete a directory recursively from the ISO.

## Examples

**Create a 1.44MB floppy image:**
```bash
./flippy create-fd my_disk.img 1440
```

**Add a file to the floppy:**
```bash
./flippy add-fd my_disk.img hello.txt
```

**List contents recursively:**
```bash
./flippy list-fd my_disk.img -r
```

**Create an ISO from a folder:**
```bash
./flippy create-iso ./my_files output.iso "MY_LABEL"
```

## Testing

A comprehensive test suite is included in `run.bat`. It is written for Windows Command Prompt. It creates temporary files, tests every floppy size, tests ISO creation, extraction, and deletion, and then cleans up after itself.

To run it on Windows:
```cmd
run.bat
```

If you are on Linux or macOS, you will need to translate the batch script to bash yourself. Or just trust that it works. It is pretty good code.

## Limitations

*   **FAT12 Only**: Do not ask for FAT32. Do not ask for NTFS. This is for floppies. Small, slow, reliable floppies.
*   **ISO 9660 Level 1**: Filenames are restricted to uppercase alphanumeric characters, underscores, and dots. Long filenames are not supported because this is 1985.
*   **No Sparse Files**: The tool allocates memory for entire images. A 2.88MB image takes 2.88MB of RAM. Please do not run this on a computer with less than 4GB of RAM, just to be safe. Actually, it will probably work fine on 64KB if you remove the printf statements.
*   **Deletion in ISO**: Deleting files in an ISO zeros out the data blocks but does not rebuild the image to save space. The ISO file size remains the same. This is a feature, not a bug. It is simpler this way.

## License

MIT License

Copyright (c) 2026 Flippy Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See the `LICENSE` file in the root directory for details.
