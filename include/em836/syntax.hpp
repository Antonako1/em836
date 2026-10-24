/**
 * @file syntax.hpp
 * @brief Syntax Highlighter for Assembly (.as) and Astra C (.ac) matching AC VSCODE.
 */

#pragma once

#include <string>
#include <vector>
#include <imgui.h>

namespace em836 {

/**
 * @enum TokenType
 * @brief Lexical token types mapped to VSCode theme colors.
 */
enum class TokenType {
    Default,
    Comment,
    Directive,
    Preprocessor,
    Instruction,
    Register,
    Keyword,
    Type,
    PointerType,
    Constant,
    Number,
    String,
    Label,
    Operator
};

/**
 * @struct SyntaxToken
 * @brief A single colored substring in a tokenized line of code.
 */
struct SyntaxToken {
    std::string text;
    TokenType type = TokenType::Default;
};

/**
 * @class SyntaxHighlighter
 * @brief Tokenizes and formats source lines according to AC VSCODE language definitions.
 */
class SyntaxHighlighter {
public:
    /**
     * @brief Returns the ImVec4 display color for a given token type.
     */
    static ImVec4 GetTokenColor(TokenType type);

    /**
     * @brief Tokenizes a single line of Assembly source code (.as).
     */
    static std::vector<SyntaxToken> TokenizeAs(const std::string& line);

    /**
     * @brief Tokenizes a single line of Astra C source code (.ac).
     * @param line Input line.
     * @param inBlockComment State tracker across multiple lines for block comments.
     */
    static std::vector<SyntaxToken> TokenizeAc(const std::string& line, bool& inBlockComment);

    /**
     * @brief Renders an array of tokens horizontally using ImGui::TextColored and SameLine().
     */
    static void RenderColoredLine(const std::vector<SyntaxToken>& tokens);
};

} // namespace em836
