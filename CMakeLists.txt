cmake_minimum_required(VERSION 3.20)
project(CurrenMemory LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CURREN_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(CURREN_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

add_library(curren src/arena.cpp src/table.cpp)
target_include_directories(curren PUBLIC include)

enable_testing()
add_executable(test_arena tests/test_arena.cpp)
target_link_libraries(test_arena PRIVATE curren)
add_test(NAME arena COMMAND test_arena)

add_executable(test_table tests/test_table.cpp)
target_link_libraries(test_table PRIVATE curren)
add_test(NAME table COMMAND test_table)
