find_package(Boost REQUIRED COMPONENTS program_options regex)

add_executable(bonc-backend-sat src/main.cpp src/cmsat-adapter.hpp)

target_link_libraries(bonc-backend-sat PRIVATE bonc-midend-common sat-modeller Boost::program_options Boost::regex)

target_compile_options(bonc-backend-sat PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow=compatible-local>
)

option(USE_CRYPTOMINISAT5 "Link with cryptominisat5" ON)

if(USE_CRYPTOMINISAT5)
  find_package(cryptominisat5 REQUIRED)
  # message(STATUS "cryptominisat5 executable at: ${CRYPTOMINISAT5_EXECUTABLE}")
  target_link_libraries(bonc-backend-sat PRIVATE cryptominisat5)
  target_compile_definitions(bonc-backend-sat PRIVATE USE_CRYPTOMINISAT5)
endif()