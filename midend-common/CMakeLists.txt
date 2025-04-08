
add_library(bonc-midend-common
  src/frontend_result_parser.cpp
  src/lookup_table.cpp)

find_package(Boost REQUIRED)
find_package(nlohmann_json REQUIRED)

target_include_directories(bonc-midend-common PUBLIC includes)
target_link_libraries(bonc-midend-common
    PUBLIC
        nlohmann_json::nlohmann_json
        Boost::boost
)
