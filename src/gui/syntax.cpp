/**
 * @file syntax.cpp
 * @brief Syntax Highlighter Implementation for Assembly (.as) and Astra C (.ac).
 */

#include "em836/syntax.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace em836 {

ImVec4 SyntaxHighlighter::GetTokenColor(TokenType type) {
    switch (type) {
        case TokenType::Comment:
            return ImVec4(0.42f, 0.65f, 0.35f, 1.0f); // #6A9955 (Green)
        case TokenType::Directive:
        case TokenType::Preprocessor:
            return ImVec4(0.77f, 0.52f, 0.77f, 1.0f); // #C586C0 (Purple)
        case TokenType::Instruction:
            return ImVec4(0.31f, 0.76f, 1.00f, 1.0f); // #4FC1FF (Light Blue/Cyan)
        case TokenType::Register:
            return ImVec4(0.86f, 0.86f, 0.67f, 1.0f); // #DCDCAA (Soft Yellow)
        case TokenType::Keyword:
            return ImVec4(0.34f, 0.61f, 0.84f, 1.0f); // #569CD6 (Blue)
        case TokenType::Type:
        case TokenType::PointerType:
            return ImVec4(0.31f, 0.79f, 0.69f, 1.0f); // #4EC9B0 (Teal)
        case TokenType::Constant:
            return ImVec4(0.31f, 0.76f, 1.00f, 1.0f); // #4FC1FF (Cyan)
        case TokenType::Number:
            return ImVec4(0.71f, 0.81f, 0.66f, 1.0f); // #B5CEA8 (Light Green)
        case TokenType::String:
            return ImVec4(0.81f, 0.57f, 0.47f, 1.0f); // #CE9178 (Orange/Peach)
        case TokenType::Label:
            return ImVec4(0.86f, 0.86f, 0.67f, 1.0f); // #DCDCAA (Yellow)
        case TokenType::Operator:
            return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        case TokenType::Default:
        default:
            return ImVec4(0.85f, 0.85f, 0.85f, 1.0f); // #D4D4D4 (Off-white)
    }
}

// -----------------------------------------------------------------------------
// Assembly (.as) Lexical Data
// -----------------------------------------------------------------------------

static const std::unordered_set<std::string> kAsDirectives = {
    ".use32", ".use16", ".org", ".data", ".rodata", ".code", ".times"
};

static const std::unordered_set<std::string> kAsTypes = {
    "db", "dw", "dd", "byte", "word", "dword", "real4", "ptr", "near", "far", "short"
};

static const std::unordered_set<std::string> kAsRegisters = {
    "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip",
    "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
    "ah", "al", "bh", "bl", "ch", "cl", "dh", "dl",
    "cs", "ds", "es", "fs", "gs", "ss",
    "cr0", "cr2", "cr3", "cr4", "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",
    "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
    "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
};

static const std::unordered_set<std::string> kAsInstructions = {
    "aaa", "aad", "aam", "aas", "adc", "add", "and", "arpl", "bound", "bsf", "bsr", "bt", "btc", "btr", "bts",
    "call", "cbw", "cdq", "clc", "cld", "cli", "clts", "cmc", "cmp", "cmpsb", "cmpsd", "cmpsw", "cpuid", "cwd", "cwde",
    "daa", "das", "dec", "div", "enter", "hlt", "idiv", "imul", "in", "inc", "insb", "insd", "insw", "int", "int3", "into", "iret",
    "ja", "jae", "jb", "jbe", "jc", "jcxz", "je", "jecxz", "jg", "jge", "jl", "jle", "jmp", "jna", "jnae", "jnb", "jnbe",
    "jnc", "jne", "jng", "jnge", "jnl", "jnle", "jno", "jnp", "jns", "jnz", "jo", "jp", "jpe", "jpo", "js", "jz",
    "lahf", "lar", "lds", "lea", "leave", "les", "lfs", "lgdt", "lgs", "lidt", "lldt", "lmsw", "lock",
    "lodsb", "lodsd", "lodsw", "loop", "loope", "loopne", "loopnz", "loopz", "lsl", "lss", "ltr",
    "mov", "movsb", "movsd", "movsw", "movsx", "movzx", "mul", "neg", "nop", "not", "or", "out", "outsb", "outsd", "outsw",
    "pop", "popa", "popad", "popf", "popfd", "push", "pusha", "pushad", "pushf", "pushfd",
    "rcl", "rcr", "rdtsc", "rep", "repe", "repne", "repnz", "repz", "ret", "retf", "rol", "ror",
    "sahf", "sar", "sbb", "scasb", "scasd", "scasw", "seta", "setae", "setb", "setbe", "setc", "sete", "setg", "setge",
    "setl", "setle", "setna", "setnae", "setnb", "setnbe", "setnc", "setne", "setng", "setnge", "setnl", "setnle",
    "setno", "setnp", "setns", "setnz", "seto", "setp", "setpe", "setpo", "sets", "setz",
    "sgdt", "shl", "shld", "shr", "shrd", "sidt", "sldt", "smsw", "stc", "std", "sti",
    "stosb", "stosd", "stosw", "str", "sub", "test", "verr", "verw", "wait", "xchg", "xlat", "xor"
};

// -----------------------------------------------------------------------------
// Astra C (.ac) Lexical Data
// -----------------------------------------------------------------------------

static const std::unordered_set<std::string> kAcKeywords = {
    "if", "else", "for", "while", "do", "return", "break", "continue",
    "switch", "case", "default", "goto", "struct", "union", "enum",
    "typedef", "sizeof", "static", "local", "asm"
};

static const std::unordered_set<std::string> kAcTypes = {
    "u8", "u16", "u32", "i8", "i16", "i32", "f32", "u0", "void", "bool", "boolean", "voidptr"
};

static const std::unordered_set<std::string> kAcPointerTypes = {
    "pu8", "pu16", "pu32", "ppu8", "ppu16", "ppu32",
    "pi8", "pi16", "pi32", "ppi8", "ppi16", "ppi32"
};

static const std::unordered_set<std::string> kAcConstants = {
    "true", "false", "nullptr", "arch_i386", "arch_i286"
};

// -----------------------------------------------------------------------------
// Assembly (.as) Tokenizer
// -----------------------------------------------------------------------------

std::vector<SyntaxToken> SyntaxHighlighter::TokenizeAs(const std::string& line) {
    std::vector<SyntaxToken> tokens;
    size_t i = 0;
    size_t len = line.length();

    // Check preprocessor directive (^#...)
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos && line[firstNonSpace] == '#') {
        tokens.push_back({line, TokenType::Preprocessor});
        return tokens;
    }

    while (i < len) {
        char c = line[i];

        // Semicolon comments (; ...)
        if (c == ';') {
            tokens.push_back({line.substr(i), TokenType::Comment});
            break;
        }

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            size_t start = i;
            while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::Default});
            continue;
        }

        // Strings ("...")
        if (c == '"') {
            size_t start = i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::String});
            continue;
        }

        // Character literals ('...')
        if (c == '\'') {
            size_t start = i++;
            while (i < len && line[i] != '\'') {
                if (line[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::String});
            continue;
        }

        // Hex (0x...) or Binary (0b...) or Decimal Number
        if (std::isdigit(c) || (c == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X' || line[i+1] == 'b' || line[i+1] == 'B'))) {
            size_t start = i;
            if (line[i] == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                i += 2;
                while (i < len && std::isxdigit(static_cast<unsigned char>(line[i]))) ++i;
            } else if (line[i] == '0' && i + 1 < len && (line[i+1] == 'b' || line[i+1] == 'B')) {
                i += 2;
                while (i < len && (line[i] == '0' || line[i] == '1')) ++i;
            } else {
                while (i < len && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            }
            tokens.push_back({line.substr(start, i - start), TokenType::Number});
            continue;
        }

        // Directives (.code, .use32, etc.)
        if (c == '.' && i + 1 < len && std::isalpha(static_cast<unsigned char>(line[i+1]))) {
            size_t start = i++;
            while (i < len && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
            std::string word = line.substr(start, i - start);
            std::string lowerWord = word;
            std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), [](unsigned char ch) { return std::tolower(ch); });
            if (kAsDirectives.count(lowerWord)) {
                tokens.push_back({word, TokenType::Directive});
            } else {
                tokens.push_back({word, TokenType::Default});
            }
            continue;
        }

        // Identifiers, instructions, registers, labels
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == '$') {
            size_t start = i++;
            while (i < len && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_' || line[i] == '@' || line[i] == '$')) ++i;
            std::string word = line.substr(start, i - start);

            // Check if label (followed by ':')
            if (i < len && line[i] == ':') {
                ++i;
                tokens.push_back({word + ":", TokenType::Label});
                continue;
            }

            std::string lowerWord = word;
            std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), [](unsigned char ch) { return std::tolower(ch); });

            if (kAsInstructions.count(lowerWord)) {
                tokens.push_back({word, TokenType::Instruction});
            } else if (kAsRegisters.count(lowerWord)) {
                tokens.push_back({word, TokenType::Register});
            } else if (kAsTypes.count(lowerWord)) {
                tokens.push_back({word, TokenType::Type});
            } else if (word[0] == '@') {
                tokens.push_back({word, TokenType::Label});
            } else {
                tokens.push_back({word, TokenType::Default});
            }
            continue;
        }

        // Operators & delimiters
        tokens.push_back({std::string(1, c), TokenType::Operator});
        ++i;
    }

    return tokens;
}

// -----------------------------------------------------------------------------
// Astra C (.ac) Tokenizer
// -----------------------------------------------------------------------------

std::vector<SyntaxToken> SyntaxHighlighter::TokenizeAc(const std::string& line, bool& inBlockComment) {
    std::vector<SyntaxToken> tokens;
    size_t i = 0;
    size_t len = line.length();

    // Check preprocessor directive
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if (!inBlockComment && firstNonSpace != std::string::npos && line[firstNonSpace] == '#') {
        tokens.push_back({line, TokenType::Preprocessor});
        return tokens;
    }

    while (i < len) {
        // Block comment continuation
        if (inBlockComment) {
            size_t endComment = line.find("*/", i);
            if (endComment == std::string::npos) {
                tokens.push_back({line.substr(i), TokenType::Comment});
                break;
            } else {
                tokens.push_back({line.substr(i, endComment + 2 - i), TokenType::Comment});
                i = endComment + 2;
                inBlockComment = false;
                continue;
            }
        }

        char c = line[i];

        // Double-slash comments (// ...)
        if (c == '/' && i + 1 < len && line[i+1] == '/') {
            tokens.push_back({line.substr(i), TokenType::Comment});
            break;
        }

        // Block comment begin (/* ...)
        if (c == '/' && i + 1 < len && line[i+1] == '*') {
            size_t endComment = line.find("*/", i + 2);
            if (endComment == std::string::npos) {
                tokens.push_back({line.substr(i), TokenType::Comment});
                inBlockComment = true;
                break;
            } else {
                tokens.push_back({line.substr(i, endComment + 2 - i), TokenType::Comment});
                i = endComment + 2;
                continue;
            }
        }

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            size_t start = i;
            while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::Default});
            continue;
        }

        // Strings ("...")
        if (c == '"') {
            size_t start = i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::String});
            continue;
        }

        // Character literals ('...')
        if (c == '\'') {
            size_t start = i++;
            while (i < len && line[i] != '\'') {
                if (line[i] == '\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({line.substr(start, i - start), TokenType::String});
            continue;
        }

        // Hex / Binary / Decimal Numbers
        if (std::isdigit(c) || (c == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X' || line[i+1] == 'b' || line[i+1] == 'B'))) {
            size_t start = i;
            if (line[i] == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                i += 2;
                while (i < len && std::isxdigit(static_cast<unsigned char>(line[i]))) ++i;
            } else if (line[i] == '0' && i + 1 < len && (line[i+1] == 'b' || line[i+1] == 'B')) {
                i += 2;
                while (i < len && (line[i] == '0' || line[i] == '1')) ++i;
            } else {
                while (i < len && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            }
            tokens.push_back({line.substr(start, i - start), TokenType::Number});
            continue;
        }

        // Identifiers, keywords, types
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i++;
            while (i < len && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
            std::string word = line.substr(start, i - start);

            std::string lowerWord = word;
            std::transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), [](unsigned char ch) { return std::tolower(ch); });

            if (kAcKeywords.count(lowerWord)) {
                tokens.push_back({word, TokenType::Keyword});
            } else if (kAcTypes.count(lowerWord)) {
                tokens.push_back({word, TokenType::Type});
            } else if (kAcPointerTypes.count(lowerWord)) {
                tokens.push_back({word, TokenType::PointerType});
            } else if (kAcConstants.count(lowerWord)) {
                tokens.push_back({word, TokenType::Constant});
            } else {
                tokens.push_back({word, TokenType::Default});
            }
            continue;
        }

        // Operators & punctuation
        tokens.push_back({std::string(1, c), TokenType::Operator});
        ++i;
    }

    return tokens;
}

void SyntaxHighlighter::RenderColoredLine(const std::vector<SyntaxToken>& tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (i > 0) {
            ImGui::SameLine(0, 0);
        }
        ImGui::TextColored(GetTokenColor(tok.type), "%s", tok.text.c_str());
    }
}

} // namespace em836
