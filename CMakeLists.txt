cmake_minimum_required(VERSION 3.10)

# Set the project name
project(LispInterpreter)

# Set CMake policy CMP0075 to NEW to fix the warning about CMAKE_REQUIRED_LIBRARIES
if(POLICY CMP0075)
  cmake_policy(SET CMP0075 NEW)
endif()

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Include LLVM directories
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Link directories (needed if found via llvm-config)
link_directories(${LLVM_LIBRARY_DIRS})

# Define source files
set(COMMON_SOURCES
    lexer.cpp
    parser.cpp
    ast_node.cpp
    code_gen_visitor.cpp
    simple_jit.cpp
)

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs
    Core
    ExecutionEngine
    OrcJIT
    Support
    native
    AArch64
    AArch64AsmParser
    AArch64CodeGen
    AArch64Desc
    AArch64Info
    AArch64Utils
    # Add other LLVM components as needed
)

# Add the main executable
add_executable(LispInterpreter main.cpp ${COMMON_SOURCES})
target_include_directories(LispInterpreter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LispInterpreter ${llvm_libs})

# Add the test executable
add_executable(LispTest test.cpp ${COMMON_SOURCES})
target_include_directories(LispTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LispTest ${llvm_libs})

# Print instructions if build succeeds
add_custom_command(TARGET LispInterpreter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build successful! Run './LispInterpreter' or './LispTest' to execute."
)
