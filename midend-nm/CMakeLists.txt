
find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(bonc-midend-nm src/lib.cpp src/main.cpp)

target_link_libraries(bonc-midend-nm PRIVATE bonc-midend-common Boost::program_options)
