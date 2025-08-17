cmake_minimum_required(VERSION 3.30)

project(bonc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-stdlib=libc++)
endif()

add_subdirectory(espresso)
add_subdirectory(sat-modeller)

add_subdirectory(midend-common)
add_subdirectory(midend-nm)
add_subdirectory(test)
