cmake_minimum_required(VERSION 3.28)

project(TarantoolBenchmark CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ttbench ttbench.cc msgpuck/msgpuck.c)
target_link_libraries(ttbench yaml)
target_compile_options(ttbench PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-deprecated-declarations)

add_executable(ttbenchcmp ttbenchcmp.cc)
target_compile_options(ttbenchcmp PRIVATE -Wall -Wextra -Wpedantic)
