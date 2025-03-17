#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <variant>
#include <unordered_map>
#include "lexer.h"
#include "ast_node.h"

// Forward declarations
class Expression;
class LispParser;

// Different types of LISP expressions
enum class ExprType {
    ATOM,       // Numbers, strings, booleans, symbols
    LIST,       // Lists (expressions in parentheses)
    QUOTED      // Quoted expressions
};

// Class to represent any LISP expression
class Expression {
public:
    ExprType type;
    
    std::variant<double, std::string, bool> value;
    TokenType atomType;
    
    std::vector<std::shared_ptr<Expression>> elements;
    
    Expression(TokenType type, std::variant<double, std::string, bool> val);
    Expression(std::vector<std::shared_ptr<Expression>> elems);
    Expression(std::shared_ptr<Expression> quoted);
    
    std::string toString() const;
};

// Parser class for LISP
class LispParser {
private:
    std::vector<Token> tokens;
    size_t current = 0;
    
public:
    LispParser(const std::vector<Token>& tokens);
    std::vector<std::shared_ptr<ASTNode>> parse();
    
private:
    std::shared_ptr<ASTNode> expression();
    std::shared_ptr<ASTNode> list();
    std::shared_ptr<ASTNode> functionDefinition();
    std::shared_ptr<ASTNode> quoted();
    std::shared_ptr<ASTNode> atom();
    
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& message);
    bool check(TokenType type) const;
    Token advance();
    bool isAtEnd() const;
    Token peek() const;
    Token previous() const;
    void synchronize();
};

#endif // PARSER_H 