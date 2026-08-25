/*
 * convert-png-to-bas-bytes.cpp
 * ----------------------------
 * Reads an image (PNG, JPEG, BMP, TIFF, PPM, ...) and emits an Epson HX-20
 * BASIC program that plots black pixels. Pixel data are packed (8 pixels per
 * byte), so the DATA section is ~8x smaller and the draw loop traverses bits
 * instead of pixels.
 *
 * If the image has multiple channels (RGB, RGBA) it is automatically converted
 * to grayscale via luminance weighting before processing.
 *
 * Dependencies:
 *   CImg (header-only): https://cimg.eu
 *   Optional: libpng, libjpeg, libtiff — link whichever formats you need.
 *   Without them CImg falls back to built-in BMP/PPM support.
 *
 * Compile example:
 *   g++ -O2 -o convert-png-to-bas-bytes convert-png-to-bas-bytes.cpp \
 *       -lpng -ljpeg -lz
 *   (On Linux you may also need -lX11 -pthread, or define cimg_display=0)
 *
 * Usage:
 *   ./convert-png-to-bas-bytes [options] input.<ext> > output.bas
 *
 * Options:
 *   --width  <int>   Expected image width  (default: 120)
 *   --height <int>   Expected image height (default: 32)
 *   --thresh <int>   Black threshold 0-255, pixels < thresh are black
 *                    (default: 127)
 *   --help           Show this message
 */

#define cimg_display 0      // No GUI needed
#include "CImg.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace cimg_library;

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------
struct Config {
    int         width  = 120;
    int         height = 32;
    int         thresh = 127;   // pixels strictly less than this => black
    bool        resize = false; // force-resize input to width x height
    std::string input;
};

static void print_usage(const char* prog)
{
    std::cerr
        << "Usage: " << prog << " [options] input.<ext>\n"
        << "\n"
        << "Options:\n"
        << "  --width  <int>   Expected image width  (default: 120)\n"
        << "  --height <int>   Expected image height (default: 32)\n"
        << "  --thresh <int>   Black threshold 0-255 (default: 127)\n"
        << "  --resize         Resize input to width x height (ignoring aspect ratio)\n"
        << "  --help           Show this message\n";
}

static Config parse_args(int argc, char* argv[])
{
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if ((a == "--width" || a == "--height" || a == "--thresh") && i + 1 < argc) {
            int val = std::atoi(argv[++i]);
            if      (a == "--width")  cfg.width  = val;
            else if (a == "--height") cfg.height = val;
            else                      cfg.thresh = val;
        } else if (a == "--resize") {
            cfg.resize = true;
        } else if (a.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << a << "\n";
            print_usage(argv[0]);
            std::exit(1);
        } else {
            if (!cfg.input.empty()) {
                std::cerr << "Unexpected argument: " << a << "\n";
                std::exit(1);
            }
            cfg.input = a;
        }
    }
    if (cfg.input.empty()) {
        print_usage(argv[0]);
        std::exit(1);
    }
    if (cfg.thresh < 0 || cfg.thresh > 255) {
        std::cerr << "--thresh must be in range 0-255.\n";
        std::exit(1);
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// Image helpers
// ---------------------------------------------------------------------------

// Convert any CImg<unsigned char> to single-channel grayscale.
// Supports L (1ch), LA (2ch), RGB (3ch), RGBA (4ch).
// Uses standard luminance weights for RGB: 0.2126 R + 0.7152 G + 0.0722 B.
static CImg<unsigned char> to_grayscale(const CImg<unsigned char>& src)
{
    const int ch = src.spectrum();
    if (ch == 1)
        return src;  // already grayscale

    CImg<unsigned char> gray(src.width(), src.height(), 1, 1);
    cimg_forXY(src, x, y) {
        double r, g, b;
        if (ch >= 3) {
            r = src(x, y, 0, 0);
            g = src(x, y, 0, 1);
            b = src(x, y, 0, 2);
        } else {
            // 2-channel: treat channel 0 as gray, channel 1 as alpha (ignored)
            r = g = b = src(x, y, 0, 0);
        }
        gray(x, y, 0, 0) = static_cast<unsigned char>(
            0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
    }
    return gray;
}

// Pack one row of cfg.width pixels into cfg.width/8 bytes.
// MSB = leftmost pixel, 1 = black.
static std::vector<int> pack_row(const CImg<unsigned char>& img,
                                 int y, const Config& cfg)
{
    std::vector<int> row;
    row.reserve(cfg.width / 8);
    for (int xb = 0; xb < cfg.width; xb += 8) {
        int b = 0;
        for (int bit = 0; bit < 8; ++bit) {
            int x = xb + bit;
            if (img(x, y, 0, 0) < cfg.thresh)
                b |= (1 << (7 - bit));  // MSB first
        }
        row.push_back(b);
    }
    return row;
}

// ---------------------------------------------------------------------------
// BASIC emitter
// ---------------------------------------------------------------------------
static void emit_basic(const CImg<unsigned char>& img, const Config& cfg)
{
    const int bytes_per_row = cfg.width / 8;
    const int last_byte_idx = bytes_per_row - 1;
    const int last_row_idx  = cfg.height - 1;

    int ln = 10;
    auto line = [&](const std::string& s) {
        std::cout << ln << " " << s << "\r\n";
        ln += 10;
    };

    auto str = [](int v) { return std::to_string(v); };

    line("REM " + str(cfg.width) + "x" + str(cfg.height) + " BITMAP DRAW (byte-packed)");
    line("CLS");
    line("DIM P(7)");
    line("P(0)=1: P(1)=2: P(2)=4: P(3)=8");
    line("P(4)=16: P(5)=32: P(6)=64: P(7)=128");
    line("DIM B(" + str(last_byte_idx) + ")");
    line("FOR Y=0 TO " + str(last_row_idx));
    line("FOR I=0 TO " + str(last_byte_idx) + ": READ B(I): NEXT");
    line("X=0");
    line("FOR I=0 TO " + str(last_byte_idx));
    line("V=B(I)");
    line("FOR K=7 TO 0 STEP -1");
    line("IF V>=P(K) THEN PSET(X,Y): V=V-P(K)");
    line("X=X+1");
    line("NEXT K");
    line("NEXT I");
    line("NEXT Y");
    line("END");

    // DATA section: bytes_per_row bytes per row
    for (int y = 0; y < cfg.height; ++y) {
        std::vector<int> row = pack_row(img, y, cfg);
        std::ostringstream oss;
        oss << "DATA ";
        for (int i = 0; i < static_cast<int>(row.size()); ++i) {
            if (i) oss << ",";
            oss << row[i];
        }
        std::cout << ln << " " << oss.str() << "\r\n";
        ln += 10;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    Config cfg = parse_args(argc, argv);

    // Load image (CImg detects format from file extension)
    CImg<unsigned char> img;
    try {
        img.load(cfg.input.c_str());
    } catch (CImgException& e) {
        std::cerr << "Error loading '" << cfg.input << "': " << e.what() << "\n";
        return 1;
    }

    // Resize or validate dimensions
    if (img.width() != cfg.width || img.height() != cfg.height) {
        if (cfg.resize) {
            std::cerr << "Note: resizing " << img.width() << "x" << img.height()
                      << " -> " << cfg.width << "x" << cfg.height << ".\n";
            // interpolation 3 = bicubic, -100 = keep all channels
            img.resize(cfg.width, cfg.height, -100, -100, 3);
        } else {
            std::cerr << "Image must be exactly " << cfg.width << "x" << cfg.height
                      << " (got " << img.width() << "x" << img.height() << ").\n"
                      << "Use --resize to resize automatically.\n";
            return 1;
        }
    }

    // Width must be a multiple of 8 (packing requirement)
    if (cfg.width % 8 != 0) {
        std::cerr << "Width must be a multiple of 8 (got " << cfg.width << ").\n";
        return 1;
    }

    // Auto-convert to grayscale if needed
    const int ch = img.spectrum();
    if (ch != 1) {
        std::cerr << "Note: " << ch << "-channel image detected, "
                  << "converting to grayscale via luminance weighting.\n";
        img = to_grayscale(img);
    }

    emit_basic(img, cfg);
    return 0;
}
