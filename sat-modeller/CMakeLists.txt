cmake_minimum_required(VERSION 3.21)

project(SatModeller)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sat-modeller src/main.cpp src/espresso-wrapper.cpp src/sat-modeller.cpp)
target_include_directories(sat-modeller PUBLIC includes)
target_link_libraries(sat-modeller PRIVATE espresso)