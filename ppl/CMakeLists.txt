find_package(GMP REQUIRED)

include(ExternalProject)

set(PPL_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/ppl_src-prefix)

ExternalProject_Add(
  ppl_src
  URL               https://support.bugseng.com/ppl/download/ftp/releases/1.2/ppl-1.2.tar.gz
  URL_HASH          SHA256=6bc36dd4a87abc429d8f9c00c53e334e5041a9b0857cfc00dbad6ef14294aac8
  DOWNLOAD_EXTRACT_TIMESTAMP ON

  INSTALL_DIR       ${PPL_INSTALL_DIR}
  BUILD_BYPRODUCTS  ${PPL_INSTALL_DIR}/lib/libppl.so

  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-interfaces=c,cxx --with-gmp-include=${GMP_INCLUDE_DIR} --with-gmp-lib=${GMP_LIB}
  BUILD_COMMAND     make
  INSTALL_COMMAND   make install
  UPDATE_DISCONNECTED ON
)


message(STATUS "Install dir: ${PPL_INSTALL_DIR}")

# Create the directory during configuration to satisfy INTERFACE include check
file(MAKE_DIRECTORY ${PPL_INSTALL_DIR}/include)

add_library(BUGSENG::ppl SHARED IMPORTED GLOBAL)
add_dependencies(BUGSENG::ppl ppl_src)

set_target_properties(BUGSENG::ppl PROPERTIES IMPORTED_LOCATION "${PPL_INSTALL_DIR}/lib/libppl.so")
target_link_libraries(BUGSENG::ppl INTERFACE GMP::gmp)
target_include_directories(BUGSENG::ppl INTERFACE ${PPL_INSTALL_DIR}/include)

install(DIRECTORY "${PPL_INSTALL_DIR}/lib/"
  DESTINATION lib
  USE_SOURCE_PERMISSIONS
  FILES_MATCHING PATTERN "libppl.so*"
)
