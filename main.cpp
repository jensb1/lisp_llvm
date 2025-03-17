#include "lexer.h"
#include "parser.h"
#include "simple_jit.h"  // Include the SimpleJIT implementation
#include <iostream>
#include <string>
#include <sstream>
#include <memory>

// Add these LLVM includes
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Error.h>

void runRepl() {
    std::string line;
    std::string input;
    bool multilineInput = false;
    int parenBalance = 0;
    
    SimpleJIT TheJIT;
    
    std::cout << "Lisp JIT REPL - Enter expressions:" << std::endl;
    std::cout << "  'exit' to quit" << std::endl;
    std::cout << " try: (defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1))))) (factorial 5)" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    
    while (true) {
        // Display appropriate prompt
        if (!multilineInput) {
            std::cout << "> ";
        } else {
            std::cout << "... ";
        }
        
        // Read a line of input
        std::getline(std::cin, line);
        
        // Check for exit command
        if (line == "exit" && !multilineInput) {
            break;
        }
        
        // Add the line to our input buffer
        input += line + "\n";
        
        // Count parentheses to determine if we have a complete expression
        for (char c : line) {
            if (c == '(') parenBalance++;
            if (c == ')') parenBalance--;
        }
        
        // If we have balanced parentheses, process the input
        if (parenBalance <= 0 || line.empty()) {
            if (!input.empty()) {
                try {
                    
                    LispLexer lexer(input);
                    std::vector<Token> tokens = lexer.scanTokens();
                    
                    // Parse the tokens
                    LispParser parser(tokens);
                    std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
                    
                    if (!expressions.empty()) {
                        // Print the parsed expressions
                        std::cout << "Parsed: ";
                        for (const auto& expr : expressions) {
                            std::cout << expr->toString() << " ";
                        }
                        std::cout << std::endl;
                        
                        // Compile and run the expressions directly using SimpleJIT
                        auto resultValue = TheJIT.compileAndRunProgram(expressions);
                        
                        if (resultValue.has_value()) {
                            std::cout << "=> " << resultValue.value() << std::endl;
                        } else {
                            std::cerr << "Error: Failed to evaluate expression" << std::endl;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
                
                // Reset for next input
                input.clear();
                parenBalance = 0;
                multilineInput = false;
            }
        } else {
            // We're in the middle of an expression
            multilineInput = true;
        }
    }
    
    std::cout << "Goodbye!" << std::endl;
}

int main() {
    
    runRepl();
    return 0;
} 