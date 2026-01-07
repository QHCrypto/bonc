find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(bonc-backend-nm src/lib.cpp src/main.cpp)

target_link_libraries(bonc-backend-nm PRIVATE bonc-midend-common bonc-backend-common Boost::program_options)
