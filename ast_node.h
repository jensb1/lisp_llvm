#ifndef AST_NODE_H
#define AST_NODE_H

#include <memory>
#include <vector>
#include <variant>
#include <string>
#include "lexer.h"
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

// Forward declaration of Expression class
class Expression;

// Forward declaration of CodeGenVisitor class
class CodeGenVisitor;

// ASTNode class to represent nodes in an Abstract Syntax Tree
class ASTNode : public std::enable_shared_from_this<ASTNode> {
public:
    // Enum to represent the type of AST node
    enum class NodeType {
        ATOM,
        LIST,
        QUOTED,
        FUNCTION
    };

    NodeType nodeType;
    std::variant<double, std::string, bool> value;
    TokenType atomType;
    std::vector<std::shared_ptr<ASTNode>> children;
    
    // Function-specific fields
    std::string functionName;
    std::vector<std::string> parameters;
    std::vector<std::shared_ptr<ASTNode>> body;

    // Constructors for different types of nodes
    ASTNode(TokenType type, std::variant<double, std::string, bool> val);
    ASTNode(std::vector<std::shared_ptr<ASTNode>> elems);
    ASTNode(std::shared_ptr<ASTNode> quoted);
    
    // Constructor for function definitions
    ASTNode(std::string name, std::vector<std::string> params, std::vector<std::shared_ptr<ASTNode>> functionBody);

    // Constructor for function call nodes
    ASTNode(std::string funcName, std::vector<std::shared_ptr<ASTNode>> args);

    // Method to convert the AST node to a string representation
    std::string toString() const;

    // Accept method for the visitor pattern - now virtual, not pure virtual
    virtual llvm::Value* accept(CodeGenVisitor& visitor);
    
    // Virtual destructor for proper cleanup
    virtual ~ASTNode() = default;
};

// Concrete node classes - REMOVE the std::enable_shared_from_this inheritance
class AtomNode : public ASTNode {
public:
    AtomNode(TokenType type, std::variant<double, std::string, bool> val);
    llvm::Value* accept(CodeGenVisitor& visitor) override;
};

class ListNode : public ASTNode {
public:
    ListNode(std::vector<std::shared_ptr<ASTNode>> elems);
    llvm::Value* accept(CodeGenVisitor& visitor) override;
};

class QuotedNode : public ASTNode {
public:
    QuotedNode(std::shared_ptr<ASTNode> quoted);
    llvm::Value* accept(CodeGenVisitor& visitor) override;
};

class FunctionNode : public ASTNode {
public:
    FunctionNode(std::string name, std::vector<std::string> params, std::vector<std::shared_ptr<ASTNode>> functionBody);
    FunctionNode(std::string funcName, std::vector<std::shared_ptr<ASTNode>> args);
    llvm::Value* accept(CodeGenVisitor& visitor) override;
};

#endif // AST_NODE_H 