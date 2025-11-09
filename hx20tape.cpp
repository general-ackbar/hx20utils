#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <unistd.h>
namespace fs = std::filesystem;

#define KERMIT true
//#define DEBUG true

// HX-20 Cassette encoding parameters (from official documentation)
const int SAMPLE_RATE = 11025;
const uint8_t DC_OFFSET = 129;
const double AMPLITUDE = 1.0;

// Pulse timing (microseconds) - from documentation
const int PULSE_SHORT = 545;   // ~400μs for bit '0' (< 750μs threshold)
const int PULSE_LONG = 1080;   // ~1000μs for bit '1' (> 750μs threshold)


// Block structure
const int SYNC_BITS = 80;      // 80 bits of '0' for synchronization
//const int HEADER_DATA_SIZE = 80;
const int DATA_BLOCK_SIZE = 256;

enum class BasicType { ASCII, TOKEN, SEQUENTIAL, BINARY };
bool DEBUG = false;

struct WAVHeader {
    char riff[4] = {'R','I','F','F'};
    uint32_t fileSize;
    char wave[4] = {'W','A','V','E'};
    char fmt[4] = {'f','m','t',' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint32_t sampleRate = SAMPLE_RATE;
    uint32_t byteRate = SAMPLE_RATE;
    uint16_t blockAlign = 1;
    uint16_t bitsPerSample = 8;
    char data[4] = {'d','a','t','a'};
    uint32_t dataSize;
};

class HX20TapeEncoder {
private:
    std::vector<uint8_t> audioData;

    // Generate a single pulse (rising edge to rising edge)
    void addPulse(int durationUs) {
        int samples = (durationUs * SAMPLE_RATE) / 1000000;
        int halfSamples = samples / 2;
        
        // Rising edge + high period
        for (int i = 0; i < halfSamples; i++) {
            double t = (double)i / halfSamples;
            // Smooth transition using tanh for soft edges
            double value = DC_OFFSET + AMPLITUDE * tanh(4.0 * (t - 0.5));
            audioData.push_back((uint8_t)value);
        }
        
        // Falling edge + low period
        for (int i = 0; i < halfSamples; i++) {
            double t = (double)i / halfSamples;
            double value = DC_OFFSET - AMPLITUDE * tanh(4.0 * (t - 0.5));
            audioData.push_back((uint8_t)value);
        }
    }

    // Add a single bit using pulse-width encoding
    void addBit(bool bit) {
        if (bit) {
            addPulse(PULSE_LONG);  // '1' = long pulse (~1000μs)
        } else {
            addPulse(PULSE_SHORT); // '0' = short pulse (~400μs)
        }
    }

    // Add a byte (LSB first + stop bit)
    void addByte(uint8_t byte) {
        // Send bits 0-7 (LSB first)
        for (int i = 0; i < 8; i++) {
            addBit((byte >> i) & 1);
        }
        // Stop bit (always '1')
        addBit(1);
    }

    // Add synchronization field (80 bits of '0')
    void addSyncField() {
        for (int i = 0; i < SYNC_BITS; i++) {
            addBit(0);
        }
    }

    // Add preamble (FF AA) with extra '1' bit as seen in working implementations
    void addPreamble() {
        addBit(1);       // Extra '1' bit
        addByte(0xFF);
        addByte(0xAA);
    }

    // Add postamble (AA 00)
    void addPostamble() {
        addByte(0xAA);
        addByte(0x00);
    }

    // Calculate CRC-CCITT for block check
    uint16_t calculateCRC(const std::vector<uint8_t>& data) {
        uint16_t crc = 0xFFFF;
        
        for (uint8_t byte : data) {
            crc ^= (uint16_t)byte << 8;
            for (int i = 0; i < 8; i++) {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021; // CRC-CCITT polynomial
                } else {
                    crc = crc << 1;
                }
            }
        }
        
        return crc;
    }
    
    // Calculate CRC-16-Kermit 
    // This is the reflected version of CRC-CCITT
    uint16_t calculateCRC_Kermit(const std::vector<uint8_t>& data) {
        uint16_t crc = 0x0000; // Kermit starts with 0x0000
        
        for (uint8_t byte : data) {
            crc ^= byte; // XOR with LSB
            for (int i = 0; i < 8; i++) {
                if (crc & 0x0001) { // Test LSB (reflected)
                    crc = (crc >> 1) ^ 0x8408; // Reflected polynomial
                } else {
                    crc = crc >> 1;
                }
            }
        }
        
        // Note: Some implementations swap bytes here, but based on PHP code,
        // we write MSB, LSB directly without swapping
        return crc;
    }

    //Add a complete block - new version
    void addBlock(char blockType, uint16_t blockNumber, uint8_t blockID,
                  const std::vector<uint8_t>& data) {
        
        
        // Build block data
        std::vector<uint8_t> blockData;
        
        // Block identification field (4 bytes)
        blockData.push_back(blockType);                     // 'H', 'D', or 'E'
        blockData.push_back((blockNumber >> 8) & 0xFF);     // MSB
        blockData.push_back(blockNumber & 0xFF);            // LSB
        blockData.push_back(blockID);                       // Block ID (0 or 1 for double write)
        // Data field
        blockData.insert(blockData.end(), data.begin(), data.end());
        
        
        // Calculate CRC
        uint16_t crc = KERMIT ? calculateCRC_Kermit(blockData) : calculateCRC(blockData);
        
        blockData.push_back(crc & 0xFF);               //CRC LSB
        blockData.push_back((crc >> 8) & 0xFF);        //CRC MSB
        
        
        // Now write the complete block to audio
        addSyncField();
        addPreamble();
    
        // add Data block w/ header and CRC
        for (uint8_t byte : blockData) {
            addByte(byte);
        }

        
        addPostamble();
        
        if(DEBUG)
        {
            printf("Type: %c Number: %i, Copy: %i, CRC: %hx\n", blockType, blockNumber, blockID, ((crc & 0xFF) << 8) | ((crc >> 8) & 0xFF)) ;
            
            int i = 0;
            for (uint8_t byte : blockData) {
                printf("%02X ", byte);
                i++;
                if(i % 32 == 0) printf("\n");
            }
            printf("\n");
        }
        addInterblockGap(100); //<--- this seems to do the trick
    }
    
   // Add file gap (5 seconds of 0xFF)
    void addFileGap() {
        // 5 seconds -ish
        for (int i = 0; i < 614; i++) {
            addByte(0xFF);
        }
    }

    // Add short interblock gap (~10-50 bytes of 0xFF)
    void addInterblockGap(int bytes) {
        // Short gap between blocks (when tape doesn't stop)
        for (int i = 0; i < bytes; i++) {
            addByte(0xFF);
        }
    }
    
    


    // Create header block data (80 bytes)
    std::vector<uint8_t> createHeaderData(const std::string& filename,
                                          BasicType type) {
        std::vector<uint8_t> header(80, 0x20); // Fill with spaces
        
        // ID field: "HDR1"
        header[0] = 'H';
        header[1] = 'D';
        header[2] = 'R';
        header[3] = '1';
        
        // Filename (8 bytes, space-padded)
        for (size_t i = 0; i < 8 && i < filename.length(); i++) {
            header[4 + i] = filename[i];
        }
        
        /*
        // File type (8 bytes, space-padded)
        for (size_t i = 0; i < 8 && i < filetype.length(); i++) {
            header[12 + i] = filetype[i];
        }
        */
        /*
        header[12]= ' ';
        header[13]= ' ';
        header[14]= ' ';
        
        header[16]= 0xFF;
        header[17]= 0xFF;
        header[18]= 0x00;
        header[19]= 0x00;
        */
        
        header[15]=header[16]=header[17]=header[18]=header[19] = 0x00;
        switch (type) {
            case BasicType::ASCII:
                header[16]=0xFF; header[17]=0xFF;
                break;
            case BasicType::TOKEN:
                header[16]=0x00; header[17]=0x00;
                break;
            case BasicType::SEQUENTIAL:
                header[15]=0x01; header[16]=0xFF; header[17]=0xFF;
                break;
            case BasicType::BINARY:
                header[15]=0x02;
                break;
        }
        
        // Record type: '2' (double write)
        header[20] = '2';
        
        // Block mode: 'S' (short gap)
        header[21] = 'S';
        
        // Block length: "  256" (5 bytes, right-aligned)
        header[22] = ' ';
        header[23] = ' ';
        header[24] = (DATA_BLOCK_SIZE / 100) % 10 + '0';
        header[25] = (DATA_BLOCK_SIZE / 10) % 10 + '0';
        header[26] = DATA_BLOCK_SIZE % 10 + '0';
        
        // Date (MMDDYY)
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char dateStr[7];
        snprintf(dateStr, 7, "%02d%02d%02d", t->tm_mon + 1, t->tm_mday, t->tm_year % 100);
        for (int i = 0; i < 6; i++) {
            header[32 + i] = dateStr[i];
        }
        
        // Time (HHMMSS)
        char timeStr[7];
        snprintf(timeStr, 7, "%02d%02d%02d", t->tm_hour, t->tm_min, t->tm_sec);
        for (int i = 0; i < 6; i++) {
            header[38 + i] = timeStr[i];
        }
        
        // Volume number: "01"
        header[50] = '0';
        header[51] = '1';
        
        // System name: "HX-20   "
        const char* sysname = "HX-20   ";
        for (int i = 0; i < 8; i++) {
            header[52 + i] = sysname[i];
        }
        
        return header;
    }

    // Create header block data (80 bytes)
    std::vector<uint8_t> createFooterData(const std::string& filename,
                                          BasicType type) {
        std::vector<uint8_t> header(80, 0x20); // Fill with spaces
        
        // ID field: "HDR1"
        header[0] = 'E';
        header[1] = 'O';
        header[2] = 'F';
        header[3] = 'D';
        
        // Filename (8 bytes, space-padded)
        for (size_t i = 0; i < 8 && i < filename.length(); i++) {
            header[4 + i] = filename[i];
        }
                
        header[15]=header[16]=header[17]=header[18]=header[19] = 0x00;
        switch (type) {
            case BasicType::ASCII:
                header[16]=0xFF; header[17]=0xFF;
                break;
            case BasicType::TOKEN:
                header[16]=0x00; header[17]=0x00;
                break;
            case BasicType::SEQUENTIAL:
                header[15]=0x01; header[16]=0xFF; header[17]=0xFF;
                break;
            case BasicType::BINARY:
                header[15]=0x02;
                break;
        }
        
        // Record type: '2' (double write)
        header[20] = '2';
        
        // Block mode: 'S' (short gap)
        header[21] = 'S';
        
        // Block length: "  256" (5 bytes, right-aligned)
        header[22] = ' ';
        header[23] = ' ';
        header[24] = (DATA_BLOCK_SIZE / 100) % 10 + '0';
        header[25] = (DATA_BLOCK_SIZE / 10) % 10 + '0';
        header[26] = DATA_BLOCK_SIZE % 10 + '0';
        
        // Date (MMDDYY)
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char dateStr[7];
        snprintf(dateStr, 7, "%02d%02d%02d", t->tm_mon + 1, t->tm_mday, t->tm_year % 100);
        for (int i = 0; i < 6; i++) {
            header[32 + i] = dateStr[i];
        }
        
        // Time (HHMMSS)
        char timeStr[7];
        snprintf(timeStr, 7, "%02d%02d%02d", t->tm_hour, t->tm_min, t->tm_sec);
        for (int i = 0; i < 6; i++) {
            header[38 + i] = timeStr[i];
        }
        
        // Volume number: "01"
        header[50] = '0';
        header[51] = '1';
        
        // System name: "HX-20   "
        const char* sysname = "HX-20   ";
        for (int i = 0; i < 8; i++) {
            header[52 + i] = sysname[i];
        }
        
        return header;
    }
    
    // Create EOF block data (80 bytes)
    std::vector<uint8_t> createEOFData() {
        std::vector<uint8_t> eof(80, 0x20); // Fill with spaces
        
        // ID field: "EOFD"
        eof[0] = 'E';
        eof[1] = 'O';
        eof[2] = 'F';
        eof[3] = 'D';
        
        return eof;
    }

public:
    // Encode ASCII BASIC program
    void encodeBasicProgram(const std::string& programText,
                           const std::string& filename = "PROGRAM",
                           const BasicType filetype = BasicType::ASCII) {
        
        // Add initial file gap
        addFileGap();
                
        // Create and add header block (written twice)
        std::vector<uint8_t> headerData = createHeaderData(filename, filetype);
        addBlock('H', 0, 0, headerData); // First write
        addBlock('H', 0, 1, headerData); // Second write (double write)
        addInterblockGap(100); //100 bytes = 815ms
        
        // Split program into 256-byte data blocks
        std::vector<uint8_t> programBytes(programText.begin(), programText.end());
         
        int blockNumber = 1;
        for (size_t i = 0; i < programBytes.size(); i += DATA_BLOCK_SIZE) {
            
            std::vector<uint8_t> blockData(DATA_BLOCK_SIZE, 0x00);
            
            size_t copySize = std::min((size_t)DATA_BLOCK_SIZE,
                                       programBytes.size() - i);
            std::copy(programBytes.begin() + i,
                     programBytes.begin() + i + copySize,
                     blockData.begin());
            
            
            
            // Write block twice (double write)
            addBlock('D', blockNumber, 0, blockData);
            addBlock('D', blockNumber, 1, blockData);
            addInterblockGap(300);
            blockNumber++;
        }
        
        // Add EOF block (written twice)
        /* original
        std::vector<uint8_t> eofData = createEOFData();
        addBlock('E', blockNumber, 0, eofData);
        addBlock('E', blockNumber, 1, eofData);
        */
        std::vector<uint8_t> footerData = createFooterData(filename, filetype);
        addBlock('E', blockNumber, 0, footerData);
        addBlock('E', blockNumber, 1, footerData);
        
        // Add final file gap
        addFileGap();
//        addBit(0);
        
    }

    // Normalize audio to target amplitude
    void normalizeAudio(double targetAmplitude = 50.0) {
        if (audioData.empty()) return;
        
        // Find min and max values
        uint8_t minVal = 255, maxVal = 0;
        for (uint8_t sample : audioData) {
            if (sample < minVal) minVal = sample;
            if (sample > maxVal) maxVal = sample;
        }
        
        // Calculate current center and amplitude
        double currentCenter = (minVal + maxVal) / 2.0;
        double currentAmplitude = (maxVal - minVal) / 2.0;
        
        if (currentAmplitude < 0.1) return; // Avoid division by zero
        
        // Calculate scaling factor
        double scale = targetAmplitude / currentAmplitude;
        
        // Normalize all samples
        for (size_t i = 0; i < audioData.size(); i++) {
            double centered = audioData[i] - currentCenter;
            double scaled = centered * scale;
            double result = 128.0 + scaled; // Re-center at 128
            
            // Clamp to valid range
            if (result < 0) result = 0;
            if (result > 255) result = 255;
            
            audioData[i] = (uint8_t)result;
        }
        
        std::cout << "Normalized: amplitude " << currentAmplitude
                  << " -> " << targetAmplitude << " (scale: " << scale << "x)\n";
    }

    // Save to WAV file
    bool saveToWAV(const std::string& filename, int normalize = 50) {
        if (normalize > 0) {
            normalizeAudio(normalize); // Normalize to ±40 amplitude
        }
        
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not create file " << filename << std::endl;
            return false;
        }

        WAVHeader header;
        header.dataSize = audioData.size();
        header.fileSize = sizeof(WAVHeader) - 8 + audioData.size();

        file.write(reinterpret_cast<char*>(&header), sizeof(WAVHeader));
        file.write(reinterpret_cast<char*>(audioData.data()), audioData.size());
        
        file.close();
        return true;
    }

    

    void reset() {
        audioData.clear();
    }
};

void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " -i <input.bas> -o <output.wav> [-n <name>] [-t <type>]\n\n"
        << "Encodes an ASCII BASIC program to HX-20 tape format\n"
        << "Uses official pulse-width encoding from HX-20 documentation\n\n"
        << "Options:\n"
        << "  -i <file>   ASCII BASIC source file (REQUIRED)\n"
        << "  -o <file>   Output WAV file (11025Hz, 8-bit mono) (REQUIRED)\n"
        << "  -n <name>   Program name (max 8 chars, default: PROGRAM)\n"
        /* << "  -t <type>   File type    (ASCII or TOKEN, default: ASCII)\n" */
        << "  -a <level>  Amplitude    (default: 95) \n"
        << "  -d          Dump encoded payload  \n"
        << "  -h          Show this help and exit\n\n"
        << "Example:\n"
        << "  " << prog << " -i hello.bas -o hello.wav -n HELLO -t BAS\n";
}

BasicType detectFileType(const std::string& ft) {
    std::string upper = ft;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper.find("TOKEN") != std::string::npos) return BasicType::TOKEN;
    return BasicType::ASCII;
}

int main(int argc, char* argv[]) {
    std::cout << "HX-20 Tape Encoder v2.0 (Official Format)\n";
    std::cout << "==========================================\n\n";

    std::string inputFile;
    std::string outputFile;
    std::string programName = "PROGRAM";
    //std::string fileType = "";
    int normalizeLevel = 95;
    BasicType fileType = BasicType::ASCII;
    

    int opt;
    while ((opt = getopt(argc, argv, ":i:o:n:a:dh")) != -1) {
        switch (opt) {
            case 'i':
                inputFile = optarg ? std::string(optarg) : "";
                break;
            case 'o':
                outputFile = optarg ? std::string(optarg) : "";
                break;
            case 'n':
                programName = optarg ? std::string(optarg) : programName;
                break;
//            case 't':
//                fileType = optarg ? std::string(optarg) : fileType;
//                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case 'a':
                normalizeLevel = atoi(optarg);
                break;
            case 'd':
                DEBUG = true;
                break;
            case ':': // missing argument to option
                std::cerr << "Error: Option '-" << char(optopt) << "' requires an argument.\n";
                printUsage(argv[0]);
                return 1;
            case '?': // unknown option
            default:
                std::cerr << "Error: Unknown option '-" << char(optopt) << "'.\n";
                printUsage(argv[0]);
                return 1;
        }
    }

    // Validate required options
    if (inputFile.empty()) {
        std::cerr << "Error: -i <input.bas> is required.\n";
        printUsage(argv[0]);
        return 1;
    }
    if (outputFile.empty()) {
        fs::path p(inputFile);
        outputFile = p.stem().string() + ".wav";
    }
    

    // Convert to uppercase and pad/truncate
    std::transform(programName.begin(), programName.end(),
                   programName.begin(), ::toupper);

//    std::transform(fileType.begin(), fileType.end(), fileType.begin(), ::toupper);
    
    programName.resize(8, ' ');
    //fileType.resize(8, ' ');

    // Read input file
    std::ifstream inFile(inputFile);
    if (!inFile) {
        std::cerr << "Error: Could not open input file " << inputFile << std::endl;
        return 1;
    }

    
    std::string programText((std::istreambuf_iterator<char>(inFile)),
                            std::istreambuf_iterator<char>());
    inFile.close();

    
    // Determine operation based on first byte
    bool isTokenized = !programText.empty() && (uint8_t)programText[0] == 0xFF;
    if(isTokenized) fileType = BasicType::TOKEN;

    
    if (programText.empty()) {
        std::cerr << "Error: Input file is empty\n";
        return 1;
    }

    // Ensure CRLF line endings
    std::string normalized;
    for (size_t i = 0; i < programText.length(); i++) {
        if (programText[i] == '\n' && (i == 0 || programText[i-1] != '\r')) {
            normalized += "\r\n";
        } else if (programText[i] != '\r' ||
                   (i + 1 < programText.length() && programText[i+1] == '\n')) {
            normalized += programText[i];
        }
    }

    std::cout << "Input file: " << inputFile << "\n";
    std::cout << "Output file: " << outputFile << "\n";
    std::cout << "Program name: " << programName << "\n";
    std::cout << "Program size: " << normalized.length() << " bytes\n";
    std::cout << "Input is " << ( fileType == BasicType::ASCII ? "pure ASCII" : "tokenized ASCII") << "\n\n";
    
    // Encode
    std::cout << "Encoding with pulse-width modulation...\n";
    HX20TapeEncoder encoder;
    
    encoder.encodeBasicProgram(normalized, programName, fileType);


    // Save with normalization
    std::cout << "Writing WAV file...\n";
    if (!encoder.saveToWAV(outputFile, normalizeLevel)) {
        return 1;
    }

    std::cout << "\nSuccess! WAV file created: " << outputFile << "\n";
    std::cout << "\nTo load on HX-20:\n";
    std::cout << "1. Connect audio output to HX-20's cassette input (CAS1)\n";
    std::cout << "2. On HX-20, type: LOAD\"CAS1:\"\n";
    std::cout << "3. Press RETURN and start playback immediately\n";
    std::cout << "4. Adjust volume if needed (try 70-90% initially)\n";

    return 0;
}
