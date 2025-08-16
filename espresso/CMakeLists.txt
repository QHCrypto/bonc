include(FetchContent)

FetchContent_Declare(espresso
  GIT_REPOSITORY https://github.com/classabbyamp/espresso-logic.git
  GIT_TAG 1.1
)
FetchContent_MakeAvailable(espresso)

file(GLOB ESPRESSO_SRC CONFIGURE_DEPENDS "${espresso_SOURCE_DIR}/espresso-src/*.c")
file(GLOB ESPRESSO_HEADERS CONFIGURE_DEPENDS "${espresso_SOURCE_DIR}/espresso-src/*.h")

add_library(espresso STATIC ${ESPRESSO_SRC})
set_target_properties(espresso PROPERTIES
    C_STANDARD 99
)

target_compile_options(espresso PRIVATE -Wno-write-strings)
target_include_directories(espresso PUBLIC "${espresso_SOURCE_DIR}/espresso-src")

# Optionally, install the library and headers
# install(TARGETS espresso ARCHIVE DESTINATION lib)
# install(FILES ${ESPRESSO_HEADERS} DESTINATION include)
