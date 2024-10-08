include(CMakePackageConfigHelpers)

set (libde265_sources 
  alloc_pool.cc
  bitstream.cc
  cabac.cc
  configparam.cc
  contextmodel.cc
  de265.cc
  deblock.cc
  decctx.cc
  dpb.cc
  en265.cc
  fallback-dct.cc
  fallback-intrapred.cc
  fallback-motion.cc
  fallback-postfilter.cc
  fallback.cc
  image-io.cc
  image.cc
  intrapred.cc
  md5.cc
  motion.cc
  nal-parser.cc
  nal.cc
  pps.cc
  quality.cc
  refpic.cc
  sao.cc
  scan.cc
  sei.cc
  slice.cc
  sps.cc
  threads.cc
  transform.cc
  util.cc
  visualize.cc
  vps.cc
  vui.cc
)

set (libde265_headers
  acceleration.h
  alloc_pool.h
  bitstream.h
  cabac.h
  configparam.h
  de265-version.h
  contextmodel.h
  de265.h
  deblock.h
  decctx.h
  dpb.h
  en265.h
  fallback-dct.h
  fallback-intrapred.h
  fallback-motion.h
  fallback-postfilter.h
  fallback.h
  image-io.h
  image.h
  intrapred.h
  md5.h
  motion.h
  nal-parser.h
  nal.h
  pps.h
  quality.h
  refpic.h
  sao.h
  scan.h
  sei.h
  slice.h
  sps.h
  threads.h
  transform.h
  util.h
  visualize.h
  vps.h
  vui.h
)

set (libde265_public_headers
  de265.h
  en265.h
  ${CMAKE_CURRENT_BINARY_DIR}/de265-version.h
)

if(MSVC OR MINGW)
  set (libde265_sources
    ${libde265_sources}
    ../extra/win32cond.c
    ../extra/win32cond.h
  )
endif()

add_definitions(-DLIBDE265_EXPORTS)

add_subdirectory (encoder)

if(NOT DISABLE_ARM)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cpu_lowercase)
  if(cpu_lowercase MATCHES "^arm")
    set(TARGET_CPU "arm")
    add_definitions(-DHAVE_ARM)
    add_subdirectory (arm_new)
    SET(DISABLE_SSE ON)
  elseif(cpu_lowercase MATCHES "aarch64")
    set(TARGET_CPU "arm64")
    add_definitions(-DHAVE_ARM)
    add_definitions(-DHAVE_ARM64)
    add_subdirectory (arm_new)
    SET(DISABLE_SSE ON)
  else()
    #message(WARNING "The architecture ${CMAKE_SYSTEM_PROCESSOR} is not "
    #                "supported, falling back to the generic target")
    SET(TARGET_CPU "generic")
  endif()
  #message(WARING "The architecture is ${TARGET_CPU}")
endif()

if(NOT DISABLE_SSE)
check_c_source_compiles(
  "#if !defined(__x86_64) && !defined(__i386__) \
  && !defined(_M_IX86) && !defined(_M_AMD64)
  #error not x86
  #endif
  int main(){return 0;}"
  HAVE_X86)

if(HAVE_X86)
  if (MSVC)
    set(SUPPORTS_SSE2 1)
    set(SUPPORTS_SSSE3 1)
    set(SUPPORTS_SSE4_1 1)
  else (MSVC)
    check_c_compiler_flag(-msse2 SUPPORTS_SSE2)
    check_c_compiler_flag(-mssse3 SUPPORTS_SSSE3)
    check_c_compiler_flag(-msse4.1 SUPPORTS_SSE4_1)
    check_c_compiler_flag(-mavx SUPPORTS_AVX)
    check_c_compiler_flag(-mavx2 SUPPORTS_AVX2)
    check_c_compiler_flag(-mavx512f SUPPORTS_AVX512F)
    check_c_compiler_flag(-mavx512bw SUPPORTS_AVX512BW)
  endif (MSVC)

  message(WARNING "SUPPORTS_AVX ${SUPPORTS_AVX}")
  message(WARNING "SUPPORTS_AVX2 ${SUPPORTS_AVX2}")
  message(WARNING "SUPPORTS_AVX512F ${SUPPORTS_AVX512F}")
  message(WARNING "SUPPORTS_AVX512BW ${SUPPORTS_AVX512BW}")

  if(SUPPORTS_SSE4_1)
    add_definitions(-DHAVE_SSE4_1)
  endif()

  if(SUPPORTS_AVX AND SUPPORTS_AVX2)
    add_definitions(-DHAVE_AVX2)
    set(CMAKE_CXX_FLAGS "-mavx")
    set(CMAKE_CXX_FLAGS "-mavx2" )
  endif()

  if(SUPPORTS_AVX512F AND SUPPORTS_AVX512BW)
    add_definitions(-DHAVE_AVX512)
  endif()



  if(SUPPORTS_SSE4_1 OR (SUPPORTS_SSE2 AND SUPPORTS_SSSE3) OR (SUPPORTS_AVX AND SUPPORTS_AVX2) OR (SUPPORTS_AVX512F AND SUPPORTS_AVX512BW))
    add_subdirectory (x86_new)
  endif()
endif()
endif()


add_library(de265 ${libde265_sources} ${ENCODER_OBJECTS} ${DSP_OBJECTS})
target_link_libraries(de265 PRIVATE Threads::Threads)
target_include_directories(de265 PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})

write_basic_package_version_file(libde265ConfigVersion.cmake COMPATIBILITY ExactVersion)

if (WIN32)
    set_target_properties(de265 PROPERTIES PREFIX "lib")
endif()

install(TARGETS de265 EXPORT libde265Config
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${libde265_public_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libde265)
install(EXPORT libde265Config DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libde265")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libde265ConfigVersion.cmake DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/libde265")


# --- pkg-config

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix "\${prefix}")
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(libdir "${CMAKE_INSTALL_LIBDIR}")
else()
    set(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(includedir "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()

set(VERSION ${PROJECT_VERSION}) # so that the replacement in libde265.pc will work with both autotools and CMake
configure_file(../libde265.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libde265.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libde265.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
