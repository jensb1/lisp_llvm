#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>
#include <stdexcept>

// TokenType enum to represent different token categories in LISP
enum class TokenType {
    LEFT_PAREN,    // (
    RIGHT_PAREN,   // )
    LEFT_BRACKET,  // [
    RIGHT_BRACKET, // ]
    QUOTE,         // '
    SYMBOL,        // identifiers, operators, etc.
    NUMBER,        // numeric literals
    STRING,        // string literals
    BOOLEAN,       // #t or #f
    COMMENT,       // ; comment
    EOF_TOKEN      // end of file
};

// Token class to represent a lexical token
class Token {
public:
    TokenType type;
    std::string lexeme;
    int line;
    
    Token(TokenType type, std::string lexeme, int line);
    std::string toString() const;
};

// LispLexer class that converts LISP source text into tokens
class LispLexer {
private:
    std::string source;
    std::vector<Token> tokens;
    
    int start = 0;      // Start position of the current lexeme
    int current = 0;    // Current position in the source
    int line = 1;       // Current line number
    
public:
    LispLexer(std::string source);
    std::vector<Token> scanTokens();
    
private:
    void scanToken();
    void comment();
    void string();
    void number();
    void symbol();
    
    char advance();
    void addToken(TokenType type);
    void addToken(TokenType type, const std::string& literal);
    
    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    bool isSpecialSymbolChar(char c) const;
};

#endif // LEXER_H
