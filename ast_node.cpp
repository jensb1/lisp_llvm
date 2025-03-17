#include "ast_node.h"
#include "code_gen_visitor.h"
#include <iostream>

// Base ASTNode constructors
ASTNode::ASTNode(TokenType type, std::variant<double, std::string, bool> val) 
    : nodeType(NodeType::ATOM), atomType(type), value(val) {}

ASTNode::ASTNode(std::vector<std::shared_ptr<ASTNode>> elems) 
    : nodeType(NodeType::LIST), children(elems) {}

ASTNode::ASTNode(std::shared_ptr<ASTNode> quoted) 
    : nodeType(NodeType::QUOTED) {
    children.push_back(quoted);
}

ASTNode::ASTNode(std::string name, std::vector<std::string> params, std::vector<std::shared_ptr<ASTNode>> functionBody) 
    : nodeType(NodeType::FUNCTION), functionName(name), parameters(params), body(functionBody) {}

ASTNode::ASTNode(std::string funcName, std::vector<std::shared_ptr<ASTNode>> args) 
    : nodeType(NodeType::LIST), functionName(funcName), children(args) {}

// Default implementation for the base class
llvm::Value* ASTNode::accept(CodeGenVisitor& visitor) {
    // This should never be called directly
    return nullptr;
}

// AtomNode implementation
AtomNode::AtomNode(TokenType type, std::variant<double, std::string, bool> val)
    : ASTNode(type, val) {}

llvm::Value* AtomNode::accept(CodeGenVisitor& visitor) {
    std::cout << "DEBUG: Visiting atom" << std::endl;
    return visitor.visitAtom(std::dynamic_pointer_cast<AtomNode>(shared_from_this()));
}

// ListNode implementation
ListNode::ListNode(std::vector<std::shared_ptr<ASTNode>> elems)
    : ASTNode(elems) {}

llvm::Value* ListNode::accept(CodeGenVisitor& visitor) {
  std::cout << "DEBUG: Visiting list" << std::endl;
    return visitor.visitList(std::dynamic_pointer_cast<ListNode>(shared_from_this()));
}

// QuotedNode implementation
QuotedNode::QuotedNode(std::shared_ptr<ASTNode> quoted)
    : ASTNode(quoted) {}

llvm::Value* QuotedNode::accept(CodeGenVisitor& visitor) {
  std::cout << "DEBUG: Visiting quoted" << std::endl;
    return visitor.visitQuoted(std::dynamic_pointer_cast<QuotedNode>(shared_from_this()));
}

// FunctionNode implementation
FunctionNode::FunctionNode(std::string name, std::vector<std::string> params, std::vector<std::shared_ptr<ASTNode>> functionBody)
    : ASTNode(name, params, functionBody) {}

FunctionNode::FunctionNode(std::string funcName, std::vector<std::shared_ptr<ASTNode>> args)
    : ASTNode(funcName, args) {}

llvm::Value* FunctionNode::accept(CodeGenVisitor& visitor) {
  std::cout << "DEBUG: Visiting function" << std::endl;
    return visitor.visitFunction(std::dynamic_pointer_cast<FunctionNode>(shared_from_this()));
}

// Method to convert the AST node to a string representation
std::string ASTNode::toString() const {
    switch (nodeType) {
        case NodeType::ATOM:
            if (atomType == TokenType::NUMBER) {
                double num = std::get<double>(value);
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
        
        case NodeType::LIST: {
            std::string result = "(";
            for (size_t i = 0; i < children.size(); i++) {
                if (i > 0) result += " ";
                result += children[i]->toString();
            }
            result += ")";
            return result;
        }
        
        case NodeType::QUOTED:
            return "'" + children[0]->toString();
        
        case NodeType::FUNCTION: {
            std::string result = "(defn " + functionName + " [";
            for (size_t i = 0; i < parameters.size(); i++) {
                if (i > 0) result += " ";
                result += parameters[i];
            }
            result += "]";
            
            for (const auto& expr : body) {
                result += " " + expr->toString();
            }
            
            result += ")";
            return result;
        }
            
        default:
            return "Unknown node type";
    }
}
