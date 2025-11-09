# HX‑20 Tools

Two small command‑line utilities for transfering BASIC programs to Epson HX‑20:

- **hx20tape** — encode an ASCII or tokenized BASIC source file as an HX‑20 WAV “tape”.  
- **hx20tokenizer** — tokenize ASCII BASIC to HX‑20 binary format, or detokenize a tokenized HX‑20 BASIC file back to ASCII.

## Build

You need a C++17 compiler (GCC, Clang, or MSVC). On Unix-like systems:

```bash
make
```

This produces two binaries in the current directory: `hx20tape` and `hx20tokenizer`.

### Filesystem link note

If you see unresolved `std::filesystem` symbols on older GCC (<= 8), link with `-lstdc++fs`:

```bash
make FS_LIB=-lstdc++fs
```

### Install

```bash
sudo make install            # installs to /usr/local/bin
# or specify a different prefix:
sudo make install PREFIX=/opt
```

## Usage

### hx20tape — encode BASIC to WAV

Encodes an ASCII (or tokenized) BASIC program to an HX‑20 cassette WAV (11025 Hz, 8‑bit mono).

```
hx20tape -i <input.bas> -o <output.wav> [-n <name>] [-t <type>] [-a <level>] [-d] [-h]
```

**Options**

- `-i <file>`  ASCII BASIC source file (required)  
- `-o <file>`  Output WAV file (default: `<input>.wav`)  
- `-n <name>`  Program name (max 8 chars, default: `PROGRAM`)    
- `-a <level>` Output normalization amplitude (default: `95`)  
- `-d`         Dump encoded payload for debugging  
- `-h`         Show help

**Example**

```bash
./hx20tape -i hello.txt -o hello.wav -n HELLO
```

**Notes**

- The tool ensures CRLF line endings in the encoded content.
- Blocks are written with synchronization, preamble/postamble, CRC (CRC‑Kermit), and short inter‑block gaps.
- The program name is padded/truncated to 8 chars.

### hx20tokenizer — (de)tokenize HX‑20 BASIC

This tool detects the input format automatically:

- If the input starts with byte `0xFF`, it is treated as tokenized HX‑20 BASIC and will be **detokenized** to ASCII text.
- Otherwise, the ASCII BASIC input will be **tokenized** to the HX‑20 binary format.

```
hx20tokenizer -i <input> -o <output>
```

**Examples**

```bash
# ASCII -> tokenized
./hx20tokenizer -i game.txt -o game.bas

# tokenized -> ASCII
./hx20tokenizer -i game.bas -o game.txt
```

## Kknown bugs
- Tokenized programs are recognized but often yields a "BD ERROR" in the end. Just stick to pure ASCII programs
- Loading short programs might require manual stop. Just press BREAK when the wav file is finished playing.  

## Platform notes

- **Linux/macOS**: build with `make`.  
- **Windows**: build with MSYS2/MinGW (`pacman -S mingw-w64-x86_64-gcc make`) or with Visual Studio by creating a project that uses C++17.

## License
MIT 
