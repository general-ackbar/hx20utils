#include <map>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

// Token tables
const uint8_t FUNCTION_ESCAPE = 0xFF;

std::map<std::string, uint8_t> basicCommands = {
    {"END", 0x80}, {"FOR", 0x81}, {"NEXT", 0x82}, {"DATA", 0x83},
    {"DIM", 0x84}, {"READ", 0x85}, {"LET", 0x86}, {"GO", 0x87},
    {"RUN", 0x88}, {"IF", 0x89}, {"RESTORE", 0x8A}, {"RETURN", 0x8B},
    {"REM", 0x8C}, {"'", 0x8D}, {"STOP", 0x8E}, {"ELSE", 0x8F},
    {"TRON", 0x90}, {"TROFF", 0x91}, {"SWAP", 0x92}, {"DEFSTR", 0x93},
    {"DEFINT", 0x94}, {"DEFSNG", 0x95}, {"DEFDBL", 0x96}, {"DEFFIL", 0x97},
    {"ON", 0x98}, {"LPRINT", 0x99}, {"LLIST", 0x9A}, {"RENUM", 0x9B},
    {"ERROR", 0x9C}, {"RESUME", 0x9D}, {"AUTO", 0x9E}, {"DELETE", 0x9F},
    {"DEF", 0xA0}, {"POKE", 0xA1}, {"PRINT", 0xA2}, {"CONT", 0xA3},
    {"LIST", 0xA4}, {"CLEAR", 0xA5}, {"OPTION", 0xA6}, {"RANDOMIZE", 0xA7},
    {"WHILE", 0xA8}, {"WEND", 0xA9}, {"NEW", 0xAA}, {"ERASE", 0xAB},
    {"LOADM", 0xAC}, {"LOAD?", 0xAD}, {"SAVEM", 0xAE}, {"SAVE", 0xAF},
    {"LOAD", 0xB0}, {"MERGE", 0xB1}, {"OPEN", 0xB2}, {"CLOSE", 0xB3},
    {"LINE", 0xB4}, {"SCROLL", 0xB5}, {"SOUND", 0xB6}, {"MON", 0xB7},
    {"FILES", 0xB8}, {"MOTOR", 0xB9}, {"PUT", 0xBA}, {"GET", 0xBB},
    {"LOCATES", 0xBC}, {"LOCATE", 0xBD}, {"CLS", 0xBE}, {"KEY", 0xBF},
    {"WIDTH", 0xC0}, {"PSET", 0xC1}, {"PRESET", 0xC2}, {"COPY", 0xC3},
    {"EXEC", 0xC4}, {"WIND", 0xC5}, {"GCLS", 0xC6}, {"SCREEN", 0xC7},
    {"COLOR", 0xC8}, {"LOGIN", 0xC9}, {"TITLE", 0xCA}, {"STAT", 0xCB},
    {"PCOPY", 0xCC}, {"MEMSET", 0xCD}, {"BASE", 0xCE}, {"TAB", 0xCF},
    {"TO", 0xD0}, {"SUB", 0xD1}, {"FN", 0xD2}, {"SPC", 0xD3},
    {"USING", 0xD4}, {"USR", 0xD5}, {"ERL", 0xD6}, {"ERR", 0xD7},
    {"OFF", 0xD8}, {"ALL", 0xD9}, {"THEN", 0xDA}, {"NOT", 0xDB},
    {"STEP", 0xDC}, {"+", 0xDD}, {"-", 0xDE}, {"*", 0xDF},
    {"/", 0xE0}, {"^", 0xE1}, {"AND", 0xE2}, {"OR", 0xE3},
    {"XOR", 0xE4}, {"EQV", 0xE5}, {"IMP", 0xE6}, {"MOD", 0xE7},
    {"\\", 0xE8}, {">", 0xE9}, {"=", 0xEA}, {"<", 0xEB}
};

std::map<std::string, uint8_t> basicFunctions = {
    {"SGN", 0x80}, {"INT", 0x81}, {"ABS", 0x82}, {"FRE", 0x83},
    {"POS", 0x84}, {"SQR", 0x85}, {"LOG", 0x86}, {"EXP", 0x87},
    {"COS", 0x88}, {"SIN", 0x89}, {"TAN", 0x8A}, {"ATN", 0x8B},
    {"PEEK", 0x8C}, {"LEN", 0x8D}, {"STR$", 0x8E}, {"VAL", 0x8F},
    {"ASC", 0x90}, {"CHR$", 0x91}, {"EOF", 0x92}, {"LOF", 0x93},
    {"CINT", 0x94}, {"CSNG", 0x95}, {"CDBL", 0x96}, {"FIX", 0x97},
    {"SPACE$", 0x98}, {"HEX$", 0x99}, {"OCT$", 0x9A}, {"LEFT$", 0x9B},
    {"RIGHT$", 0x9C}, {"MID$", 0x9D}, {"INSTR", 0x9E}, {"VARPTR", 0x9F},
    {"STRING$", 0xA0}, {"RND", 0xA1}, {"TIME", 0xA2}, {"DATE", 0xA3},
    {"DAY", 0xA4}, {"INKEY$", 0xA5}, {"INPUT", 0xA6}, {"CSRLIN", 0xA7},
    {"POINT", 0xA8}, {"TAPCNT", 0xA9}
};

// Reverse lookup maps
std::map<uint8_t, std::string> commandTokens;
std::map<uint8_t, std::string> functionTokens;

void initReverseMaps() {
    for (const auto& pair : basicCommands) {
        commandTokens[pair.second] = " " + pair.first + " ";
    }
    for (const auto& pair : basicFunctions) {
        functionTokens[pair.second] = " " + pair.first + " ";
    }
}

uint16_t readWord(std::ifstream& in) {
    uint8_t high = in.get();
    uint8_t low = in.get();
    return (high << 8) | low;
}

void writeWord(std::ofstream& out, uint16_t value) {
    out.put((value >> 8) & 0xFF);  // Big-endian: high byte first
    out.put(value & 0xFF);          // Low byte second
}

std::string tokenizeBasicLine(const std::string& line, int lineNumber) {
    std::string result;
    size_t pos = 0;
    bool inString = false;
    bool inRemark = false;
    
    // Skip line number in input if present
    while (pos < line.length() && std::isdigit(line[pos])) {
        pos++;
    }
    while (pos < line.length() && std::isspace(line[pos])) {
        pos++;
    }
    
   
    while (pos < line.length()) {
        char ch = line[pos];
        
        // Handle strings - pass through unchanged
        if (ch == '"') {
            result += ch;
            pos++;
            inString = !inString;
            continue;
        }
        
        if (inString) {
            result += ch;
            pos++;
            continue;
        }
        
        // Handle REM - rest of line is comment
        if (inRemark) {
            result += ch;
            pos++;
            continue;
        }
        
        // Skip whitespace (preserve it)
        if (std::isspace(ch)) {
            result += ch;
            pos++;
            continue;
        }
        
        bool matched = false;
        
        // Try to match functions first (longest match)
        for (auto it = basicFunctions.rbegin(); it != basicFunctions.rend(); ++it) {
            const std::string& keyword = it->first;
            if (pos + keyword.length() <= line.length()) {
                std::string upper = line.substr(pos, keyword.length());
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                
                if (upper == keyword) {
                    result += (char)FUNCTION_ESCAPE;
                    result += (char)it->second;
                    pos += keyword.length();
                    matched = true;
                    break;
                }
            }
        }
        
        if (matched) continue;
        
        // Try to match commands (longest match)
        for (auto it = basicCommands.rbegin(); it != basicCommands.rend(); ++it) {
            const std::string& keyword = it->first;
            if (pos + keyword.length() <= line.length()) {
                std::string upper = line.substr(pos, keyword.length());
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                
                if (upper == keyword) {
                    bool isOperator = (keyword.length() <= 2 && !std::isalpha(keyword[0])) ||
                                     (keyword == "AND" || keyword == "OR" || keyword == "XOR" ||
                                      keyword == "EQV" || keyword == "IMP" || keyword == "MOD" ||
                                      keyword == "NOT");
                    
                    bool validMatch = false;
                    
                    if (isOperator) {
                        validMatch = true;
                    } else {
                        size_t nextPos = pos + keyword.length();
                        if (nextPos >= line.length()) {
                            validMatch = true;
                        } else {
                            char nextChar = line[nextPos];
                            if (!std::isalpha(nextChar)) {
                                validMatch = true;
                            } else if (std::isupper(nextChar)) {
                                if (nextPos + 1 >= line.length() || !std::isalpha(line[nextPos + 1])) {
                                    validMatch = true;
                                } else if (std::islower(line[nextPos + 1])) {
                                    validMatch = true;
                                }
                            } else if (std::islower(nextChar)) {
                                validMatch = true;
                            }
                        }
                    }
                    
                    if (validMatch) {
                        result += (char)it->second;
                        pos += keyword.length();
                        
                        if (upper == "REM" || upper == "'") {
                            inRemark = true;
                        }
                        
                        matched = true;
                        break;
                    }
                }
            }
        }
        
        if (matched) continue;
        
        result += ch;
        pos++;
    }
    
    return result;
}

std::string tokenizeBasicProgram(const std::string& program) {
    std::vector<std::string> lines;
    std::stringstream ss(program);
    std::string line;
    
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    
    std::ostringstream binary;
    binary.put(0xFF);
    
    size_t sizePos = binary.tellp();
    binary.put(0x00);
    binary.put(0x00);
    
    for (const auto& line : lines) {
        size_t pos = 0;
        while (pos < line.length() && std::isspace(line[pos])) pos++;
        
        int lineNumber = 0;
        while (pos < line.length() && std::isdigit(line[pos])) {
            lineNumber = lineNumber * 10 + (line[pos] - '0');
            pos++;
        }
        
        if (lineNumber == 0) continue;
        
        binary.put(0x00);
        binary.put(0x00);
        
        // Big-endian line number
        binary.put((lineNumber >> 8) & 0xFF);
        binary.put(lineNumber & 0xFF);
        
        std::string tokenized = tokenizeBasicLine(line, lineNumber);
        binary.write(tokenized.c_str(), tokenized.length());
        
        binary.put(0x00);
    }
    
    std::string binaryData = binary.str();
    uint16_t totalSize = binaryData.length();
    binaryData[1] = (totalSize >> 8) & 0xFF;  // Big-endian
    binaryData[2] = totalSize & 0xFF;
    
    return binaryData;
}

std::string detokenizeBasicProgram(const std::string& binaryData) {
    initReverseMaps();
    std::ostringstream output;
    
    if (binaryData.empty() || (uint8_t)binaryData[0] != 0xFF) {
        return "Error: Not a valid HX-20 BASIC file\n";
    }
    
    uint16_t size = ((uint8_t)binaryData[1] << 8) | (uint8_t)binaryData[2];
    size_t pos = 3;
    
    while (pos < size && pos < binaryData.length()) {
        // Skip dummy word
        pos += 2;
        if (pos >= binaryData.length()) break;
        
        // Read line number (big-endian)
        uint16_t lineNumber = ((uint8_t)binaryData[pos] << 8) | (uint8_t)binaryData[pos + 1];
        pos += 2;
        
        output << lineNumber << " ";
        
        // Read and detokenize line content
        bool inString = false;
        while (pos < binaryData.length() && (uint8_t)binaryData[pos] != 0x00) {
            uint8_t token = (uint8_t)binaryData[pos++];
            
            // Check for quote to toggle string mode
            if (token == '"') {
                output << '"';
                inString = !inString;
                continue;
            }
            
            // Inside strings, output everything as-is (no detokenization)
            if (inString) {
                output << (char)token;
                continue;
            }
            
            // Outside strings, detokenize normally
            if (token == FUNCTION_ESCAPE) {
                if (pos >= binaryData.length()) break;
                token = (uint8_t)binaryData[pos++];
                if (functionTokens.find(token) != functionTokens.end()) {
                    output << functionTokens[token];
                } else {
                    output << (char)token;
                }
            } else {
                if (commandTokens.find(token) != commandTokens.end()) {
                    output << commandTokens[token];
                } else {
                    output << (char)token;
                }
            }
        }
        
        // Clean up multiple spaces
        std::string lineStr = output.str();
        size_t lineStart = lineStr.rfind('\n');
        if (lineStart == std::string::npos) lineStart = 0;
        else lineStart++;
        
        std::string currentLine = lineStr.substr(lineStart);
        size_t firstSpace = currentLine.find(' ');
        if (firstSpace != std::string::npos) {
            std::string cleaned = currentLine.substr(0, firstSpace + 1);
            std::string rest = currentLine.substr(firstSpace + 1);
            
            // Remove redundant spaces
            std::string result;
            bool lastWasSpace = false;
            for (char ch : rest) {
                if (ch == ' ') {
                    if (!lastWasSpace) result += ch;
                    lastWasSpace = true;
                } else {
                    result += ch;
                    lastWasSpace = false;
                }
            }
            
            output.str("");
            output << lineStr.substr(0, lineStart) << cleaned << result;
        }
        
        output << "\n";
        pos++; // Skip line terminator
    }
    
    return output.str();
}

void printUsage(const char* progName) {
    std::cerr << "HX-20 BASIC Tokenizer/Detokenizer\n";
    std::cerr << "Usage: " << progName << " -i <input> -o <output>\n";
    std::cerr << "  -i <file>   Input file\n";
    std::cerr << "  -o <file>   Output file\n";
    std::cerr << "\nIf input starts with 0xFF, it will be detokenized to ASCII.\n";
    std::cerr << "Otherwise, it will be tokenized to binary format.\n";
}

int main(int argc, char* argv[]) {
    std::string inputFile;
    std::string outputFile;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            inputFile = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    if (inputFile.empty() || outputFile.empty()) {
        printUsage(argv[0]);
        return 1;
    }
    
    // Read input file
    std::ifstream inFile(inputFile, std::ios::binary);
    if (!inFile) {
        std::cerr << "Error: Could not open input file: " << inputFile << "\n";
        return 1;
    }
    
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    std::string inputData = buffer.str();
    inFile.close();
    
    // Determine operation based on first byte
    bool isTokenized = !inputData.empty() && (uint8_t)inputData[0] == 0xFF;
    std::string output;
    
    if (isTokenized) {
        std::cout << "Detokenizing...\n";
        output = detokenizeBasicProgram(inputData);
        
        std::ofstream outFile(outputFile);
        if (!outFile) {
            std::cerr << "Error: Could not open output file: " << outputFile << "\n";
            return 1;
        }
        outFile << output;
        outFile.close();
    } else {
        std::cout << "Tokenizing...\n";
        output = tokenizeBasicProgram(inputData);
        
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            std::cerr << "Error: Could not open output file: " << outputFile << "\n";
            return 1;
        }
        outFile.write(output.c_str(), output.length());
        outFile.close();
    }
    
    std::cout << "Complete!\n";
    std::cout << "Input:  " << inputFile << " (" << inputData.length() << " bytes)\n";
    std::cout << "Output: " << outputFile << " (" << output.length() << " bytes)\n";
    
    return 0;
}
