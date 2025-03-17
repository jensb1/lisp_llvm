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
    
    // Create our JIT compiler
    SimpleJIT TheJIT;
    
    std::cout << "Lisp JIT REPL - Enter expressions or commands:" << std::endl;
    std::cout << "  'exit' to quit" << std::endl;
    std::cout << "  'eval <expr>' to evaluate an expression" << std::endl;
    std::cout << "  'def <name> <expr>' to define a named function" << std::endl;
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
                    // Check if this is a special command
                    std::istringstream iss(input);
                    std::string command;
                    iss >> command;
                    
                    if (command == "eval") {
                        // Extract the expression part (everything after "eval ")
                        std::string exprStr = input.substr(5);
                        
                        // Tokenize the expression
                        LispLexer lexer(exprStr);
                        std::vector<Token> tokens = lexer.scanTokens();
                        
                        // Parse the tokens
                        LispParser parser(tokens);
                        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
                        
                        if (!expressions.empty()) {
                            // Print the parsed expression
                            std::cout << "Parsed: " << expressions[0]->toString() << std::endl;
                            
                            // Generate a unique function name
                            static int evalCounter = 0;
                            std::string funcName = "eval_" + std::to_string(evalCounter++);
                            
                            // Add the AST as a function to the JIT
                            if (auto Err = TheJIT.addASTFunction(expressions[0], funcName)) {
                                llvm::errs() << "Error adding AST function: ";
                                llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "");
                                continue;
                            }
                            
                            // Look up and call the function
                            auto FuncOrErr = TheJIT.lookupASTFunction(funcName);
                            if (!FuncOrErr) {
                                llvm::errs() << "Failed to look up AST function: ";
                                llvm::logAllUnhandledErrors(FuncOrErr.takeError(), llvm::errs(), "");
                                continue;
                            }
                            auto Func = *FuncOrErr;
                            
                            // Call the function and print the result
                            int result = Func();
                            std::cout << "=> " << result << std::endl;
                        }
                    }
                    else if (command == "def") {
                        std::string funcName;
                        iss >> funcName;
                        
                        // Extract the expression part (everything after "def <name> ")
                        size_t pos = input.find(funcName) + funcName.length();
                        std::string exprStr = input.substr(pos);
                        
                        // Tokenize the expression
                        LispLexer lexer(exprStr);
                        std::vector<Token> tokens = lexer.scanTokens();
                        
                        // Parse the tokens
                        LispParser parser(tokens);
                        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
                        
                        if (!expressions.empty()) {
                            // Print the parsed expression
                            std::cout << "Defining function '" << funcName << "' as: " 
                                      << expressions[0]->toString() << std::endl;
                            
                            // Add the AST as a function to the JIT
                            if (auto Err = TheJIT.addASTFunction(expressions[0], funcName)) {
                                llvm::errs() << "Error defining function: ";
                                llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "");
                                continue;
                            }
                            
                            std::cout << "=> Function '" << funcName << "' defined successfully" << std::endl;
                        }
                    }
                    else {
                        // Regular expression parsing and display (no JIT compilation)
                        LispLexer lexer(input);
                        std::vector<Token> tokens = lexer.scanTokens();
                        
                        // Parse the tokens
                        LispParser parser(tokens);
                        std::vector<std::shared_ptr<ASTNode>> expressions = parser.parse();
                        
                        // Print the parsed expressions
                        for (const auto& expr : expressions) {
                            std::cout << "=> " << expr->toString() << std::endl;
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
    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    runRepl();
    return 0;
} 