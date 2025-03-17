#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include "lexer.h"
#include "simple_jit.h"
#include "parser.h"
#include "code_gen_visitor.h"

// Utility function to check if two strings are equal and report errors
void test_equal(const std::string& actual, const std::string& expected, const std::string& test_name) {
    if (actual != expected) {
        std::cerr << "FAILED: " << test_name << std::endl;
        std::cerr << "  Expected: " << expected << std::endl;
        std::cerr << "  Actual:   " << actual << std::endl;
        assert(false);
    } else {
        std::cout << "PASSED: " << test_name << std::endl;
    }
}

// Test the lexer functionality
void test_lexer() {
    std::cout << "\n=== LEXER TESTS ===\n" << std::endl;
    
    // Test 1: Basic tokens
    {
        std::string source = "(+ 1 2)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 6); // 5 tokens + EOF
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "+");
        assert(tokens[2].type == TokenType::NUMBER && tokens[2].lexeme == "1");
        assert(tokens[3].type == TokenType::NUMBER && tokens[3].lexeme == "2");
        assert(tokens[4].type == TokenType::RIGHT_PAREN);
        assert(tokens[5].type == TokenType::EOF_TOKEN);
        
        std::cout << "PASSED: Basic tokens test" << std::endl;
    }
    
    // Test 2: String literals
    {
        std::string source = "(display \"Hello, World!\")";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 5); // 4 tokens + EOF
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "display");
        assert(tokens[2].type == TokenType::STRING && tokens[2].lexeme == "Hello, World!");
        assert(tokens[3].type == TokenType::RIGHT_PAREN);
        
        std::cout << "PASSED: String literals test" << std::endl;
    }
    
    // Test 3: Comments
    {
        std::string source = "(define x 10) ; This is a comment";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 6); // 5 tokens + EOF
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "define");
        assert(tokens[2].type == TokenType::SYMBOL && tokens[2].lexeme == "x");
        assert(tokens[3].type == TokenType::NUMBER && tokens[3].lexeme == "10");
        assert(tokens[4].type == TokenType::RIGHT_PAREN);
        
        std::cout << "PASSED: Comments test" << std::endl;
    }
    
    // Test 4: Boolean values
    {
        std::string source = "(if #t 1 0)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 7); // 6 tokens + EOF
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "if");
        assert(tokens[2].type == TokenType::BOOLEAN && tokens[2].lexeme == "#t");
        assert(tokens[3].type == TokenType::NUMBER && tokens[3].lexeme == "1");
        assert(tokens[4].type == TokenType::NUMBER && tokens[4].lexeme == "0");
        assert(tokens[5].type == TokenType::RIGHT_PAREN);
        
        std::cout << "PASSED: Boolean values test" << std::endl;
    }
    
    // Test 5: Quoted expressions
    {
        std::string source = "'(1 2 3)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 7); // 6 tokens + EOF
        assert(tokens[0].type == TokenType::QUOTE);
        assert(tokens[1].type == TokenType::LEFT_PAREN);
        assert(tokens[2].type == TokenType::NUMBER && tokens[2].lexeme == "1");
        assert(tokens[3].type == TokenType::NUMBER && tokens[3].lexeme == "2");
        assert(tokens[4].type == TokenType::NUMBER && tokens[4].lexeme == "3");
        assert(tokens[5].type == TokenType::RIGHT_PAREN);
        
        std::cout << "PASSED: Quoted expressions test" << std::endl;
    }
    
    // Test 6: Negative numbers
    {
        std::string source = "(- -42 -3.14)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens.size() == 6); // 5 tokens + EOF
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "-");
        assert(tokens[2].type == TokenType::NUMBER && tokens[2].lexeme == "-42");
        assert(tokens[3].type == TokenType::NUMBER && tokens[3].lexeme == "-3.14");
        assert(tokens[4].type == TokenType::RIGHT_PAREN);
        
        std::cout << "PASSED: Negative numbers test" << std::endl;
    }
    
    // Test 7: Special symbol characters
    {
        std::string source = "(define <= (lambda (x y) (<= x y)))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        assert(tokens[0].type == TokenType::LEFT_PAREN);
        assert(tokens[1].type == TokenType::SYMBOL && tokens[1].lexeme == "define");
        assert(tokens[2].type == TokenType::SYMBOL && tokens[2].lexeme == "<=");
        
        std::cout << "PASSED: Special symbol characters test" << std::endl;
    }
}

// Test the parser functionality
void test_parser() {
    std::cout << "\n=== PARSER TESTS ===\n" << std::endl;
    
    // Test 1: Simple expression
    {
        std::string source = "(+ 1 2)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        std::string result = expressions[0]->toString();
        test_equal(result, "(+ 1 2)", "Simple expression parsing");
    }
    
    // Test 2: Nested expressions
    {
        std::string source = "(define (square x) (* x x))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        std::string result = expressions[0]->toString();
        test_equal(result, "(define (square x) (* x x))", "Nested expressions parsing");
    }
    
    // Test 3: Multiple expressions
    {
        std::string source = "(define x 10) (define y 20) (+ x y)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 3);
        test_equal(expressions[0]->toString(), "(define x 10)", "Multiple expressions - 1");
        test_equal(expressions[1]->toString(), "(define y 20)", "Multiple expressions - 2");
        test_equal(expressions[2]->toString(), "(+ x y)", "Multiple expressions - 3");
    }
    
    // Test 4: Quoted expressions
    {
        std::string source = "'(1 2 3) 'symbol";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 2);
        test_equal(expressions[0]->toString(), "'(1 2 3)", "Quoted list");
        test_equal(expressions[1]->toString(), "'symbol", "Quoted symbol");
    }
    
    // Test 5: Complex nested expressions
    {
        std::string source = R"(
            (define (factorial n)
                (if (= n 0)
                    1
                    (* n (factorial (- n 1)))))
        )";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        test_equal(expressions[0]->toString(), 
                  "(define (factorial n) (if (= n 0) 1 (* n (factorial (- n 1)))))", 
                  "Complex nested expressions");
    }
    
    // Test 6: String literals
    {
        std::string source = R"((display "Hello, World!"))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        test_equal(expressions[0]->toString(), "(display \"Hello, World!\")", "String literals parsing");
    }
    
    // Test 7: Boolean values
    {
        std::string source = "(if #t (display \"true\") (display \"false\"))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        test_equal(expressions[0]->toString(), 
                  "(if #t (display \"true\") (display \"false\"))", 
                  "Boolean values parsing");
    }
    
    // Test 8: Function definition
    {
        std::string source = "(defn square [x] (* x x))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        assert(expressions[0]->nodeType == ASTNode::NodeType::FUNCTION);
        assert(expressions[0]->functionName == "square");
        assert(expressions[0]->parameters.size() == 1);
        assert(expressions[0]->parameters[0] == "x");
        assert(expressions[0]->body.size() == 1);
        
        std::string result = expressions[0]->toString();
        test_equal(result, "(defn square [x] (* x x))", "Function definition parsing");
    }
    
    // Test 9: Function with multiple parameters and body expressions
    {
        std::string source = "(defn max [a b] (if (> a b) a b))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        assert(expressions[0]->nodeType == ASTNode::NodeType::FUNCTION);
        assert(expressions[0]->functionName == "max");
        assert(expressions[0]->parameters.size() == 2);
        assert(expressions[0]->parameters[0] == "a");
        assert(expressions[0]->parameters[1] == "b");
        assert(expressions[0]->body.size() == 1);

        // Check the body of the function (if expression)
        auto bodyNode = expressions[0]->body[0];
        assert(bodyNode->nodeType == ASTNode::NodeType::LIST);
        auto listNode = std::static_pointer_cast<ListNode>(bodyNode);
        
        // Check that it's an 'if' expression
        assert(listNode->children.size() == 4);
        assert(listNode->children[0]->nodeType == ASTNode::NodeType::ATOM);
        auto ifNode = std::static_pointer_cast<AtomNode>(listNode->children[0]);
        assert(ifNode->atomType == TokenType::SYMBOL);
        assert(std::get<std::string>(ifNode->value) == "if");
        
        // Check the condition (> a b)
        assert(listNode->children[1]->nodeType == ASTNode::NodeType::LIST);
        auto conditionNode = std::static_pointer_cast<ListNode>(listNode->children[1]);
        assert(conditionNode->children.size() == 3);
        assert(conditionNode->children[0]->nodeType == ASTNode::NodeType::ATOM);
        auto gtNode = std::static_pointer_cast<AtomNode>(conditionNode->children[0]);
        assert(gtNode->atomType == TokenType::SYMBOL);
        assert(std::get<std::string>(gtNode->value) == ">");
        
        std::string result = expressions[0]->toString();
        test_equal(result, "(defn max [a b] (if (> a b) a b))", "Function with multiple parameters");
    }
    
    // Test 10: Function with multiple body expressions
    {
        std::string source = "(defn print-and-square [x] (display x) (* x x))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        assert(expressions[0]->nodeType == ASTNode::NodeType::FUNCTION);
        assert(expressions[0]->functionName == "print-and-square");
        assert(expressions[0]->parameters.size() == 1);
        assert(expressions[0]->parameters[0] == "x");
        assert(expressions[0]->body.size() == 2);
        
        std::string result = expressions[0]->toString();
        test_equal(result, "(defn print-and-square [x] (display x) (* x x))", "Function with multiple body expressions");
    }
    
}

// Test the code generator functionality
void test_code_generator() {
    std::cout << "\n=== CODE GENERATOR TESTS ===\n" << std::endl;
    
    
    // Create a JIT compiler instance
    SimpleJIT jit;
    
    // Test 1: Simple arithmetic expression
    {
        std::string source = "(+ 1 2)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();

        assert(expressions.size() == 1);
        

        // Compile and run the expression directly
        auto resultValue = jit.compileAndRunExpression(expressions[0]);
        assert(resultValue.has_value());
        assert(resultValue.value() == 3);  // 1 + 2 = 3
        
        std::cout << "PASSED: Simple arithmetic expression code generation and execution" << std::endl;
    }
    
    // Test 2: Function definition and call
    {
        std::string source = "(defn square [x] (* x x)) (square 4)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 2);
        
        // Compile and run the program directly
        auto resultValue = jit.compileAndRunProgram(expressions);
        std::cout << "Result value: " << resultValue.value() << std::endl;
        assert(resultValue.has_value());
        assert(resultValue.value() == 16);  // square(4) = 16
        
        std::cout << "PASSED: Function definition and call code generation and execution" << std::endl;
    }
    
    // Test 3: If expression
    {
        std::string source = "(if (> 5 3) 1 0)";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        
        // Compile and run the expression directly
        auto resultValue = jit.compileAndRunExpression(expressions[0]);
        assert(resultValue.has_value());
        assert(resultValue.value() == 1);  // 5 > 3, so result is 1
        
        std::cout << "PASSED: If expression code generation and execution" << std::endl;
    }
    
    // Test 4: Let binding
    {
        std::string source = "(let ((x 10) (y 20)) (+ x y))";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 1);
        
        // Compile and run the expression directly
        auto resultValue = jit.compileAndRunExpression(expressions[0]);
        assert(resultValue.has_value());
        assert(resultValue.value() == 30);  // 10 + 20 = 30
        
        std::cout << "PASSED: Let binding code generation and execution" << std::endl;
    }
    
    // Test 5: Complex nested expression - factorial
    {
        std::string source = R"(
            (defn factorial [n]
                (if (= n 0)
                    1
                    (* n (factorial (- n 1)))))
            (factorial 5)
        )";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 2);
        
        // Compile and run the program directly
        auto resultValue = jit.compileAndRunProgram(expressions);
        assert(resultValue.has_value());
        assert(resultValue.value() == 120);  // factorial(5) = 120
        
        std::cout << "PASSED: Complex nested expression (factorial) code generation and execution" << std::endl;
    }
    
    // Test 6: Multiple function definitions and calls
    {
        std::string source = R"(
            (defn add [a b] (+ a b))
            (defn multiply [a b] (* a b))
            (multiply (add 3 4) 2)
        )";
        LispLexer lexer(source);
        std::vector<Token> tokens = lexer.scanTokens();
        
        LispParser parser(tokens);
        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
        
        assert(expressions.size() == 3);
        
        // Compile and run the program directly
        auto resultValue = jit.compileAndRunProgram(expressions);
        assert(resultValue.has_value());
        assert(resultValue.value() == 14);  // multiply(add(3, 4), 2) = multiply(7, 2) = 14
        
        std::cout << "PASSED: Multiple function definitions and calls code generation and execution" << std::endl;
    }
}

// Run all tests
int main() {
    std::cout << "Running LISP lexer and parser tests..." << std::endl;
    
    try {
        test_lexer();
        test_parser();
        test_code_generator();
        
        std::cout << "\nAll tests passed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
