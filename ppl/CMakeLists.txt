find_package(GMP REQUIRED)

include(ExternalProject)

ExternalProject_Add(
  ppl_src
  URL               https://support.bugseng.com/ppl/download/ftp/releases/1.2/ppl-1.2.tar.gz
  URL_HASH          SHA256=6bc36dd4a87abc429d8f9c00c53e334e5041a9b0857cfc00dbad6ef14294aac8
  DOWNLOAD_EXTRACT_TIMESTAMP ON

  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-interfaces=c,cxx --with-gmp-include=${GMP_INCLUDE_DIR} --with-gmp-lib=${GMP_LIB}
  BUILD_COMMAND     make
  INSTALL_COMMAND   make install
  UPDATE_DISCONNECTED ON
)

ExternalProject_Get_Property(ppl_src INSTALL_DIR)

message(STATUS "Install dir: ${INSTALL_DIR}")

add_library(BUGSENG::ppl SHARED IMPORTED GLOBAL)
set_target_properties(BUGSENG::ppl PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libppl.so)
target_link_libraries(BUGSENG::ppl INTERFACE GMP::gmp)
target_include_directories(BUGSENG::ppl INTERFACE ${INSTALL_DIR}/include)
