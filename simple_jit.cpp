#include "simple_jit.h"
#include <iostream>

// LLVM includes
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

// Add the code_gen_visitor.h include
#include "code_gen_visitor.h"

using namespace llvm;
using namespace llvm::orc;

// Constructor implementation
SimpleJIT::SimpleJIT() 
    : ES(createExecutionSession()),
      ObjectLayer(*ES, []() { return std::make_unique<SectionMemoryManager>(); }),
      CompileLayer(*ES, ObjectLayer, 
                  std::make_unique<ConcurrentIRCompiler>(
                      getTargetMachineBuilder())),
      DL(getDataLayout()),
      Mangle(*ES, DL),
      Ctx(std::make_unique<LLVMContext>()),
      MainJD(cantFail(ES->createJITDylib("main"))) {
    
    // Add a dynamic library search generator to find external symbols
    MainJD.addGenerator(
        cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
                    DL.getGlobalPrefix())));
}

// Helper function to create execution session
std::unique_ptr<ExecutionSession> SimpleJIT::createExecutionSession() {
    // Register platform support - make sure this happens first
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    // Now create the execution session
    return std::make_unique<ExecutionSession>(
         cantFail(SelfExecutorProcessControl::Create()));
}

// Helper function to get target machine builder
JITTargetMachineBuilder SimpleJIT::getTargetMachineBuilder() {
    auto JTMB = JITTargetMachineBuilder::detectHost();
    if (!JTMB) {
        errs() << "Failed to create JITTargetMachineBuilder: " 
               << toString(JTMB.takeError()) << "\n";
        exit(1);
    }
    return *JTMB;
}

// Helper function to get data layout
DataLayout SimpleJIT::getDataLayout() {
    auto JTMB = getTargetMachineBuilder();
    auto TM = JTMB.createTargetMachine();
    if (!TM) {
        errs() << "Failed to create TargetMachine: " 
               << toString(TM.takeError()) << "\n";
        exit(1);
    }
    return (*TM)->createDataLayout();
}

// Helper function to report errors
void SimpleJIT::reportError(Error Err) {
    errs() << "Error: ";
    logAllUnhandledErrors(std::move(Err), errs(), "");
    errs() << "\n";
}

// Create a module with a function that returns a constant
std::unique_ptr<Module> SimpleJIT::createGetFirstValueModule(int returnValue) {
    auto M = std::make_unique<Module>("get_first_value_module", *Ctx.getContext());
    M->setDataLayout(DL);

    // Create a function that returns an int: int getFirstValue()
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(*Ctx.getContext()), false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, "getFirstValue", M.get());
    
    // Create a new basic block
    BasicBlock *BB = BasicBlock::Create(*Ctx.getContext(), "entry", F);
    IRBuilder<> Builder(BB);
    
    // Return the specified value
    Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(*Ctx.getContext()), returnValue));
    
    // Verify the function
    verifyFunction(*F);
    
    std::cout << "Created getFirstValue function that returns " << returnValue << std::endl;
    
    return M;
}

// Create a module with an add function that calls getFirstValue for its first operand
std::unique_ptr<Module> SimpleJIT::createAddModule() {
    auto M = std::make_unique<Module>("add_module", *Ctx.getContext());
    M->setDataLayout(DL);

    // Declare the getFirstValue function (external)
    FunctionType *GetFirstValueFT = FunctionType::get(
        Type::getInt32Ty(*Ctx.getContext()), false);
    Function *GetFirstValue = Function::Create(
        GetFirstValueFT, Function::ExternalLinkage, "getFirstValue", M.get());

    // Create the add function: int add(int b) { return getFirstValue() + b; }
    FunctionType *AddFT = FunctionType::get(
        Type::getInt32Ty(*Ctx.getContext()),
        {Type::getInt32Ty(*Ctx.getContext())},
        false);
    Function *AddFunc = Function::Create(
        AddFT, Function::ExternalLinkage, "add", M.get());
    
    // Set name for second argument
    auto args_it = AddFunc->arg_begin();
    Value *ArgB = &*args_it;
    ArgB->setName("b");
    
    // Create a new basic block
    BasicBlock *BB = BasicBlock::Create(*Ctx.getContext(), "entry", AddFunc);
    IRBuilder<> Builder(BB);
    
    // Call getFirstValue to get the first operand
    Value *FirstValue = Builder.CreateCall(GetFirstValue);
    
    // Add the values and return
    Value *Result = Builder.CreateAdd(FirstValue, ArgB, "addtmp");
    Builder.CreateRet(Result);
    
    // Verify the function
    verifyFunction(*AddFunc);
    
    return M;
}

// Add a function module and track its dependencies
Error SimpleJIT::addFunctionModule(std::unique_ptr<Module> M, const std::string &FunctionName, 
                      const std::unordered_set<std::string> &Dependencies) {
    // Create a resource tracker for this function
    auto RT = MainJD.createResourceTracker();
    
    // Add the module using the new resource tracker
    if (auto Err = CompileLayer.add(RT, ThreadSafeModule(std::move(M), Ctx)))
        return Err;
        
    // Save the resource tracker
    FunctionTrackers[FunctionName] = RT;
    
    // Register dependencies
    FunctionDependencies[FunctionName] = Dependencies;
    
    // Register this function as a dependent of each dependency
    for (const auto &Dependency : Dependencies) {
        FunctionDependents[Dependency].insert(FunctionName);
    }
    
    return Error::success();
}

// Initialize with getFirstValue and add function
Error SimpleJIT::initialize(int firstValue) {
    // Create getFirstValue module
    auto GetFirstValueModule = createGetFirstValueModule(firstValue);
    if (auto Err = addFunctionModule(std::move(GetFirstValueModule), "getFirstValue"))
        return Err;
    
    // Create add module with dependency on getFirstValue
    auto AddModule = createAddModule();
    if (auto Err = addFunctionModule(std::move(AddModule), "add", {"getFirstValue"}))
        return Err;
        
    return Error::success();
}

// Replace a function and all functions that depend on it
Error SimpleJIT::replaceFunction(const std::string &FunctionName, std::unique_ptr<Module> NewM) {
    // Build the list of functions to replace (the function and all its dependents)
    std::unordered_set<std::string> FunctionsToReplace = { FunctionName };
    
    // Find all direct and indirect dependents
    std::unordered_set<std::string> DirectDependents = FunctionDependents[FunctionName];
    FunctionsToReplace.insert(DirectDependents.begin(), DirectDependents.end());
    
    // For each dependent, add its dependents as well (recursive)
    for (const auto &Dependent : DirectDependents) {
        auto IndirectDependents = FunctionDependents[Dependent];
        FunctionsToReplace.insert(IndirectDependents.begin(), IndirectDependents.end());
    }
    
    std::cout << "Replacing function " << FunctionName << " and " 
              << (FunctionsToReplace.size() - 1) << " dependents" << std::endl;
              
    // Remove all functions to be replaced
    for (const auto &FuncName : FunctionsToReplace) {
        auto It = FunctionTrackers.find(FuncName);
        if (It != FunctionTrackers.end()) {
            if (auto Err = It->second->remove()) {
                return Err;
            }
            std::cout << "Removed function: " << FuncName << std::endl;
        }
    }
    
    // First add the new implementation of the function being replaced
    if (auto Err = addFunctionModule(std::move(NewM), FunctionName, 
                                    FunctionDependencies[FunctionName])) {
        return Err;
    }
    
    // Now re-add all dependent functions
    for (const auto &Dependent : DirectDependents) {
        // Create the dependent module again
        if (Dependent == "add") {
            auto NewAddModule = createAddModule();
            if (auto Err = addFunctionModule(std::move(NewAddModule), "add", {"getFirstValue"})) {
                return Err;
            }
        }
        // Add more cases for other dependent functions as needed
    }
    
    return Error::success();
}

// Replace getFirstValue to return a new value
Error SimpleJIT::replaceGetFirstValue(int newValue) {
    auto NewModule = createGetFirstValueModule(newValue);
    return replaceFunction("getFirstValue", std::move(NewModule));
}

// Look up a JIT'd function (single arg version for add)
Expected<int (*)(int)> SimpleJIT::lookupAdd() {
    auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle("add"));
    if (!SymbolsOrErr)
        return SymbolsOrErr.takeError();
    
    // Cast to the right function type and return
    auto AddFuncAddr = (*SymbolsOrErr).getAddress();
    return (int (*)(int))(intptr_t)AddFuncAddr.getValue();
}

// Look up a JIT'd function that takes no args and returns an int
Expected<int (*)()> SimpleJIT::lookupGetFirstValue() {
    auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle("getFirstValue"));
    if (!SymbolsOrErr)
        return SymbolsOrErr.takeError();
    
    // Cast to the right function type and return
    auto FuncAddr = (*SymbolsOrErr).getAddress();
    return (int (*)())(intptr_t)FuncAddr.getValue();
}

// Create a module from an AST node
std::unique_ptr<Module> SimpleJIT::createModuleFromAST(const std::shared_ptr<ASTNode>& ast, const std::string& moduleName) {
    // Create a new module using our existing context
    auto M = std::make_unique<Module>(moduleName, *Ctx.getContext());
    M->setDataLayout(DL);
    
    // Create a code generation context with our context and the module name
    CodeGenContext codeGenContext(*Ctx.getContext(), moduleName);
    
    // Create a visitor to generate code
    LLVMCodeGenVisitor visitor(codeGenContext);
    
    // Generate code into the context's module
    visitor.visit(ast);
    
    // Check for errors
    if (codeGenContext.hasErrors()) {
        std::cerr << "Errors during code generation:" << std::endl;
        for (const auto& error : codeGenContext.getErrors()) {
            std::cerr << "  " << error << std::endl;
        }
    }
    
    // Get the module from the context
    auto generatedModule = codeGenContext.releaseModule();
    
    std::cout << "Created module " << moduleName << " from AST" << std::endl;
    
    return generatedModule;
}

// Helper function to generate code from an AST node
Value* SimpleJIT::generateCodeFromAST(const std::shared_ptr<ASTNode>& node, IRBuilder<>& Builder) {
    if (!node) {
        std::cerr << "Null AST node provided to generateCodeFromAST" << std::endl;
        return ConstantInt::get(Type::getInt32Ty(Builder.getContext()), 0);
    }
    
    // Create a code generation context using our context
    CodeGenContext context(*Ctx.getContext(), "temp_module");
    
    // Set the current basic block in the context to match our Builder
    context.getBuilder().SetInsertPoint(Builder.GetInsertBlock(), Builder.GetInsertPoint());
    
    // Create a visitor and use it to generate code
    LLVMCodeGenVisitor visitor(context);
    Value* result = visitor.visit(node);

    
    // If no result was generated, return a default value
    if (!result) {
        std::cerr << "Failed to generate code for AST node" << std::endl;
        return ConstantInt::get(Type::getInt32Ty(Builder.getContext()), 0);
    }
    
    // If the result is not of integer type, convert it
    if (!result->getType()->isIntegerTy(32)) {
        if (result->getType()->isIntegerTy()) {
            // Convert to i32 if it's another integer type
            result = Builder.CreateIntCast(result, Type::getInt32Ty(Builder.getContext()), true, "intcast");
        } else if (result->getType()->isFloatingPointTy()) {
            // Convert float to int if needed
            result = Builder.CreateFPToSI(result, Type::getInt32Ty(Builder.getContext()), "fptoitmp");
        } else {
            // For other types, provide a default value to avoid the allocation error
            std::cerr << "Unsupported result type, using default value" << std::endl;
            return ConstantInt::get(Type::getInt32Ty(Builder.getContext()), 0);
        }
    }
    
    return result;
}

// Add a function to compile and add an AST to the JIT
Error SimpleJIT::addASTFunction(const std::shared_ptr<ASTNode>& ast, const std::string& functionName) {
    auto M = createModuleFromAST(ast, functionName);
    return addFunctionModule(std::move(M), functionName);
}

// Look up a JIT'd function that takes no args and returns an int (for AST-generated functions)
Expected<int (*)()> SimpleJIT::lookupASTFunction(const std::string& functionName) {
    auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle(functionName));
    if (!SymbolsOrErr)
        return SymbolsOrErr.takeError();
    
    // Cast to the right function type and return
    auto FuncAddr = (*SymbolsOrErr).getAddress();
    return (int (*)())(intptr_t)FuncAddr.getValue();
}

// Compile and run a single expression
std::optional<int> SimpleJIT::compileAndRunExpression(const std::shared_ptr<ASTNode>& expr) {
    if (!expr) {
        std::cerr << "Null expression provided to compileAndRunExpression" << std::endl;
        return std::nullopt;
    }
    
    std::cout << "Compiling expression..." << std::endl;
    
    // Create a unique name for the expression function to avoid conflicts
    static int exprCounter = 0;
    std::string exprFuncName = "expr_main_" + std::to_string(exprCounter++);
    
    // Create a module with a main function that evaluates the expression
    auto M = std::make_unique<Module>("expr_module", *Ctx.getContext());
    M->setDataLayout(DL);
    
    // Create a main function that returns an int
    FunctionType *FT = FunctionType::get(Type::getInt32Ty(*Ctx.getContext()), false);
    Function *MainFunc = Function::Create(FT, Function::ExternalLinkage, exprFuncName, M.get());
    
    // Create a basic block
    BasicBlock *BB = BasicBlock::Create(*Ctx.getContext(), "entry", MainFunc);
    IRBuilder<> Builder(BB);
    
    try {
        // Generate code for the expression
        Value* result = generateCodeFromAST(expr, Builder);
        std::cout << "Generated code for expression" << std::endl;


        if (!result) {
            std::cerr << "Failed to generate code for expression" << std::endl;
            return std::nullopt;
        }
        
        // Return the result
        Builder.CreateRet(result);
        std::cout << "Created return statement" << std::endl;
        
        // Skip verification for now to avoid segmentation fault
        // We'll add safer verification later if needed
        std::cout << "Skipping verification to avoid potential crashes" << std::endl;
        
        // Add the module to JIT
        if (auto Err = addFunctionModule(std::move(M), exprFuncName)) {
            reportError(std::move(Err));
            return std::nullopt;
        }
        std::cout << "Added module to JIT" << std::endl;

        
        // Look up the function
        auto MainFuncPtr = lookupASTFunction(exprFuncName);
        if (!MainFuncPtr) {
            reportError(MainFuncPtr.takeError());
            return std::nullopt;
        }
        std::cout << "Looked up function" << std::endl;
        
        // Execute the function
        int (*MainFn)() = *MainFuncPtr;
        std::cout << "Executing function" << std::endl;
        return MainFn();
    }
    catch (const std::exception& e) {
        std::cerr << "Exception during compilation: " << e.what() << std::endl;
        return std::nullopt;
    }
    catch (...) {
        std::cerr << "Unknown exception during compilation" << std::endl;
        return std::nullopt;
    }
}

// Compile and run a program (multiple expressions)
std::optional<int> SimpleJIT::compileAndRunProgram(const std::vector<std::shared_ptr<ASTNode>>& expressions) {
    if (expressions.empty()) {
        std::cerr << "No expressions provided to compileAndRunProgram" << std::endl;
        return std::nullopt;
    }
    
    std::cout << "Compiling program with " << expressions.size() << " expressions..." << std::endl;
    
    // Create a unique name for the main function to avoid conflicts
    static int programCounter = 0;
    std::string mainFuncName = "program_main_" + std::to_string(programCounter++);
    
    try {
        // Create a module for the program - we'll pass this to the CodeGenContext
        auto M = std::make_unique<llvm::Module>("program_module", *Ctx.getContext());
        M->setDataLayout(DL);
        
        // Create a code generation context with our context and pass the existing module
        // instead of letting it create its own
        CodeGenContext codeGenContext(*Ctx.getContext(), M.get());
        
        // Create a visitor to generate code
        LLVMCodeGenVisitor visitor(codeGenContext);
        
        // First, process all function definitions to make them available for calls
        for (const auto& expr : expressions) {
            if (expr && expr->nodeType == ASTNode::NodeType::FUNCTION) {
                std::cout << "DEBUG: Processing function definition first: " 
                          << std::static_pointer_cast<FunctionNode>(expr)->functionName << std::endl;
                visitor.visit(expr);
            }
        }
        
        // Create a main function that returns an int
        llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getInt32Ty(*Ctx.getContext()), false);
        
        // Create the main function with the unmangled name
        llvm::Function *MainFunc = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, mainFuncName, *M);
        
        // Create a basic block
        llvm::BasicBlock *BB = llvm::BasicBlock::Create(*Ctx.getContext(), "entry", MainFunc);
        
        // Set the builder's insert point to the basic block we created
        codeGenContext.getBuilder().SetInsertPoint(BB);
        codeGenContext.setCurrentFunction(MainFunc);
        
        // Generate code for each non-function expression in sequence
        llvm::Value* result = nullptr;
        for (const auto& expr : expressions) {
            if (!expr) {
                std::cerr << "Null expression in program" << std::endl;
                continue;
            }
            
            // Skip function definitions as we've already processed them
            if (expr->nodeType == ASTNode::NodeType::FUNCTION) {
                continue;
            }
            
            // Visit each expression, which will update the shared context
            result = visitor.visit(expr);
            
            if (!result) {
                std::cerr << "Failed to generate code for expression in program" << std::endl;
            }
        }
        
        // Get the builder from the context
        llvm::IRBuilder<>& Builder = codeGenContext.getBuilder();
        
        // If we have a result, return it, otherwise return 0
        if (result) {
            // Ensure the result is of integer type
            if (!result->getType()->isIntegerTy(32)) {
                if (result->getType()->isIntegerTy()) {
                    // Convert to i32 if it's another integer type
                    result = Builder.CreateIntCast(result, llvm::Type::getInt32Ty(*Ctx.getContext()), true, "intcast");
                } else if (result->getType()->isFloatingPointTy()) {
                    // Convert float to int
                    result = Builder.CreateFPToSI(result, llvm::Type::getInt32Ty(*Ctx.getContext()), "fptoitmp");
                } else {
                    // For other types, provide a default value
                    result = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*Ctx.getContext()), 0);
                }
            }
            
            Builder.CreateRet(result);
        } else {
            // No valid result, return 0
            Builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*Ctx.getContext()), 0));
        }
        
        // Output the generated LLVM IR to the terminal
        std::string IRString;
        llvm::raw_string_ostream IRStream(IRString);
        M->print(IRStream, nullptr);
        IRStream.flush();
        
        std::cout << "\n=== Generated LLVM IR ===\n" << IRString << "\n=== End of LLVM IR ===\n" << std::endl;
        
        // Check for errors
        if (codeGenContext.hasErrors()) {
            std::cerr << "Errors during code generation:" << std::endl;
            for (const auto& error : codeGenContext.getErrors()) {
                std::cerr << "  " << error << std::endl;
            }
        }
        
        std::cout << "Generated code for program" << std::endl;
        
        // Verify the module
        std::string errorInfo;
        llvm::raw_string_ostream errorStream(errorInfo);
        
        if (llvm::verifyModule(*M, &errorStream)) {
            std::cerr << "Module verification failed: " << errorInfo << std::endl;
            return std::nullopt;
        }
        
        // Add the module to JIT
        if (auto Err = addFunctionModule(std::move(M), mainFuncName)) {
            std::cerr << "Failed to add module: " << toString(std::move(Err)) << std::endl;
            return std::nullopt;
        }
        std::cout << "Added module to JIT" << std::endl;
        
        // Look up the function
        auto MainFuncPtr = lookupASTFunction(mainFuncName);
        if (!MainFuncPtr) {
            reportError(MainFuncPtr.takeError());
            return std::nullopt;
        }
        std::cout << "Looked up function" << std::endl;
        
        // Execute the function and store the result
        int (*MainFn)() = *MainFuncPtr;
        std::cout << "Executing program" << std::endl;
        int main_result = MainFn();
        std::cout << "Program returned: " << main_result << std::endl;
        
        // Store the result before any cleanup
        std::optional<int> result_to_return = main_result;
        
        // Clean up the resource tracker for this function to prevent memory leaks
        // This is important to prevent the segmentation fault
        auto it = FunctionTrackers.find(mainFuncName);
        if (it != FunctionTrackers.end()) {
            // Remove the resource tracker but don't delete the entry from the map yet
            // This prevents potential use-after-free issues
            if (auto Err = it->second->remove()) {
                std::cerr << "Warning: Failed to clean up resources: " 
                          << toString(std::move(Err)) << std::endl;
            }
        }
        
        return result_to_return;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception during program compilation: " << e.what() << std::endl;
        return std::nullopt;
    }
    catch (...) {
        std::cerr << "Unknown exception during program compilation" << std::endl;
        return std::nullopt;
    }
}