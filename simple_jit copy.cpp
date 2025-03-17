#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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

using namespace llvm;
using namespace llvm::orc;

// A simple JIT compiler class with optimized dependency tracking
class SimpleJIT {
private:
    std::unique_ptr<ExecutionSession> ES;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;
    DataLayout DL;
    MangleAndInterner Mangle;
    ThreadSafeContext Ctx;
    JITDylib &MainJD; // Reference to our main JITDylib
    
    // Maps functions to their resource trackers
    std::unordered_map<std::string, ResourceTrackerSP> FunctionTrackers;
    
    // Maps functions to their dependencies (functions that call them)
    std::unordered_map<std::string, std::unordered_set<std::string>> FunctionDependents;
    
    // Maps functions to the functions they call (dependencies)
    std::unordered_map<std::string, std::unordered_set<std::string>> FunctionDependencies;

public:
    SimpleJIT() 
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
    static std::unique_ptr<ExecutionSession> createExecutionSession() {
        // Register platform support - make sure this happens first
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        
        // Now create the execution session
        return std::make_unique<ExecutionSession>(
             cantFail(SelfExecutorProcessControl::Create()));
    }
    
    // Helper function to get target machine builder
    static JITTargetMachineBuilder getTargetMachineBuilder() {
        auto JTMB = JITTargetMachineBuilder::detectHost();
        if (!JTMB) {
            errs() << "Failed to create JITTargetMachineBuilder: " 
                   << toString(JTMB.takeError()) << "\n";
            exit(1);
        }
        return *JTMB;
    }
    
    // Helper function to get data layout
    static DataLayout getDataLayout() {
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
    void reportError(Error Err) {
        errs() << "Error: ";
        logAllUnhandledErrors(std::move(Err), errs(), "");
        errs() << "\n";
    }

    // Create a module with a function that returns a constant
    std::unique_ptr<Module> createGetFirstValueModule(int returnValue) {
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
    std::unique_ptr<Module> createAddModule() {
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
    Error addFunctionModule(std::unique_ptr<Module> M, const std::string &FunctionName, 
                          const std::unordered_set<std::string> &Dependencies = {}) {
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
    Error initialize(int firstValue) {
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
    Error replaceFunction(const std::string &FunctionName, std::unique_ptr<Module> NewM) {
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
    Error replaceGetFirstValue(int newValue) {
        auto NewModule = createGetFirstValueModule(newValue);
        return replaceFunction("getFirstValue", std::move(NewModule));
    }
    
    // Look up a JIT'd function (single arg version for add)
    Expected<int (*)(int)> lookupAdd() {
        auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle("add"));
        if (!SymbolsOrErr)
            return SymbolsOrErr.takeError();
        
        // Cast to the right function type and return
        auto AddFuncAddr = (*SymbolsOrErr).getAddress();
        return (int (*)(int))(intptr_t)AddFuncAddr.getValue();
    }
    
    // Look up a JIT'd function that takes no args and returns an int
    Expected<int (*)()> lookupGetFirstValue() {
        auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle("getFirstValue"));
        if (!SymbolsOrErr)
            return SymbolsOrErr.takeError();
        
        // Cast to the right function type and return
        auto FuncAddr = (*SymbolsOrErr).getAddress();
        return (int (*)())(intptr_t)FuncAddr.getValue();
    }
};

// Main function to demonstrate JIT usage
int main() {
    try {
        // Create our JIT
        SimpleJIT TheJIT;
        
        // Initialize with getFirstValue returning 5 and add function
        std::cout << "Initializing JIT with getFirstValue returning 5..." << std::endl;
        if (auto Err = TheJIT.initialize(5)) {
            llvm::errs() << "Error initializing JIT: ";
            logAllUnhandledErrors(std::move(Err), errs(), "");
            return 1;
        }
        
        // Look up the functions
        auto GetFirstValueFuncOrErr = TheJIT.lookupGetFirstValue();
        if (!GetFirstValueFuncOrErr) {
            llvm::errs() << "Failed to look up getFirstValue function: ";
            logAllUnhandledErrors(GetFirstValueFuncOrErr.takeError(), errs(), "");
            return 1;
        }
        auto GetFirstValueFunc = *GetFirstValueFuncOrErr;
        
        auto AddFuncOrErr = TheJIT.lookupAdd();
        if (!AddFuncOrErr) {
            llvm::errs() << "Failed to look up add function: ";
            logAllUnhandledErrors(AddFuncOrErr.takeError(), errs(), "");
            return 1;
        }
        auto AddFunc = *AddFuncOrErr;
        
        // Call both functions to verify
        std::cout << "Direct call to getFirstValue(): " << GetFirstValueFunc() << std::endl;
        int Result1 = AddFunc(7);
        std::cout << "Result of add(7) [using getFirstValue()=5]: " << Result1 << std::endl;
        
        // Now replace getFirstValue to return 10 (and recompile its dependents)
        std::cout << "\nReplacing getFirstValue to return 10..." << std::endl;
        if (auto Err = TheJIT.replaceGetFirstValue(10)) {
            llvm::errs() << "Error replacing getFirstValue: ";
            logAllUnhandledErrors(std::move(Err), errs(), "");
            return 1;
        }
        
        // Look up the new functions
        auto NewGetFirstValueFuncOrErr = TheJIT.lookupGetFirstValue();
        if (!NewGetFirstValueFuncOrErr) {
            llvm::errs() << "Failed to look up new getFirstValue function: ";
            logAllUnhandledErrors(NewGetFirstValueFuncOrErr.takeError(), errs(), "");
            return 1;
        }
        auto NewGetFirstValueFunc = *NewGetFirstValueFuncOrErr;
        
        auto NewAddFuncOrErr = TheJIT.lookupAdd();
        if (!NewAddFuncOrErr) {
            llvm::errs() << "Failed to look up new add function: ";
            logAllUnhandledErrors(NewAddFuncOrErr.takeError(), errs(), "");
            return 1;
        }
        auto NewAddFunc = *NewAddFuncOrErr;
        
        // Call the new functions to verify
        std::cout << "Direct call to new getFirstValue(): " << NewGetFirstValueFunc() << std::endl;
        int Result2 = NewAddFunc(7);
        std::cout << "Result of add(7) [using getFirstValue()=10]: " << Result2 << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    }
}
