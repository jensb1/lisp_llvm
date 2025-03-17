#ifndef SIMPLE_JIT_H
#define SIMPLE_JIT_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "ast_node.h"

// Include LLVM headers instead of forward declarations
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>

// A simple JIT compiler class with optimized dependency tracking
class SimpleJIT {
private:
    std::unique_ptr<llvm::orc::ExecutionSession> ES;
    llvm::orc::RTDyldObjectLinkingLayer ObjectLayer;
    llvm::orc::IRCompileLayer CompileLayer;
    llvm::DataLayout DL;
    llvm::orc::MangleAndInterner Mangle;
    llvm::orc::ThreadSafeContext Ctx;
    llvm::orc::JITDylib &MainJD; // Reference to our main JITDylib
    
    // Maps functions to their resource trackers
    std::unordered_map<std::string, llvm::orc::ResourceTrackerSP> FunctionTrackers;
    
    // Maps functions to their dependencies (functions that call them)
    std::unordered_map<std::string, std::unordered_set<std::string>> FunctionDependents;
    
    // Maps functions to the functions they call (dependencies)
    std::unordered_map<std::string, std::unordered_set<std::string>> FunctionDependencies;

    // Helper functions
    static std::unique_ptr<llvm::orc::ExecutionSession> createExecutionSession();
    static llvm::orc::JITTargetMachineBuilder getTargetMachineBuilder();
    static llvm::DataLayout getDataLayout();
    void reportError(llvm::Error Err);
    
    // Module creation helpers
    std::unique_ptr<llvm::Module> createGetFirstValueModule(int returnValue);
    std::unique_ptr<llvm::Module> createAddModule();
    std::unique_ptr<llvm::Module> createModuleFromAST(const std::shared_ptr<ASTNode>& ast, const std::string& moduleName);
    llvm::Value* generateCodeFromAST(const std::shared_ptr<ASTNode>& node, llvm::IRBuilder<>& Builder);
    
    // Helper function to generate code from an AST node with variable scope
    llvm::Value* generateCodeFromASTWithScope(
        const std::shared_ptr<ASTNode>& node, 
        llvm::IRBuilder<>& Builder,
        std::unordered_map<std::string, llvm::Value*>& scope);

public:
    SimpleJIT();
    
    // Add a function module and track its dependencies
    llvm::Error addFunctionModule(std::unique_ptr<llvm::Module> M, const std::string &FunctionName, 
                                const std::unordered_set<std::string> &Dependencies = {});
    
    // Initialize with getFirstValue and add function
    llvm::Error initialize(int firstValue);
    
    // Replace a function and all functions that depend on it
    llvm::Error replaceFunction(const std::string &FunctionName, std::unique_ptr<llvm::Module> NewM);
    
    // Replace getFirstValue to return a new value
    llvm::Error replaceGetFirstValue(int newValue);
    
    // Look up JIT'd functions
    llvm::Expected<int (*)(int)> lookupAdd();
    llvm::Expected<int (*)()> lookupGetFirstValue();
    
    // AST-related functions
    llvm::Error addASTFunction(const std::shared_ptr<ASTNode>& ast, const std::string& functionName);
    llvm::Expected<int (*)()> lookupASTFunction(const std::string& functionName);

    // Compile and run a single expression
    std::optional<int> compileAndRunExpression(const std::shared_ptr<ASTNode>& expr);

    // Compile and run a program (multiple expressions)
    std::optional<int> compileAndRunProgram(const std::vector<std::shared_ptr<ASTNode>>& expressions);
};

#endif // SIMPLE_JIT_H