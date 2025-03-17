#include "code_gen_visitor.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include <iostream>

// CodeGenContext implementation
CodeGenContext::CodeGenContext(llvm::LLVMContext& ctx, const std::string& moduleName)
    : context(ctx) {
    module = std::make_unique<llvm::Module>(moduleName, context);
    builder = std::make_unique<llvm::IRBuilder<>>(context);
    
    // Initialize with global scope
    pushScope();
    currentFunction = nullptr;
}

// Add a new constructor that takes an existing module
CodeGenContext::CodeGenContext(llvm::LLVMContext& ctx, llvm::Module* existingModule)
    : context(ctx) {
    // Use the existing module instead of creating a new one
    if (existingModule) {
        module = std::unique_ptr<llvm::Module>(existingModule);
        ownsModule = false;  // Mark that we don't own this module
    } else {
        module = std::make_unique<llvm::Module>("default_module", context);
        ownsModule = true;
    }
    
    builder = std::make_unique<llvm::IRBuilder<>>(context);
    
    // Initialize with global scope
    pushScope();
    currentFunction = nullptr;
}

CodeGenContext::~CodeGenContext() {
    // If we don't own the module, set it to nullptr before the unique_ptr destructor runs
    if (!ownsModule && module) {
        module.release();  // Release ownership without deleting
    }
}

llvm::LLVMContext& CodeGenContext::getContext() {
    return context;
}

llvm::Module& CodeGenContext::getModule() {
    return *module;
}

llvm::IRBuilder<>& CodeGenContext::getBuilder() {
    return *builder;
}

void CodeGenContext::pushScope() {
    symbolTables.push(std::unordered_map<std::string, llvm::Value*>());
}

void CodeGenContext::popScope() {
    if (!symbolTables.empty()) {
        symbolTables.pop();
    }
}

bool CodeGenContext::setSymbol(const std::string& name, llvm::Value* value) {
    if (symbolTables.empty()) return false;
    symbolTables.top()[name] = value;
    return true;
}

llvm::Value* CodeGenContext::getSymbol(const std::string& name) {
    // Search from innermost to outermost scope
    auto tempStack = symbolTables;
    while (!tempStack.empty()) {
        auto& scope = tempStack.top();
        auto it = scope.find(name);
        if (it != scope.end()) {
            return it->second;
        }
        tempStack.pop();
    }
    return nullptr;
}

void CodeGenContext::setCurrentFunction(llvm::Function* function) {
    currentFunction = function;
}

llvm::Function* CodeGenContext::getCurrentFunction() {
    return currentFunction;
}

bool CodeGenContext::addFunction(const std::string& name, llvm::Function* function) {
    if (functionTable.find(name) != functionTable.end()) {
        return false;
    }
    functionTable[name] = function;
    return true;
}

llvm::Function* CodeGenContext::getFunction(const std::string& name) {
    auto it = functionTable.find(name);
    if (it != functionTable.end()) {
        return it->second;
    }
    return nullptr;
}

llvm::Type* CodeGenContext::getIntType() {
    return llvm::Type::getInt32Ty(context);
}

llvm::Type* CodeGenContext::getDoubleType() {
    return llvm::Type::getDoubleTy(context);
}

llvm::Type* CodeGenContext::getBoolType() {
    return llvm::Type::getInt1Ty(context);
}

llvm::Type* CodeGenContext::getVoidType() {
    return llvm::Type::getVoidTy(context);
}

llvm::Type* CodeGenContext::getStringType() {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
}

void CodeGenContext::addError(const std::string& error) {
    errors.push_back(error);
    std::cerr << "CodeGen Error: " << error << std::endl;
}

bool CodeGenContext::hasErrors() const {
    return !errors.empty();
}

std::vector<std::string> CodeGenContext::getErrors() const {
    return errors;
}

std::unique_ptr<llvm::Module> CodeGenContext::releaseModule() {
    if (ownsModule) {
        ownsModule = false;  // We're giving up ownership
        return std::move(module);
    } else {
        // If we don't own the module, return an empty unique_ptr
        // to indicate the caller shouldn't take ownership
        return nullptr;
    }
}

void CodeGenContext::verifyModule() {
    std::string errorInfo;
    llvm::raw_string_ostream errorStream(errorInfo);
    
    if (llvm::verifyModule(*module, &errorStream)) {
        addError("Module verification failed: " + errorInfo);
    }
}

// LLVMCodeGenVisitor implementation
LLVMCodeGenVisitor::LLVMCodeGenVisitor(CodeGenContext& ctx) : context(ctx) {
    // Register special form handlers
    specialForms["if"] = &LLVMCodeGenVisitor::visitIf;
    specialForms["let"] = &LLVMCodeGenVisitor::visitLet;
    specialForms["lambda"] = &LLVMCodeGenVisitor::visitLambda;
    specialForms["define"] = &LLVMCodeGenVisitor::visitDefine;
}

llvm::Value* LLVMCodeGenVisitor::visit(std::shared_ptr<ASTNode> node) {
    if (!node) {
        context.addError("Null node encountered during code generation");
        return nullptr;
    }
    
    std::cout << "DEBUG: Visiting node" << std::endl;
    
    switch (node->nodeType) {
        case ASTNode::NodeType::ATOM:
            std::cout << "DEBUG: Node type: ATOM" << std::endl;
            return visitAtom(std::static_pointer_cast<AtomNode>(node));
        case ASTNode::NodeType::LIST:
            std::cout << "DEBUG: Node type: LIST" << std::endl;
            return visitList(std::static_pointer_cast<ListNode>(node));
        case ASTNode::NodeType::QUOTED:
            std::cout << "DEBUG: Node type: QUOTED" << std::endl;
            return visitQuoted(std::static_pointer_cast<QuotedNode>(node));
        case ASTNode::NodeType::FUNCTION:
            std::cout << "DEBUG: Node type: FUNCTION" << std::endl;
            return visitFunction(std::static_pointer_cast<FunctionNode>(node));
        default:
            context.addError("Unknown node type in code generation");
            return nullptr;
    }
}

llvm::Value* LLVMCodeGenVisitor::visitAtom(std::shared_ptr<AtomNode> node) {
    std::cout << "DEBUG: visitAtom: Atom type: " << (int)node->atomType << std::endl;
    if (!node) {
        context.addError("Null atom node encountered");
        return nullptr;
    }
    
    switch (node->atomType) {
        case TokenType::NUMBER: {
            std::cout << "DEBUG: visitAtom: Number: " << std::get<double>(node->value) << std::endl;
            double numValue = std::get<double>(node->value);
            // Check if it's an integer or floating point
            if (numValue == static_cast<int>(numValue)) {
                std::cout << "DEBUG: visitAtom: Number is an integer" << std::endl;
                return llvm::ConstantInt::get(context.getIntType(), static_cast<int>(numValue));
            } else {
                std::cout << "DEBUG: visitAtom: Number is a floating point" << std::endl;
                return llvm::ConstantFP::get(context.getDoubleType(), numValue);
            }
        }
        
        case TokenType::BOOLEAN: {
            bool boolValue = std::get<bool>(node->value);
            std::cout << "DEBUG: visitAtom: Boolean value: " << boolValue << std::endl;
            return llvm::ConstantInt::get(context.getBoolType(), boolValue ? 1 : 0);
        }
        
        case TokenType::SYMBOL: {
            std::string symbolName = std::get<std::string>(node->value);
            std::cout << "DEBUG: visitAtom: Symbol name: " << symbolName << std::endl;
            llvm::Value* value = context.getSymbol(symbolName);
            if (!value) {
                context.addError("Undefined symbol: " + symbolName);
                return nullptr;
            }
            return value;
        }
        
        case TokenType::STRING: {
            std::string strValue = std::get<std::string>(node->value);
            std::cout << "DEBUG: visitAtom: String value: " << strValue << std::endl;
            llvm::IRBuilder<>& builder = context.getBuilder();
            return builder.CreateGlobalStringPtr(strValue, "str");
        }
        
        default:
            context.addError("Unsupported atom type in code generation");
            return nullptr;
    }
}

llvm::Value* LLVMCodeGenVisitor::visitList(std::shared_ptr<ListNode> node) {
    std::cout << "DEBUG: Visiting list" << std::endl;
    if (node->children.empty()) {
        std::cout << "DEBUG: visitList: Empty list" << std::endl;
        // Empty list evaluates to null/void
        return llvm::UndefValue::get(context.getVoidType());
    }
    
    // Check if this is a special form
    if (!node->children.empty() && 
        node->children[0]->nodeType == ASTNode::NodeType::ATOM) {
        
        std::cout << "DEBUG: visitList: Checking if first child is an atom" << std::endl;
        auto firstChild = std::static_pointer_cast<AtomNode>(node->children[0]);
        if (firstChild->atomType == TokenType::SYMBOL) {
            std::string formName = std::get<std::string>(firstChild->value);
            std::cout << "DEBUG: visitList: Form name: " << formName << std::endl;
            auto it = specialForms.find(formName);
            if (it != specialForms.end()) {
                std::cout << "DEBUG: visitList: Calling special form: " << formName << std::endl;
                llvm::Value* result = (this->*(it->second))(node);
                std::cout << "DEBUG: visitList done with special form" << std::endl;
                return result;
            }
        }
    }

    
    // This is a function call
    // Get the function name (first element of the list)
    auto firstChild = node->children[0];
    if (firstChild->nodeType != ASTNode::NodeType::ATOM) {
        context.addError("First element of list must be a symbol");
        return nullptr;
    }
    
    auto atomNode = std::static_pointer_cast<AtomNode>(firstChild);
    if (atomNode->atomType != TokenType::SYMBOL) {
        context.addError("First element of list must be a symbol");
        return nullptr;
    }
    
    std::string funcName = std::get<std::string>(atomNode->value);
    std::cout << "DEBUG: visitList: Function name: " << funcName << std::endl;
    
    // Evaluate all arguments
    std::vector<llvm::Value*> args;
    for (size_t i = 1; i < node->children.size(); ++i) {
        llvm::Value* argValue = visit(node->children[i]);
        if (!argValue) {
            return nullptr; // Propagate error
        }
        args.push_back(argValue);
    }
    
    return generateFunctionCall(funcName, args);
}

llvm::Value* LLVMCodeGenVisitor::visitQuoted(std::shared_ptr<QuotedNode> node) {
    // For quoted expressions, we need to create a runtime representation
    // This is complex and depends on the language's runtime system
    context.addError("Quoted expressions not fully implemented in code generation");
    return nullptr;
}

llvm::Value* LLVMCodeGenVisitor::visitFunction(std::shared_ptr<FunctionNode> node) {
    std::cout << "DEBUG: visitFunction: Function name: " << node->functionName << std::endl;
    
    // Get the return type (default to int for now)
    llvm::Type* returnType = context.getIntType();
    
    // Create parameter types (all int for now)
    std::vector<llvm::Type*> paramTypes(node->parameters.size(), context.getIntType());
    
    // Create function type
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    
    // Create function - use ExternalLinkage to ensure it's visible outside the module
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->functionName, context.getModule());
    
    // Add the function to the function table
    context.addFunction(node->functionName, function);
    std::cout << "DEBUG: Added function to function table: " << node->functionName << std::endl;
    
    // Set parameter names
    unsigned idx = 0;
    for (auto& arg : function->args()) {
        if (idx < node->parameters.size()) {
            arg.setName(node->parameters[idx]);
            idx++;
        }
    }
    
    // Create a new basic block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context.getContext(), "entry", function);
    context.getBuilder().SetInsertPoint(bb);
    
    // Create a new scope for function parameters
    context.pushScope();
    
    // Add parameters to symbol table
    idx = 0;
    for (auto& arg : function->args()) {
        context.setSymbol(arg.getName().str(), &arg);
        idx++;
    }
    
    // Set current function
    llvm::Function* prevFunction = context.getCurrentFunction();
    context.setCurrentFunction(function);
    
    // Generate code for function body
    llvm::Value* lastValue = nullptr;
    for (const auto& expr : node->body) {
        lastValue = visit(expr);
        if (!lastValue) {
            context.addError("Error in function body");
            context.popScope();
            context.setCurrentFunction(prevFunction);
            return nullptr;
        }
    }
    
    // Create return instruction
    if (lastValue) {
        // Ensure the return value is of the correct type
        if (lastValue->getType() != returnType) {
            if (returnType->isIntegerTy() && lastValue->getType()->isIntegerTy()) {
                lastValue = context.getBuilder().CreateIntCast(
                    lastValue, returnType, true, "retcast");
            } else if (returnType->isIntegerTy() && lastValue->getType()->isFloatingPointTy()) {
                lastValue = context.getBuilder().CreateFPToSI(
                    lastValue, returnType, "fptoi");
            } else if (returnType->isFloatingPointTy() && lastValue->getType()->isIntegerTy()) {
                lastValue = context.getBuilder().CreateSIToFP(
                    lastValue, returnType, "itofp");
            } else {
                context.addError("Cannot convert return value to function return type");
                context.popScope();
                context.setCurrentFunction(prevFunction);
                return nullptr;
            }
        }
        
        context.getBuilder().CreateRet(lastValue);
    } else {
        // No return value, return 0
        context.getBuilder().CreateRet(llvm::ConstantInt::get(returnType, 0));
    }
    
    // Restore previous scope and function
    context.popScope();
    context.setCurrentFunction(prevFunction);
    
    // Verify the function
    std::string errorInfo;
    llvm::raw_string_ostream errorStream(errorInfo);
    
    if (llvm::verifyFunction(*function, &errorStream)) {
        context.addError("Function verification failed: " + errorInfo);
        function->eraseFromParent();
        return nullptr;
    }
    
    std::cout << "DEBUG: Function verified successfully: " << node->functionName << std::endl;
    
    // Return the function as a value
    return function;
}

llvm::Value* LLVMCodeGenVisitor::visitIf(std::shared_ptr<ListNode> node) {
    std::cout << "DEBUG: visitIf" << std::endl;
    // (if condition then-expr else-expr)
    if (node->children.size() < 3 || node->children.size() > 4) {
        context.addError("If expression requires 2 or 3 arguments");
        return nullptr;
    }
    
    // Generate condition code
    llvm::Value* condValue = visit(node->children[1]);
    if (!condValue) return nullptr;
    
    std::cout << "DEBUG: If condition type: " << condValue->getType()->getTypeID() << std::endl;
    
    // Convert to boolean if needed
    if (!condValue->getType()->isIntegerTy(1)) {
        // For integer types, compare not equal to zero
        if (condValue->getType()->isIntegerTy()) {
            condValue = context.getBuilder().CreateICmpNE(
                condValue, 
                llvm::ConstantInt::get(condValue->getType(), 0),
                "ifcond");
        }
        // For floating point types, compare not equal to zero
        else if (condValue->getType()->isFloatingPointTy()) {
            condValue = context.getBuilder().CreateFCmpONE(
                condValue,
                llvm::ConstantFP::get(condValue->getType(), 0.0),
                "ifcond");
        }
        else {
            context.addError("Condition must be a boolean, integer, or floating point value");
            return nullptr;
        }
    }
    
    std::cout << "DEBUG: Converted condition type: " << condValue->getType()->getTypeID() << std::endl;
    
    // Get the current function
    llvm::Function* function = context.getCurrentFunction();
    if (!function) {
        llvm::BasicBlock* currentBB = context.getBuilder().GetInsertBlock();
        if (currentBB) {
            function = currentBB->getParent();
        }
        
        if (!function) {
            context.addError("Cannot evaluate if expression outside of a function context");
            return nullptr;
        }
    }
    
    // Create basic blocks for then, else, and merge
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(context.getContext(), "then", function);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context.getContext(), "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context.getContext(), "ifcont");
    
    // Create conditional branch using the condition value
    context.getBuilder().CreateCondBr(condValue, thenBB, elseBB);
    
    // Generate code for 'then' block
    context.getBuilder().SetInsertPoint(thenBB);
    llvm::Value* thenValue = visit(node->children[2]);
    if (!thenValue) return nullptr;
    
    context.getBuilder().CreateBr(mergeBB);
    thenBB = context.getBuilder().GetInsertBlock();
    
    // Generate code for 'else' block
    elseBB->insertInto(function);  // Add elseBB to the function
    context.getBuilder().SetInsertPoint(elseBB);
    
    llvm::Value* elseValue = nullptr;
    if (node->children.size() > 3) {
        elseValue = visit(node->children[3]);
        if (!elseValue) return nullptr;
    } else {
        // Default else value is 0
        elseValue = llvm::ConstantInt::get(context.getIntType(), 0);
    }
    
    context.getBuilder().CreateBr(mergeBB);
    elseBB = context.getBuilder().GetInsertBlock();
    
    // Generate code for merge block
    mergeBB->insertInto(function);  // Add mergeBB to the function
    context.getBuilder().SetInsertPoint(mergeBB);
    
    // Create PHI node for result
    llvm::PHINode* phi = context.getBuilder().CreatePHI(
        thenValue->getType(), 2, "iftmp");
    
    phi->addIncoming(thenValue, thenBB);
    phi->addIncoming(elseValue, elseBB);
    
    // Store the current insert point
    llvm::BasicBlock* currentBlock = context.getBuilder().GetInsertBlock();
    
    // Check if we're at the top level of a function (i.e., not nested in another expression)
    // We can't use getBasicBlockList() directly, so we'll use a different approach
    bool isTopLevel = false;
    
    // If this is the only block or we're in a simple function, it might be top level
    // We'll just return the PHI node and let the caller handle termination
    
    return phi;
}

llvm::Value* LLVMCodeGenVisitor::visitLet(std::shared_ptr<ListNode> node) {
    // (let ((var1 val1) (var2 val2) ...) body)
    if (node->children.size() < 3) {
        context.addError("Let expression requires bindings and body");
        return nullptr;
    }
    
    // Create new scope
    context.pushScope();
    
    // Process bindings
    auto bindingsNode = node->children[1];
    if (bindingsNode->nodeType != ASTNode::NodeType::LIST) {
        context.addError("Let bindings must be a list");
        context.popScope();
        return nullptr;
    }
    
    for (const auto& binding : bindingsNode->children) {
        if (binding->nodeType != ASTNode::NodeType::LIST || binding->children.size() != 2) {
            context.addError("Invalid let binding");
            context.popScope();
            return nullptr;
        }
        
        auto varNode = binding->children[0];
        if (varNode->nodeType != ASTNode::NodeType::ATOM || 
            varNode->atomType != TokenType::SYMBOL) {
            context.addError("Let binding variable must be a symbol");
            context.popScope();
            return nullptr;
        }
        
        std::string varName = std::get<std::string>(varNode->value);
        llvm::Value* varValue = visit(binding->children[1]);
        if (!varValue) {
            context.popScope();
            return nullptr;
        }
        
        // Add to symbol table
        context.setSymbol(varName, varValue);
    }
    
    // Process body
    llvm::Value* result = nullptr;
    for (size_t i = 2; i < node->children.size(); ++i) {
        result = visit(node->children[i]);
        if (!result) {
            context.popScope();
            return nullptr;
        }
    }
    
    // Restore previous scope
    context.popScope();
    
    return result;
}

llvm::Value* LLVMCodeGenVisitor::visitLambda(std::shared_ptr<ListNode> node) {
    // (lambda (params) body)
    if (node->children.size() < 3) {
        context.addError("Lambda expression requires parameters and body");
        return nullptr;
    }
    
    // Get parameters
    auto paramsNode = node->children[1];
    if (paramsNode->nodeType != ASTNode::NodeType::LIST) {
        context.addError("Lambda parameters must be a list");
        return nullptr;
    }
    
    std::vector<std::string> params;
    for (const auto& param : paramsNode->children) {
        if (param->nodeType != ASTNode::NodeType::ATOM || 
            param->atomType != TokenType::SYMBOL) {
            context.addError("Lambda parameter must be a symbol");
            return nullptr;
        }
        
        params.push_back(std::get<std::string>(param->value));
    }
    
    // Create body expressions
    std::vector<std::shared_ptr<ASTNode>> body;
    for (size_t i = 2; i < node->children.size(); ++i) {
        body.push_back(node->children[i]);
    }
    
    // Create function node
    static int lambdaCount = 0;
    std::string lambdaName = "lambda_" + std::to_string(lambdaCount++);
    
    auto functionNode = std::make_shared<FunctionNode>(lambdaName, params, body);
    return visitFunction(functionNode);
}

llvm::Value* LLVMCodeGenVisitor::visitDefine(std::shared_ptr<ListNode> node) {
    // (define var value) or (define (func params) body)
    if (node->children.size() < 3) {
        context.addError("Define requires variable/function and value/body");
        return nullptr;
    }
    
    auto secondNode = node->children[1];
    
    if (secondNode->nodeType == ASTNode::NodeType::ATOM && 
        secondNode->atomType == TokenType::SYMBOL) {
        // Variable definition
        std::string varName = std::get<std::string>(secondNode->value);
        llvm::Value* varValue = visit(node->children[2]);
        if (!varValue) return nullptr;
        
        // Add to global scope
        context.setSymbol(varName, varValue);
        return varValue;
    } 
    else if (secondNode->nodeType == ASTNode::NodeType::LIST) {
        // Function definition
        if (secondNode->children.empty() || 
            secondNode->children[0]->nodeType != ASTNode::NodeType::ATOM ||
            secondNode->children[0]->atomType != TokenType::SYMBOL) {
            context.addError("Function name must be a symbol");
            return nullptr;
        }
        
        std::string funcName = std::get<std::string>(secondNode->children[0]->value);
        
        // Get parameters
        std::vector<std::string> params;
        for (size_t i = 1; i < secondNode->children.size(); ++i) {
            auto param = secondNode->children[i];
            if (param->nodeType != ASTNode::NodeType::ATOM || 
                param->atomType != TokenType::SYMBOL) {
                context.addError("Function parameter must be a symbol");
                return nullptr;
            }
            
            params.push_back(std::get<std::string>(param->value));
        }
        
        // Create body expressions
        std::vector<std::shared_ptr<ASTNode>> body;
        for (size_t i = 2; i < node->children.size(); ++i) {
            body.push_back(node->children[i]);
        }
        
        // Create function node
        auto functionNode = std::make_shared<FunctionNode>(funcName, params, body);
        return visitFunction(functionNode);
    }
    else {
        context.addError("Invalid define syntax");
        return nullptr;
    }
}

llvm::Value* LLVMCodeGenVisitor::generateFunctionCall(
    const std::string& name, const std::vector<llvm::Value*>& args) {
    
    std::cout << "DEBUG: generateFunctionCall: Function name: " << name << std::endl;
    
    // Look up the function in our function table first
    llvm::Function* function = context.getFunction(name);
    
    // If not found in our table, try to get it from the module
    if (!function) {
        function = context.getModule().getFunction(name);
    }
    
    if (!function) {
        // Check for built-in operators
        if (name == "+" && args.size() == 2) {
            return generateBinaryOp("+", args[0], args[1]);
        }
        else if (name == "-" && args.size() == 2) {
            return generateBinaryOp("-", args[0], args[1]);
        }
        else if (name == "*" && args.size() == 2) {
            return generateBinaryOp("*", args[0], args[1]);
        }
        else if (name == "/" && args.size() == 2) {
            return generateBinaryOp("/", args[0], args[1]);
        }
        else if (name == "=" && args.size() == 2) {
            return generateBinaryOp("=", args[0], args[1]);
        }
        else if (name == "<" && args.size() == 2) {
            return generateBinaryOp("<", args[0], args[1]);
        }
        else if (name == ">" && args.size() == 2) {
            return generateBinaryOp(">", args[0], args[1]);
        }
        else if (name == "not" && args.size() == 1) {
            return generateUnaryOp("not", args[0]);
        }
        
        context.addError("Unknown function: " + name);
        return nullptr;
    }
    
    // Check argument count
    if (function->arg_size() != args.size()) {
        context.addError("Incorrect number of arguments to function: " + name);
        return nullptr;
    }
    
    // Create call instruction with proper type checking
    std::vector<llvm::Value*> convertedArgs;
    unsigned idx = 0;
    for (auto& arg : function->args()) {
        llvm::Value* argValue = args[idx];
        
        // Convert argument type if needed
        if (argValue->getType() != arg.getType()) {
            if (arg.getType()->isIntegerTy() && argValue->getType()->isIntegerTy()) {
                // Integer to integer conversion
                argValue = context.getBuilder().CreateIntCast(
                    argValue, arg.getType(), true, "argcast");
            } else if (arg.getType()->isIntegerTy() && argValue->getType()->isFloatingPointTy()) {
                // Float to integer conversion
                argValue = context.getBuilder().CreateFPToSI(
                    argValue, arg.getType(), "fptoi");
            } else if (arg.getType()->isFloatingPointTy() && argValue->getType()->isIntegerTy()) {
                // Integer to float conversion
                argValue = context.getBuilder().CreateSIToFP(
                    argValue, arg.getType(), "itofp");
            } else {
                context.addError("Cannot convert argument type in call to: " + name);
                return nullptr;
            }
        }
        
        convertedArgs.push_back(argValue);
        idx++;
    }
    
    return context.getBuilder().CreateCall(function, convertedArgs, "calltmp");
}

llvm::Value* LLVMCodeGenVisitor::generateBinaryOp(
    const std::string& op, llvm::Value* lhs, llvm::Value* rhs) {

    std::cout << "DEBUG: generateBinaryOp: op: " << op << std::endl;
    
    // Ensure operands are of the same type
    if (lhs->getType() != rhs->getType()) {
        // Simple type promotion - convert integers to doubles if needed
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isDoubleTy()) {
            lhs = context.getBuilder().CreateSIToFP(lhs, context.getDoubleType(), "inttofp");
        }
        else if (lhs->getType()->isDoubleTy() && rhs->getType()->isIntegerTy()) {
            rhs = context.getBuilder().CreateSIToFP(rhs, context.getDoubleType(), "inttofp");
        }
        else {
            context.addError("Type mismatch in binary operation");
            return nullptr;
        }
    }
    
    // Generate appropriate instruction based on operator and type
    if (lhs->getType()->isIntegerTy()) {
        if (op == "+") {
            std::cout << "DEBUG: codeGen - generateBinaryOp: + operator" << std::endl;
            return context.getBuilder().CreateAdd(lhs, rhs, "addtmp");
        }
        else if (op == "-") {
            return context.getBuilder().CreateSub(lhs, rhs, "subtmp");
        }
        else if (op == "*") {
            return context.getBuilder().CreateMul(lhs, rhs, "multmp");
        }
        else if (op == "/") {
            return context.getBuilder().CreateSDiv(lhs, rhs, "divtmp");
        }
        else if (op == "=") {
            return context.getBuilder().CreateICmpEQ(lhs, rhs, "eqtmp");
        }
        else if (op == "<") {
            return context.getBuilder().CreateICmpSLT(lhs, rhs, "lttmp");
        }
        else if (op == ">") {
            return context.getBuilder().CreateICmpSGT(lhs, rhs, "gttmp");
        }
    }
    else if (lhs->getType()->isDoubleTy()) {
        if (op == "+") {
            return context.getBuilder().CreateFAdd(lhs, rhs, "addtmp");
        }
        else if (op == "-") {
            return context.getBuilder().CreateFSub(lhs, rhs, "subtmp");
        }
        else if (op == "*") {
            return context.getBuilder().CreateFMul(lhs, rhs, "multmp");
        }
        else if (op == "/") {
            return context.getBuilder().CreateFDiv(lhs, rhs, "divtmp");
        }
        else if (op == "=") {
            return context.getBuilder().CreateFCmpOEQ(lhs, rhs, "eqtmp");
        }
        else if (op == "<") {
            return context.getBuilder().CreateFCmpOLT(lhs, rhs, "lttmp");
        }
        else if (op == ">") {
            return context.getBuilder().CreateFCmpOGT(lhs, rhs, "gttmp");
        }
    }
    
    context.addError("Unsupported binary operation: " + op);
    return nullptr;
}

llvm::Value* LLVMCodeGenVisitor::generateUnaryOp(
    const std::string& op, llvm::Value* operand) {
    
    std::cout << "DEBUG: Generating unary operation: " << op << std::endl;
    
    if (op == "not") {
        // Convert to boolean if needed
        if (operand->getType() != context.getBoolType()) {
            std::cout << "DEBUG: Converting operand to boolean" << std::endl;
            operand = context.getBuilder().CreateICmpNE(
                operand, 
                llvm::ConstantInt::get(operand->getType(), 0),
                "tobool");
        }
        
        return context.getBuilder().CreateNot(operand, "nottmp");
    }
    
    context.addError("Unsupported unary operation: " + op);
    std::cout << "DEBUG: Error - unsupported unary operation: " << op << std::endl;
    return nullptr;
}

std::unique_ptr<llvm::Module> LLVMCodeGenVisitor::generateModule(
    std::shared_ptr<ASTNode> ast, const std::string& name) {
    
    std::cout << "DEBUG: Generating module: " << name << std::endl;
    
    // Use the same context as the current visitor
    CodeGenContext newContext(context.getContext(), name);
    LLVMCodeGenVisitor visitor(newContext);
    
    // Generate code for the AST
    std::cout << "DEBUG: Generating code for AST" << std::endl;
    visitor.visit(ast);
    
    // Verify the module
    std::cout << "DEBUG: Verifying module" << std::endl;
    newContext.verifyModule();
    
    if (newContext.hasErrors()) {
        std::cout << "DEBUG: Module has errors" << std::endl;
        for (const auto& error : newContext.getErrors()) {
            std::cout << "DEBUG: Module error: " << error << std::endl;
        }
    } else {
        std::cout << "DEBUG: Module verified successfully" << std::endl;
    }
    
    // Return the module
    return newContext.releaseModule();
}

// Keep these utility functions
bool isSpecialForm(const std::shared_ptr<ListNode>& node) {
    if (node->children.empty()) return false;
    
    auto firstChild = node->children[0];
    if (firstChild->nodeType != ASTNode::NodeType::ATOM || 
        firstChild->atomType != TokenType::SYMBOL) {
        return false;
    }
    
    std::string name = std::get<std::string>(firstChild->value);
    return name == "if" || name == "let" || name == "lambda" || name == "define";
}

std::string getSpecialFormName(const std::shared_ptr<ListNode>& node) {
    if (!isSpecialForm(node)) return "";
    
    auto firstChild = node->children[0];
    return std::get<std::string>(firstChild->value);
}
        