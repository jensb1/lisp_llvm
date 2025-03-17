# lisp_llvm

# Lisp Interpreter with LLVM JIT Compiler

This project implements a toy Lisp interpreter with LLVM JIT compilation capabilities.

## Building the Project

### Prerequisites

- CMake (version 3.10 or higher)
- C++ compiler with C++17 support
- LLVM (development packages)

### Build Instructions

1. Clone the repository:
   ```bash
   git clone https://github.com/jensb1/lisp_llvm.git
   cd lisp_llvm
   ```

2. Create a build directory and navigate to it:
   ```bash
   mkdir build
   cd build
   ```

3. Configure the project with CMake:
   ```bash
   cmake ..
   ```

4. Build the project:
   ```bash
   make
   ```

5. Run the interpreter or tests:
   ```bash
   ./LispInterpreter  # Run the REPL
   ./LispTest         # Run the test suite
   ```

### CMake Configuration

The project uses CMake for build configuration. The main CMakeLists.txt file:
- Sets C++17 as the required standard
- Finds and configures LLVM dependencies
- Builds two executables:
  - `LispInterpreter`: The main REPL for interactive use
  - `LispTest`: Test suite for verifying functionality

## Usage

Once built, you can run the REPL and enter Lisp expressions:
