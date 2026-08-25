/*
 * hx20rom - Epson HX-20 ROM Cartridge Image Creator
 *
 * Creates a binary ROM image suitable for burning to a 2764 (8 KB),
 * 27128 (16 KB) or 27256 (32 KB) EPROM for use with the Epson HX-20
 * ROM cartridge slot.
 *
 * ROM Format (Epson HX-20 Technical Manual, chapter 4.2.2):
 *
 *   The ROM begins with a directory of up to 31 file headers (32 bytes
 *   each), followed by a dummy header (first byte = 0xFF), followed by
 *   file data packed contiguously. Unused ROM bytes are 0xFF.
 *
 *   Header layout (32 bytes):
 *     [0-7]   Filename, ASCII, space-padded.
 *             0xFF = dummy header (end of directory marker)
 *             0x00 = deleted/empty file slot
 *     [8-10]  Extension, ASCII, space-padded
 *     [11]    File type:  0x00 = BASIC program
 *                         0x01 = BASIC data file
 *                         0x02 = Machine code program
 *     [12]    Format:     0x00 = Binary (tokenised BASIC or raw machine code)
 *                         0xFF = ASCII  (plain text BASIC, CR+LF lines)
 *     [13-15] Reserved (0x00)
 *     [16-19] Start address in ROM, 4 ASCII hex digits, e.g. "0180"
 *     [20-23] End address + 1, 4 ASCII hex digits
 *     [24-29] Creation date, 6 ASCII chars, MMDDYY
 *     [30-31] Spare / ROM version (0x00)
 *
 * Usage:
 *   hx20rom [options] inputfile ... output.bin
 *   hx20rom --list image.bin
 *
 * Options:
 *   -s <kb>       ROM size in KB: 8 (default), 16, 32
 *   -d <MMDDYY>   Date for all files (default: today)
 *   -t <type>     Type override for next file: basic, basicdata, machine
 *   -f <fmt>      Format override for next file: binary, ascii
 *   -n <name.ext> Name override for next file (as stored in ROM header)
 *   -v            Verbose output
 *   --list <img>  List contents of existing ROM image, then exit
 *
 * File type is auto-detected from extension unless overridden:
 *   .bas .asc .txt  -> BASIC / ASCII
 *   .hex .com       -> Machine code / Binary
 *   others          -> BASIC / Binary
 *
 * Build:
 *   c++ -std=c++17 -O2 -Wall -o hx20rom hx20rom.cpp
 *
 * Example:
 *   hx20rom loader.bas main.bin output.rom
 *   hx20rom -v -d 102484 -n MYAPP.BAS prog.bas output.rom
 *   hx20rom --list output.rom
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

static constexpr int     HEADER_SIZE  = 32;
static constexpr int     MAX_FILES    = 31;
static constexpr uint8_t ERASED       = 0xFF;  // unprogrammed EPROM byte

// The HX-20 ROM cartridge accepts 2764-pin-compatible EPROMs:
//   2764  ( 8 KB), 27128 (16 KB), 27256 (32 KB), and mask ROMs.
// The address counter hardware supports up to 32 KB.
// (HX-20 Technical Manual, section 4.2 and 4.2.3)
static constexpr int ROM_SIZE_8K  =  8 * 1024;
static constexpr int ROM_SIZE_16K = 16 * 1024;
static constexpr int ROM_SIZE_32K = 32 * 1024;

//------------------------------------------------------------------------------
// Enums
//------------------------------------------------------------------------------

enum class FileType : uint8_t {
    Basic       = 0x00,
    BasicData   = 0x01,
    MachineCode = 0x02,
};

enum class FileFormat : uint8_t {
    Binary = 0x00,
    ASCII  = 0xFF,
};

//------------------------------------------------------------------------------
// ROMFile
//------------------------------------------------------------------------------

struct ROMFile {
    std::string  name;    // up to 8 chars, already space-trimmed + upper-case
    std::string  ext;     // up to 3 chars
    FileType     type   = FileType::Basic;
    FileFormat   format = FileFormat::Binary;
    std::string  date;    // 6 chars MMDDYY

    std::vector<uint8_t> data;

    // Filled in by ROMBuilder::layout()
    uint16_t startAddr = 0;
    uint16_t endAddr   = 0;   // exclusive: startAddr + data.size()
};

//------------------------------------------------------------------------------
// Small helpers
//------------------------------------------------------------------------------

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static std::string padRight(const std::string& s, size_t w, char pad = ' ') {
    if (s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), pad);
}

static std::string hex4(uint16_t v) {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
    return ss.str();
}

static std::string todayMMDDYY() {
    time_t t = time(nullptr);
    tm* lt   = localtime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d%02d%02d",
             lt->tm_mon + 1, lt->tm_mday, lt->tm_year % 100);
    buf[6] = '\0';  // ensure exactly 6 digits
    return std::string(buf);
}

// Auto-detect file type and format from extension
static std::pair<FileType, FileFormat> detectTypeFromExt(const std::string& ext) {
    std::string e = toUpper(ext);
    if (e == "BAS" || e == "ASC" || e == "TXT")
        return { FileType::Basic, FileFormat::ASCII };
    if (e == "HEX" || e == "COM")
        return { FileType::MachineCode, FileFormat::Binary };
    return { FileType::Basic, FileFormat::Binary };
}

static std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    return { std::istreambuf_iterator<char>(f), {} };
}

//------------------------------------------------------------------------------
// ROMBuilder
//------------------------------------------------------------------------------

class ROMBuilder {
public:
    int         romSize     = ROM_SIZE_8K;
    bool        verbose     = false;
    std::string defaultDate = todayMMDDYY();

    void addFile(ROMFile f) {
        if ((int)files_.size() >= MAX_FILES)
            throw std::runtime_error("Too many files (max " +
                                     std::to_string(MAX_FILES) + ")");
        if (f.date.empty()) f.date = defaultDate;
        files_.push_back(std::move(f));
    }

    // Compute start/end addresses. Throws if ROM is too small.
    void layout() {
        // Directory: one header per file + one dummy header at end
        int cursor = ((int)files_.size() + 1) * HEADER_SIZE;

        for (auto& f : files_) {
            if (cursor > 0xFFFF)
                throw std::runtime_error("Address overflow: ROM image too large for 16-bit addressing");

            f.startAddr = static_cast<uint16_t>(cursor);
            cursor     += static_cast<int>(f.data.size());
            f.endAddr   = static_cast<uint16_t>(cursor);

            if (cursor > romSize)
                throw std::runtime_error(
                    "File \"" + f.name + "." + f.ext + "\" does not fit "
                    "(used: " + std::to_string(cursor) +
                    " / " + std::to_string(romSize) + " bytes)");
        }

        if (verbose) {
            int dirBytes  = ((int)files_.size() + 1) * HEADER_SIZE;
            int dataBytes = cursor - dirBytes;
            std::cerr << "Layout:"
                      << "  dir="  << dirBytes  << " B"
                      << "  data=" << dataBytes << " B"
                      << "  free=" << (romSize - cursor) << " B\n";
        }
    }

    // Build and return the complete ROM image (romSize bytes, 0xFF-padded)
    std::vector<uint8_t> build() {
        layout();

        std::vector<uint8_t> rom(romSize, ERASED);
        int pos = 0;

        // Write file headers
        for (const auto& f : files_) {
            writeFileHeader(rom, pos, f);
            pos += HEADER_SIZE;
        }

        // Write dummy header (signals end-of-directory)
        rom[pos] = ERASED;               // 0xFF in byte 0
        std::fill(rom.begin() + pos + 1, rom.begin() + pos + HEADER_SIZE, 0x00);
        pos += HEADER_SIZE;

        // Write file data
        for (const auto& f : files_) {
            assert(pos == f.startAddr);
            std::copy(f.data.begin(), f.data.end(), rom.begin() + pos);
            pos += static_cast<int>(f.data.size());
        }

        return rom;
    }

    const std::vector<ROMFile>& files() const { return files_; }

private:
    std::vector<ROMFile> files_;

    void writeFileHeader(std::vector<uint8_t>& rom, int pos, const ROMFile& f) {
        // [0-7]  name, space-padded to 8 bytes
        std::string n8 = padRight(f.name, 8);
        for (int i = 0; i < 8; i++) rom[pos + i] = static_cast<uint8_t>(n8[i]);

        // [8-10] extension, space-padded to 3 bytes
        std::string e3 = padRight(f.ext, 3);
        for (int i = 0; i < 3; i++) rom[pos + 8 + i] = static_cast<uint8_t>(e3[i]);

        // [11] file type
        rom[pos + 11] = static_cast<uint8_t>(f.type);

        // [12] format
        rom[pos + 12] = static_cast<uint8_t>(f.format);

        // [13-15] reserved
        rom[pos + 13] = rom[pos + 14] = rom[pos + 15] = 0x00;

        // [16-19] start address as 4 ASCII hex digits
        auto sa = hex4(f.startAddr);
        for (int i = 0; i < 4; i++) rom[pos + 16 + i] = static_cast<uint8_t>(sa[i]);

        // [20-23] end address + 1 as 4 ASCII hex digits
        auto ea = hex4(f.endAddr);
        for (int i = 0; i < 4; i++) rom[pos + 20 + i] = static_cast<uint8_t>(ea[i]);

        // [24-29] date MMDDYY, zero-padded if short
        std::string d6 = padRight(f.date, 6, '0');
        for (int i = 0; i < 6; i++) rom[pos + 24 + i] = static_cast<uint8_t>(d6[i]);

        // [30-31] spare
        rom[pos + 30] = rom[pos + 31] = 0x00;
    }
};

//------------------------------------------------------------------------------
// --list: display ROM directory
//------------------------------------------------------------------------------

static void listROM(const std::string& path) {
    auto data = readBinaryFile(path);

    std::cout << "ROM image: " << path
              << "  (" << data.size() << " bytes)\n\n";

    auto parseHex4 = [&](int pos, int off) -> uint16_t {
        char buf[5] = {};
        memcpy(buf, &data[pos + off], 4);
        return static_cast<uint16_t>(strtol(buf, nullptr, 16));
    };
    auto rtrim = [](std::string s) -> std::string {
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        return s;
    };

    std::cout << std::left
              << std::setw(14) << "Filename"
              << std::setw(12) << "Type"
              << std::setw(9)  << "Format"
              << std::setw(8)  << "Start"
              << std::setw(8)  << "End"
              << std::setw(8)  << "Bytes"
              << std::setw(8)  << "Date"
              << "\n"
              << std::string(67, '-') << "\n";

    int pos = 0, count = 0;
    while (pos + HEADER_SIZE <= (int)data.size()) {
        uint8_t b0 = data[pos];
        if (b0 == ERASED) {
            std::cout << "(end of directory)\n";
            break;
        }
        if (b0 == 0x00) {
            std::cout << "(deleted slot)\n";
            pos += HEADER_SIZE;
            continue;
        }
        if (count > MAX_FILES) {
            std::cout << "(possibly corrupt – stopping)\n";
            break;
        }

        std::string name = rtrim(std::string(reinterpret_cast<char*>(&data[pos]),     8));
        std::string ext  = rtrim(std::string(reinterpret_cast<char*>(&data[pos + 8]), 3));
        uint8_t typeB    = data[pos + 11];
        uint8_t fmtB     = data[pos + 12];
        uint16_t start   = parseHex4(pos, 16);
        uint16_t end     = parseHex4(pos, 20);
        std::string date = std::string(reinterpret_cast<char*>(&data[pos + 24]), 6);

        const char* typeStr = typeB == 0x00 ? "BASIC"      :
                              typeB == 0x01 ? "BASIC data"  :
                              typeB == 0x02 ? "Machine code": "?";
        const char* fmtStr  = fmtB == 0xFF  ? "ASCII"      :
                              fmtB == 0x00  ? "Binary"     : "?";

        std::string fullName = ext.empty() ? name : name + "." + ext;
        std::cout << std::left
                  << std::setw(14) << fullName
                  << std::setw(12) << typeStr
                  << std::setw(9)  << fmtStr
                  << std::setw(8)  << ("0x" + hex4(start))
                  << std::setw(8)  << ("0x" + hex4(end))
                  << std::setw(8)  << (end - start)
                  << std::setw(8)  << date
                  << "\n";

        pos += HEADER_SIZE;
        count++;
    }

    // Count used bytes (up to last non-0xFF byte)
    int used = 0;
    for (int i = (int)data.size() - 1; i >= 0; i--) {
        if (data[i] != ERASED) { used = i + 1; break; }
    }
    std::cout << "\n" << count << " file(s) | "
              << used << " bytes used | "
              << ((int)data.size() - used) << " bytes free\n";
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

static void usage(const char* prog) {
    std::cerr <<
R"(Usage:
  )" << prog << R"( [options] inputfile [inputfile ...] output.bin
  )" << prog << R"( --list image.bin

Options:
  -s <kb>       ROM size in KB: 8 (default), 16, 32
                Corresponds to EPROM types 2764, 27128, 27256
  -d <MMDDYY>   Date for all files, e.g. 102484  (default: today)
  -t <type>     File type for next file: basic | basicdata | machine
  -f <fmt>      Format for next file:   binary | ascii
  -n <name.ext> ROM header name for next file (default: from filename)
  -v            Verbose output

Auto-detected types from extension (overridable with -t and -f):
  .bas .asc .txt  -> BASIC / ASCII
  .hex .com       -> Machine code / Binary
  others          -> BASIC / Binary

Examples:
  )" << prog << R"( loader.bas main.bin output.rom
  )" << prog << R"( -v -s 16 -d 102484 -n MYAPP.BAS prog.bas output.rom
  )" << prog << R"( --list output.rom
)";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    // Handle --list early
    if (std::string(argv[1]) == "--list") {
        if (argc < 3) { std::cerr << "Error: --list requires a filename\n"; return 1; }
        try { listROM(argv[2]); }
        catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; return 1; }
        return 0;
    }

    ROMBuilder builder;

    // Per-file option state
    struct FileOpts {
        std::string overrideName;
        FileType    type   = FileType::Basic;
        FileFormat  format = FileFormat::Binary;
        bool        typeSet   = false;
        bool        formatSet = false;
        void reset() { overrideName.clear(); typeSet = false; formatSet = false;
                       type = FileType::Basic; format = FileFormat::Binary; }
    } opts;

    std::vector<std::string> positional;

    // First pass: collect global options and positional arguments
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v") { builder.verbose = true; continue; }
        if (a == "-s") {
            int kb = std::stoi(argv[++i]);
            if      (kb == 8)  builder.romSize = ROM_SIZE_8K;
            else if (kb == 16) builder.romSize = ROM_SIZE_16K;
            else if (kb == 32) builder.romSize = ROM_SIZE_32K;
            else { std::cerr << "Error: -s must be 8, 16 or 32\n"; return 1; }
            continue;
        }
        if (a == "-d") { builder.defaultDate = argv[++i]; continue; }
        // Per-file and positional handled in second pass
    }

    // Second pass: build file list, honouring per-file -t/-f/-n
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v" || a == "-s" || a == "-d") { if (a != "-v") i++; continue; }

        if (a == "-t") {
            std::string t = toUpper(argv[++i]);
            if      (t == "BASIC")     opts.type = FileType::Basic;
            else if (t == "BASICDATA") opts.type = FileType::BasicData;
            else if (t == "MACHINE")   opts.type = FileType::MachineCode;
            else { std::cerr << "Error: unknown type \"" << argv[i] << "\"\n"; return 1; }
            opts.typeSet = true;
            continue;
        }
        if (a == "-f") {
            std::string f = toUpper(argv[++i]);
            if      (f == "ASCII")  opts.format = FileFormat::ASCII;
            else if (f == "BINARY") opts.format = FileFormat::Binary;
            else { std::cerr << "Error: unknown format \"" << argv[i] << "\"\n"; return 1; }
            opts.formatSet = true;
            continue;
        }
        if (a == "-n") { opts.overrideName = toUpper(argv[++i]); continue; }
        if (a[0] == '-') { std::cerr << "Error: unknown option: " << a << "\n"; return 1; }

        positional.push_back(a);
    }

    if (positional.size() < 2) {
        std::cerr << "Error: need at least one input file and one output file\n";
        usage(argv[0]);
        return 1;
    }

    // Last positional is output file
    std::string outputFile = positional.back();
    positional.pop_back();

    // Now do a clean third pass just for file arguments (build ROM members)
    opts.reset();
    try {
        int fileIdx = 0;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "-v") continue;
            if (a == "-s" || a == "-d") { i++; continue; }
            if (a == "-t") {
                std::string t = toUpper(argv[++i]);
                if      (t == "BASIC")     opts.type = FileType::Basic;
                else if (t == "BASICDATA") opts.type = FileType::BasicData;
                else if (t == "MACHINE")   opts.type = FileType::MachineCode;
                opts.typeSet = true;
                continue;
            }
            if (a == "-f") {
                std::string f = toUpper(argv[++i]);
                opts.format    = (f == "ASCII") ? FileFormat::ASCII : FileFormat::Binary;
                opts.formatSet = true;
                continue;
            }
            if (a == "-n") { opts.overrideName = toUpper(argv[++i]); continue; }
            if (a[0] == '-') { i++; continue; } // unknown, skip

            // Positional: could be input or output
            if (fileIdx >= (int)positional.size()) { fileIdx++; continue; } // output file
            fileIdx++;

            const std::string& diskPath = a;
            fs::path p(diskPath);
            std::string ext = toUpper(p.extension().string());
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

            ROMFile f;
            f.date = builder.defaultDate;

            // Name in ROM header
            if (!opts.overrideName.empty()) {
                auto dot = opts.overrideName.find('.');
                if (dot != std::string::npos) {
                    f.name = opts.overrideName.substr(0, dot);
                    f.ext  = opts.overrideName.substr(dot + 1);
                } else {
                    f.name = opts.overrideName;
                    f.ext  = "";
                }
            } else {
                f.name = toUpper(p.stem().string());
                f.ext  = ext;
            }
            if (f.name.size() > 8) f.name = f.name.substr(0, 8);
            if (f.ext.size()  > 3) f.ext  = f.ext.substr(0, 3);

            // Type/format
            auto [dt, df] = detectTypeFromExt(ext);
            f.type   = opts.typeSet   ? opts.type   : dt;
            f.format = opts.formatSet ? opts.format : df;

            f.data = readBinaryFile(diskPath);

            if (builder.verbose) {
                const char* ts = f.type == FileType::Basic       ? "BASIC" :
                                 f.type == FileType::BasicData   ? "BASIC data" :
                                                                   "Machine code";
                const char* fs = f.format == FileFormat::ASCII   ? "ASCII" : "Binary";
                std::cerr << "  Adding " << std::setw(12) << (f.name + "." + f.ext)
                          << "  " << std::setw(12) << ts
                          << "  " << std::setw(7)  << fs
                          << "  " << f.data.size() << " bytes  (from " << diskPath << ")\n";
            }

            builder.addFile(std::move(f));
            opts.reset();
        }

        if (builder.files().empty()) {
            std::cerr << "Error: no input files specified\n";
            return 1;
        }

        // Build ROM image
        auto rom = builder.build();

        // Write output
        std::ofstream out(outputFile, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot write: " + outputFile);
        out.write(reinterpret_cast<const char*>(rom.data()), rom.size());

        // Summary
        int dirBytes  = ((int)builder.files().size() + 1) * HEADER_SIZE;
        int dataBytes = 0;
        for (const auto& f : builder.files()) dataBytes += (int)f.data.size();
        int used = dirBytes + dataBytes;
        int free = builder.romSize - used;

        std::cout << "Written: " << outputFile
                  << "  [" << builder.romSize / 1024 << " KB"
                  << "  used=" << used << " B"
                  << "  free=" << free << " B"
                  << "  files=" << builder.files().size()
                  << "]\n";

        if (builder.verbose) {
            std::cout << "\nDirectory layout:\n"
                      << std::left
                      << std::setw(14) << "File"
                      << std::setw(8)  << "Start"
                      << std::setw(8)  << "End"
                      << "Bytes\n"
                      << std::string(38, '-') << "\n";
            for (const auto& f : builder.files()) {
                std::string fn = f.name + (f.ext.empty() ? "" : "." + f.ext);
                std::cout << std::left
                          << std::setw(14) << fn
                          << std::setw(8)  << ("0x" + hex4(f.startAddr))
                          << std::setw(8)  << ("0x" + hex4(f.endAddr))
                          << f.data.size() << "\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
