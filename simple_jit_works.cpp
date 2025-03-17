#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

// We need a function pointer type for getFirstValue
typedef int (*GetValueFuncType)();

// A global variable to hold the current implementation
static GetValueFuncType CurrentGetFirstValueImpl = nullptr;

// The wrapper function that calls the current implementation
extern "C" {
    int getFirstValue() {
        // This always calls whatever function is pointed to by CurrentGetFirstValueImpl
        if (CurrentGetFirstValueImpl) {
            return CurrentGetFirstValueImpl();
        }
        return 0; // Default if not set
    }
}

// A simple JIT compiler class
class SimpleJIT {
private:
    std::unique_ptr<ExecutionSession> ES;
    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;
    DataLayout DL;
    MangleAndInterner Mangle;
    ThreadSafeContext Ctx;
    JITDylib &MainJD; // Reference to our main JITDylib
    
    // Counter for generating unique function names
    int ImplCounter = 0;

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

    // Create a module with a new implementation function
    std::unique_ptr<Module> createGetValueImplModule(int returnValue) {
        // Generate a unique name for this implementation
        std::string ImplName = "getFirstValueImpl_" + std::to_string(ImplCounter++);
        
        auto M = std::make_unique<Module>(ImplName + "_module", *Ctx.getContext());
        M->setDataLayout(DL);

        // Create a function that returns an int
        FunctionType *FT = FunctionType::get(Type::getInt32Ty(*Ctx.getContext()), false);
        Function *F = Function::Create(FT, Function::ExternalLinkage, ImplName, M.get());
        
        // Create a new basic block
        BasicBlock *BB = BasicBlock::Create(*Ctx.getContext(), "entry", F);
        IRBuilder<> Builder(BB);
        
        // Return the specified value
        Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(*Ctx.getContext()), returnValue));
        
        // Verify the function
        verifyFunction(*F);
        
        std::cout << "Created " << ImplName << " function that returns " << returnValue << std::endl;
        
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

    // Add a module to the JIT
    Error addModule(std::unique_ptr<Module> M) {
        return CompileLayer.add(MainJD, ThreadSafeModule(std::move(M), Ctx));
    }
    
    // Create and set a new implementation for getFirstValue
    Error setGetFirstValueImpl(int returnValue) {
        // Create a new implementation module
        auto ImplModule = createGetValueImplModule(returnValue);
        
        // Get the name of the implementation function
        std::string ImplName = "getFirstValueImpl_" + std::to_string(ImplCounter - 1);
        
        // Add the module to the JIT
        if (auto Err = addModule(std::move(ImplModule)))
            return Err;
            
        // Look up the new implementation
        auto SymbolsOrErr = ES->lookup({&MainJD}, Mangle(ImplName));
        if (!SymbolsOrErr)
            return SymbolsOrErr.takeError();
            
        // Get the function address and cast to the right type
        auto ImplAddr = (*SymbolsOrErr).getAddress();
        GetValueFuncType NewImpl = (GetValueFuncType)(intptr_t)ImplAddr.getValue();
        
        // Set the current implementation
        CurrentGetFirstValueImpl = NewImpl;
        
        std::cout << "Set CurrentGetFirstValueImpl to " << ImplName << std::endl;
        
        return Error::success();
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
};

// Main function to demonstrate JIT usage
int main() {
    try {
        // Create our JIT
        SimpleJIT TheJIT;
        
        // Create and set the initial getFirstValue implementation
        std::cout << "Creating initial getFirstValue implementation that returns 5..." << std::endl;
        if (auto Err = TheJIT.setGetFirstValueImpl(5)) {
            llvm::errs() << "Error setting initial implementation: ";
            logAllUnhandledErrors(std::move(Err), errs(), "");
            return 1;
        }
        
        // Create and add the add function that calls getFirstValue
        std::cout << "Creating add function that calls getFirstValue()..." << std::endl;
        auto AddModule = TheJIT.createAddModule();
        if (auto Err = TheJIT.addModule(std::move(AddModule))) {
            llvm::errs() << "Error adding add module: ";
            logAllUnhandledErrors(std::move(Err), errs(), "");
            return 1;
        }
        
        // Look up the add function
        auto AddFuncOrErr = TheJIT.lookupAdd();
        if (!AddFuncOrErr) {
            llvm::errs() << "Failed to look up add function: ";
            logAllUnhandledErrors(AddFuncOrErr.takeError(), errs(), "");
            return 1;
        }
        auto AddFunc = *AddFuncOrErr;
        
        // Call getFirstValue directly to verify
        std::cout << "Direct call to getFirstValue(): " << getFirstValue() << std::endl;
        
        // Call add(7) which will use getFirstValue() + 7
        int Result1 = AddFunc(7);
        std::cout << "Result of add(7) [using getFirstValue()=5]: " << Result1 << std::endl;
        
        // Now replace the getFirstValue implementation to return 10 instead
        std::cout << "\nReplacing getFirstValue implementation to return 10 instead..." << std::endl;
        if (auto Err = TheJIT.setGetFirstValueImpl(10)) {
            llvm::errs() << "Error replacing implementation: ";
            logAllUnhandledErrors(std::move(Err), errs(), "");
            return 1;
        }
        
        // Call getFirstValue directly to verify it now returns 10
        std::cout << "Direct call to getFirstValue(): " << getFirstValue() << std::endl;
        
        // Call add(7) again, which should now use the new getFirstValue() + 7
        int Result2 = AddFunc(7);
        std::cout << "Result of add(7) [using getFirstValue()=10]: " << Result2 << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    }
}

