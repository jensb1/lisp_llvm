#include "lexer.h"
#include <cctype>
#include <iostream>

// Token class constructor
Token::Token(TokenType type, std::string lexeme, int line)
    : type(type), lexeme(std::move(lexeme)), line(line) {}

std::string Token::toString() const {
    std::string typeStr;
    switch (type) {
        case TokenType::LEFT_PAREN:   typeStr = "LEFT_PAREN"; break;
        case TokenType::RIGHT_PAREN:  typeStr = "RIGHT_PAREN"; break;
        case TokenType::QUOTE:        typeStr = "QUOTE"; break;
        case TokenType::SYMBOL:       typeStr = "SYMBOL"; break;
        case TokenType::NUMBER:       typeStr = "NUMBER"; break;
        case TokenType::STRING:       typeStr = "STRING"; break;
        case TokenType::BOOLEAN:      typeStr = "BOOLEAN"; break;
        case TokenType::COMMENT:      typeStr = "COMMENT"; break;
        case TokenType::EOF_TOKEN:    typeStr = "EOF"; break;
    }
    
    return typeStr + " " + lexeme;
}

// LispLexer class constructor
LispLexer::LispLexer(std::string source) : source(std::move(source)) {}

std::vector<Token> LispLexer::scanTokens() {
    tokens.clear();
    start = 0;
    current = 0;
    line = 1;
    
    while (!isAtEnd()) {
        start = current;
        scanToken();
    }
    
    tokens.emplace_back(TokenType::EOF_TOKEN, "", line);
    return tokens;
}

void LispLexer::scanToken() {
    char c = advance();
    
    switch (c) {
        case '(': addToken(TokenType::LEFT_PAREN); break;
        case ')': addToken(TokenType::RIGHT_PAREN); break;
        case '[': addToken(TokenType::LEFT_BRACKET); break;
        case ']': addToken(TokenType::RIGHT_BRACKET); break;
        case '\'': addToken(TokenType::QUOTE); break;
        case ' ':
        case '\r':
        case '\t':
            break;
        case '\n':
            line++;
            break;
        case ';': 
            while (peek() != '\n' && !isAtEnd()) {
                advance();
            }
            break;
        case '"':
            string();
            break;
        default:
            if (isdigit(c) || (c == '-' && isdigit(peek()))) {
                number();
            } else if (isalpha(c) || isSpecialSymbolChar(c)) {
                symbol();
            } else {
                throw std::runtime_error("Unexpected character at line " + 
                                       std::to_string(line));
            }
            break;
    }
}

void LispLexer::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        advance();
    }
    
    if (isAtEnd()) {
        throw std::runtime_error("Unterminated string at line " + std::to_string(line));
    }
    
    advance();
    std::string value = source.substr(start + 1, current - start - 2);
    addToken(TokenType::STRING, value);
}

void LispLexer::number() {
    bool hasDecimalPoint = false;
    
    while (isdigit(peek())) {
        advance();
    }
    
    if (peek() == '.' && isdigit(peekNext())) {
        hasDecimalPoint = true;
        advance();
        
        while (isdigit(peek())) {
            advance();
        }
    }
    
    addToken(TokenType::NUMBER);
}

void LispLexer::symbol() {
    while (isalnum(peek()) || isSpecialSymbolChar(peek())) {
        advance();
    }
    
    std::string text = source.substr(start, current - start);
    
    if (text == "#t" || text == "#f") {
        addToken(TokenType::BOOLEAN);
    } else {
        addToken(TokenType::SYMBOL);
    }
}

char LispLexer::advance() {
    return source[current++];
}

void LispLexer::addToken(TokenType type) {
    addToken(type, "");
}

void LispLexer::addToken(TokenType type, const std::string& literal) {
    std::string text = literal.empty() ? 
        source.substr(start, current - start) : literal;
    tokens.emplace_back(type, text, line);
}

bool LispLexer::isAtEnd() const {
    return current >= source.length();
}

char LispLexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char LispLexer::peekNext() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

bool LispLexer::isSpecialSymbolChar(char c) const {
    return c == '+' || c == '-' || c == '*' || c == '/' || 
           c == '_' || c == '>' || c == '<' || c == '=' || 
           c == '!' || c == '?' || c == ':' || c == '#' || 
           c == '%' || c == '&' || c == '$' || c == '.';
}