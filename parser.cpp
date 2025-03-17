#include "parser.h"
#include <iostream>

// Expression class constructors
Expression::Expression(TokenType type, std::variant<double, std::string, bool> val)
    : type(ExprType::ATOM), atomType(type), value(std::move(val)) {}

Expression::Expression(std::vector<std::shared_ptr<Expression>> elems)
    : type(ExprType::LIST), elements(std::move(elems)) {}

Expression::Expression(std::shared_ptr<Expression> quoted)
    : type(ExprType::QUOTED) {
    elements.push_back(std::move(quoted));
}

std::string Expression::toString() const {
    switch (type) {
        case ExprType::ATOM:
            if (atomType == TokenType::NUMBER) {
                double num = std::get<double>(value);
                // Check if the number is an integer
                if (num == static_cast<int>(num)) {
                    return std::to_string(static_cast<int>(num));
                } else {
                    return std::to_string(num);
                }
            } else if (atomType == TokenType::STRING) {
                return "\"" + std::get<std::string>(value) + "\"";
            } else if (atomType == TokenType::BOOLEAN) {
                return std::get<bool>(value) ? "#t" : "#f";
            } else {
                return std::get<std::string>(value);
            }
        
        case ExprType::LIST: {
            std::string result = "(";
            for (size_t i = 0; i < elements.size(); i++) {
                if (i > 0) result += " ";
                result += elements[i]->toString();
            }
            result += ")";
            return result;
        }
        
        case ExprType::QUOTED:
            return "'" + elements[0]->toString();
            
        default:
            return "Unknown expression type";
    }
}

// LispParser class constructor
LispParser::LispParser(const std::vector<Token>& tokens) : tokens(tokens) {}

std::vector<std::shared_ptr<ASTNode>> LispParser::parse() {
    std::vector<std::shared_ptr<ASTNode>> expressions;
    
    while (!isAtEnd() && peek().type != TokenType::EOF_TOKEN) {
        try {
            expressions.push_back(expression());
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
            synchronize();
        }
    }
    
    return expressions;
}

std::shared_ptr<ASTNode> LispParser::expression() {
    if (match(TokenType::LEFT_PAREN)) {
        return list();
    } else if (match(TokenType::QUOTE)) {
        return quoted();
    } else {
        return atom();
    }
}

std::shared_ptr<ASTNode> LispParser::list() {
    // For function definitions
    if (check(TokenType::SYMBOL) && peek().lexeme == "defn") {
        return functionDefinition();
    }
    
    // For all other lists (including function calls)
    std::vector<std::shared_ptr<ASTNode>> elements;
    
    // If it starts with a symbol, it's likely a function call
    if (check(TokenType::SYMBOL)) {
        // Get the function name but don't consume it yet
        std::string funcName = peek().lexeme;
        
        // Create a node for the function name
        elements.push_back(atom());  // This will consume the symbol token
        
        // Parse arguments
        while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
            elements.push_back(expression());
        }
        
        consume(TokenType::RIGHT_PAREN, "Expected ')' after list.");
        
        // Create a list node with all elements (including function name)
        return std::make_shared<ListNode>(elements);
    }
    
    // For lists that don't start with a symbol
    while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
        elements.push_back(expression());
    }
    
    consume(TokenType::RIGHT_PAREN, "Expected ')' after list.");
    
    // Create a list node
    return std::make_shared<ListNode>(elements);
}

std::shared_ptr<ASTNode> LispParser::functionDefinition() {
    // Consume 'defn'
    advance();
    
    // Function name must be a symbol
    if (!check(TokenType::SYMBOL)) {
        throw std::runtime_error("Expected function name after 'defn' at line " + 
                                std::to_string(peek().line));
    }
    
    // Get function name
    Token nameToken = advance();
    std::string functionName = nameToken.lexeme;
    
    // Parameter list must be a vector (in square brackets)
    if (!match(TokenType::LEFT_BRACKET)) {
        throw std::runtime_error("Expected parameter list after function name at line " + 
                                std::to_string(peek().line));
    }
    
    // Parse parameter list
    std::vector<std::string> parameters;
    while (!check(TokenType::RIGHT_BRACKET) && !isAtEnd()) {
        if (!check(TokenType::SYMBOL)) {
            throw std::runtime_error("Expected parameter name at line " + 
                                    std::to_string(peek().line));
        }
        parameters.push_back(advance().lexeme);
    }
    
    consume(TokenType::RIGHT_BRACKET, "Expected ']' after parameter list.");
    
    // Parse function body (one or more expressions)
    std::vector<std::shared_ptr<ASTNode>> body;
    while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
        body.push_back(expression());
    }
    
    if (body.empty()) {
        throw std::runtime_error("Function body cannot be empty at line " + 
                                std::to_string(peek().line));
    }
    
    consume(TokenType::RIGHT_PAREN, "Expected ')' after function definition.");
    
    // Create a function definition node
    return std::make_shared<FunctionNode>(functionName, parameters, body);
}

std::shared_ptr<ASTNode> LispParser::quoted() {
    std::shared_ptr<ASTNode> expr = expression();
    return std::make_shared<QuotedNode>(expr);
}

std::shared_ptr<ASTNode> LispParser::atom() {
    if (match(TokenType::NUMBER)) {
        Token token = previous();
        std::string lexeme = token.lexeme;
        double value;
        if (lexeme.find('.') != std::string::npos) {
            value = std::stod(lexeme);
        } else {
            value = static_cast<double>(std::stoi(lexeme));
        }
        return std::make_shared<AtomNode>(TokenType::NUMBER, value);
    }
    
    if (match(TokenType::STRING)) {
        Token token = previous();
        return std::make_shared<AtomNode>(TokenType::STRING, token.lexeme);
    }
    
    if (match(TokenType::BOOLEAN)) {
        Token token = previous();
        bool value = (token.lexeme == "#t");
        return std::make_shared<AtomNode>(TokenType::BOOLEAN, value);
    }
    
    if (match(TokenType::SYMBOL)) {
        Token token = previous();
        return std::make_shared<AtomNode>(TokenType::SYMBOL, token.lexeme);
    }
    
    throw std::runtime_error("Expected expression at line " + 
                            std::to_string(peek().line));
}

bool LispParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token LispParser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    
    throw std::runtime_error(message + " at line " + 
                            std::to_string(peek().line));
}

bool LispParser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

Token LispParser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool LispParser::isAtEnd() const {
    return current >= tokens.size() || tokens[current].type == TokenType::EOF_TOKEN;
}

Token LispParser::peek() const {
    return tokens[current];
}

Token LispParser::previous() const {
    return tokens[current - 1];
}

void LispParser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        if (previous().type == TokenType::RIGHT_PAREN) return;
        
        switch (peek().type) {
            case TokenType::LEFT_PAREN:
                return;
            default:
                break;
        }
        
        advance();
    }
}