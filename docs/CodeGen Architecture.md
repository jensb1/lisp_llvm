# CodeGen Architecture for AST-based JIT Compilation

This document outlines the design of a code generation system that transforms Abstract Syntax Tree (AST) nodes into LLVM IR for Just-In-Time compilation.

## Overview

The CodeGen system bridges the gap between the high-level AST representation of a program and the low-level LLVM IR needed for efficient execution. It follows a visitor pattern to traverse the AST and generate appropriate LLVM IR instructions.

## Core Components

### 1. CodeGenContext

A context object that maintains the state required during code generation:

- LLVM Context, Module, and Builder references
- Symbol tables for variables and functions
- Type information
- Scope management
- Error reporting mechanisms

### 2. CodeGenVisitor

The main class responsible for traversing the AST and generating LLVM IR:

- Implements methods for each AST node type (atom, list, quoted, function)
- Handles type checking and conversion
- Manages control flow (if/else, loops)
- Implements function calls and definitions

### 3. TypeSystem

Manages type information and conversions:

- Maps language types to LLVM types
- Handles type inference
- Performs type checking
- Manages type conversions and promotions

### 4. SymbolTable

Manages variable and function bindings:

- Maintains a hierarchy of scopes
- Provides lookup mechanisms for variables and functions
- Handles variable shadowing
- Supports closures and lexical scoping

## Code Generation Process

### 1. Initialization

1. Create a CodeGenContext with LLVM infrastructure
2. Initialize the symbol table with built-in functions and constants
3. Set up the module and entry point

### 2. AST Traversal

The CodeGenVisitor traverses the AST in a depth-first manner:

1. For each node, dispatch to the appropriate visitor method based on node type
2. Generate LLVM IR instructions for the node
3. Return LLVM Value objects representing the result of expressions

### 3. Node-Specific Generation

#### Atom Nodes
- Numbers: Generate constant integer or floating-point values
- Strings: Generate global string constants
- Booleans: Generate i1 constants
- Symbols: Look up variables in the symbol table

#### List Nodes
- Function calls: Generate call instructions
- Special forms (if, let, etc.): Generate appropriate control flow

#### Quoted Nodes
- Generate data structures representing the quoted expression

#### Function Nodes
- Generate function definitions with appropriate parameters
- Create new scopes for function bodies
- Handle closures and captured variables

### 4. Optimization and Execution

1. Apply LLVM optimization passes to the generated IR
2. Compile to machine code using LLVM JIT
3. Execute the resulting function

## Error Handling

- Type errors: Detected during code generation
- Undefined variables: Checked against symbol table
- Runtime errors: Implemented through explicit checks in generated code

## Extension Points

### 1. Custom Types

The type system can be extended to support:
- User-defined types
- Algebraic data types
- Type classes or interfaces

### 2. Optimization Passes

Custom optimization passes can be added:
- Constant folding
- Inlining
- Dead code elimination
- Tail call optimization

### 3. Foreign Function Interface

Support for calling external C/C++ functions:
- Type marshaling
- ABI compatibility
- Resource management

## Integration with SimpleJIT

The CodeGen system integrates with the existing SimpleJIT infrastructure:

1. CodeGenVisitor generates LLVM Modules from AST nodes
2. SimpleJIT compiles and manages the resulting modules
3. Function dependencies are tracked for incremental compilation
4. Hot-swapping of functions is supported through the existing replacement mechanism

## Performance Considerations

- Lazy compilation of function bodies
- Caching of compiled expressions
- Incremental compilation of modified code
- Profile-guided optimization

## Memory Management

- LLVM's ownership model for IR objects
- Reference counting for AST nodes
- Garbage collection integration for runtime objects

This architecture provides a flexible and extensible foundation for generating efficient code from AST representations, with a clear separation of concerns between parsing, code generation, and execution.


# Detailed CodeGenVisitor Design

The CodeGenVisitor component needs refinement to better handle the AST structure and provide clearer separation of concerns. Here's an improved design:

## CodeGenVisitor

### Core Design

The CodeGenVisitor should follow a true visitor pattern with:

1. A base visitor interface
2. Specialized implementations for different compilation targets
3. Clear separation between node traversal and code generation logic

### Improved Structure

```
CodeGenVisitor (abstract base class)
├── visit(ASTNode* node) → dispatches to appropriate visit method
├── visitAtom(ASTNode* node)
├── visitList(ASTNode* node)
├── visitQuoted(ASTNode* node)
├── visitFunction(ASTNode* node)
└── visitSpecialForm(ASTNode* node, SpecialFormType type)
```

### Key Improvements

1. **Double Dispatch Pattern**
   - The ASTNode accepts a visitor via an `accept(Visitor* v)` method
   - This calls back to the appropriate visitor method based on node type
   - Eliminates type checking and casting in the visitor

2. **Special Form Handling**
   - Dedicated methods for common special forms (if, let, lambda, etc.)
   - Avoids complex conditional logic in the list node visitor

3. **Expression Result Management**
   - Clear return type (LLVM Value*) for all visitor methods
   - Consistent error handling pattern

4. **Context Separation**
   - The visitor uses but doesn't own the CodeGenContext
   - Allows for different visitors sharing the same context

### Implementation Considerations

1. **Node Type Detection**
   - Use the node's type enum rather than runtime type checking
   - Special forms identified by examining the first symbol in a list

2. **Visitor Registration**
   - Register the visitor with each node type during initialization
   - Allows for extensibility with new node types

3. **Visitor Composition**
   - Support for visitor chaining for multi-pass compilation
   - Pre-processing visitors for analysis before code generation

4. **Error Recovery**
   - Graceful handling of errors during traversal
   - Ability to continue compilation after non-fatal errors

## Example Visitor Method Flow

For a function call expression like `(add 1 2)`:

1. `visit()` dispatches to `visitList()`
2. `visitList()` identifies this as a function call (not a special form)
3. Visitor recursively processes arguments via `visit()`
4. Function is looked up in symbol table
5. LLVM CreateCall instruction is generated with processed arguments
6. Returns the LLVM Value* representing the call result

## Integration with AST

The ASTNode class should be extended with:

```cpp
virtual Value* accept(CodeGenVisitor* visitor) {
    return visitor->visit(this);
}
```

Each derived node type would override this to call the appropriate visitor method:

```cpp
// In AtomNode
Value* accept(CodeGenVisitor* visitor) override {
    return visitor->visitAtom(this);
}
```

This creates a clean separation between the AST structure and the code generation logic, making the system more maintainable and extensible.
