cmake_minimum_required(VERSION 3.30)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules" ${CMAKE_MODULE_PATH})

project(bonc)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-stdlib=libc++)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(espresso)
add_subdirectory(ppl)
add_subdirectory(sat-modeller)

add_subdirectory(midend-common)
add_subdirectory(backend-common)
add_subdirectory(backend-nm)
add_subdirectory(backend-sat)
add_subdirectory(backend-dp)
