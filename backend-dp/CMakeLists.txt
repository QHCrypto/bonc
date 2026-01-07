find_package(Boost REQUIRED COMPONENTS program_options)

add_executable(bonc-backend-dp src/main.cpp src/polyhedron.cpp src/sbox_modelling.cpp)

target_link_libraries(bonc-backend-dp PRIVATE bonc-midend-common bonc-backend-common Boost::program_options BUGSENG::ppl)

# Gurobi configuration, change to your own
set(GUROBI_DIR "/opt/gurobi1103/linux64")
set(GUROBI_VER "110")

set(GUROBI_INCLUDE_DIR "${GUROBI_DIR}/include")
set(GUROBI_LIBRARY "${GUROBI_DIR}/lib/libgurobi_c++.a")
set(GUROBI_LIBRARY_C "${GUROBI_DIR}/lib/libgurobi${GUROBI_VER}.so")

target_include_directories(bonc-backend-dp PRIVATE "${GUROBI_INCLUDE_DIR}")
target_link_libraries(bonc-backend-dp PRIVATE "${GUROBI_LIBRARY}" "${GUROBI_LIBRARY_C}")
