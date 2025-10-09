find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(bonc-backend-dp src/main.cpp)

target_link_libraries(bonc-backend-dp PRIVATE bonc-midend-common Boost::program_options)
