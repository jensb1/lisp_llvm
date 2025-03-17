#ifndef CODE_GEN_VISITOR_H
#define CODE_GEN_VISITOR_H

#include "ast_node.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"
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
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include <stack>


// Forward declaration
class CodeGenVisitor;
class CodeGenContext;

// CodeGenContext class to maintain state during code generation
class CodeGenContext {
private:
    llvm::LLVMContext& context;  // Reference instead of unique_ptr
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    
    // Symbol table for variables and functions
    std::stack<std::unordered_map<std::string, llvm::Value*>> symbolTables;
    
    // Function table
    std::unordered_map<std::string, llvm::Function*> functionTable;
    
    // Current function being generated
    llvm::Function* currentFunction;
    
    // Error reporting
    std::vector<std::string> errors;
    
    bool ownsModule = true;  // Add this flag
    
public:
    // Modified constructor to take an external context
    CodeGenContext(llvm::LLVMContext& ctx, const std::string& moduleName);
    CodeGenContext(llvm::LLVMContext& ctx, llvm::Module* existingModule);  // Add this constructor
    ~CodeGenContext();
    
    // Context management
    llvm::LLVMContext& getContext();
    llvm::Module& getModule();
    llvm::IRBuilder<>& getBuilder();
    
    // Symbol table management
    void pushScope();
    void popScope();
    bool setSymbol(const std::string& name, llvm::Value* value);
    llvm::Value* getSymbol(const std::string& name);
    
    // Function management
    void setCurrentFunction(llvm::Function* function);
    llvm::Function* getCurrentFunction();
    bool addFunction(const std::string& name, llvm::Function* function);
    llvm::Function* getFunction(const std::string& name);
    
    // Type management
    llvm::Type* getIntType();
    llvm::Type* getDoubleType();
    llvm::Type* getBoolType();
    llvm::Type* getVoidType();
    llvm::Type* getStringType();
    
    // Error handling
    void addError(const std::string& error);
    bool hasErrors() const;
    std::vector<std::string> getErrors() const;
    
    // Module operations
    std::unique_ptr<llvm::Module> releaseModule();
    void verifyModule();
};

// Define the CodeGenVisitor base class that was only forward declared
class CodeGenVisitor {
public:
    virtual ~CodeGenVisitor() = default;
    
    // Change return type to llvm::Value* and parameter to shared_ptr
    virtual llvm::Value* visit(std::shared_ptr<ASTNode> node) = 0;
    virtual llvm::Value* visitAtom(std::shared_ptr<AtomNode> node) = 0;
    virtual llvm::Value* visitList(std::shared_ptr<ListNode> node) = 0;
    virtual llvm::Value* visitQuoted(std::shared_ptr<QuotedNode> node) = 0;
    virtual llvm::Value* visitFunction(std::shared_ptr<FunctionNode> node) = 0;
    
    // Special form handlers
    virtual llvm::Value* visitIf(std::shared_ptr<ListNode> node) = 0;
    virtual llvm::Value* visitLet(std::shared_ptr<ListNode> node) = 0;
    virtual llvm::Value* visitLambda(std::shared_ptr<ListNode> node) = 0;
    virtual llvm::Value* visitDefine(std::shared_ptr<ListNode> node) = 0;
};

// LLVMCodeGenVisitor implementation of CodeGenVisitor
class LLVMCodeGenVisitor : public CodeGenVisitor {
private:
    CodeGenContext& context;
    
    // Special form handlers
    std::unordered_map<std::string, llvm::Value* (LLVMCodeGenVisitor::*)(std::shared_ptr<ListNode>)> specialForms;
    
    // Helper methods
    llvm::Value* generateFunctionCall(const std::string& name, const std::vector<llvm::Value*>& args);
    llvm::Value* generateBinaryOp(const std::string& op, llvm::Value* lhs, llvm::Value* rhs);
    llvm::Value* generateUnaryOp(const std::string& op, llvm::Value* operand);
    
public:
    LLVMCodeGenVisitor(CodeGenContext& ctx);
    
    // Visitor implementation
    llvm::Value* visit(std::shared_ptr<ASTNode> node) override;
    llvm::Value* visitAtom(std::shared_ptr<AtomNode> node) override;
    llvm::Value* visitList(std::shared_ptr<ListNode> node) override;
    llvm::Value* visitQuoted(std::shared_ptr<QuotedNode> node) override;
    llvm::Value* visitFunction(std::shared_ptr<FunctionNode> node) override;
    
    // Special form handlers
    llvm::Value* visitIf(std::shared_ptr<ListNode> node) override;
    llvm::Value* visitLet(std::shared_ptr<ListNode> node) override;
    llvm::Value* visitLambda(std::shared_ptr<ListNode> node) override;
    llvm::Value* visitDefine(std::shared_ptr<ListNode> node) override;
    
    // Generate a complete module from an AST
    std::unique_ptr<llvm::Module> generateModule(std::shared_ptr<ASTNode> ast, const std::string& name);
};

#endif // CODE_GEN_VISITOR_H
