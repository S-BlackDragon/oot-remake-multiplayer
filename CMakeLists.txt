cmake_minimum_required(VERSION 3.24)

project(oot_coop VERSION 0.0.1 LANGUAGES C CXX)

option(COOP_BUILD_TESTS "Build the unit tests" ON)
option(COOP_BUILD_PROGRAMS "Build the server and tools/coop_test" ON)
option(COOP_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC or Clang)" OFF)
option(COOP_TSAN "Build with ThreadSanitizer (GCC or Clang; not with COOP_SANITIZE)" OFF)
# The Hookshot module builds lichii/hook as the emulator does: on Windows, with MSVC (the
# emulator's own compiler for that code). Its tests run everywhere (tests/hookshot).
if(WIN32 AND MSVC)
  set(_coop_hookshot_default ON)
else()
  set(_coop_hookshot_default OFF)
endif()
option(COOP_BUILD_HOOKSHOT "Build hookshot_coop, the co-op module for Hookshot (docs/hookshot.md)" ${_coop_hookshot_default})
if(COOP_SANITIZE OR COOP_TSAN)
  set(COOP_BUILD_HOOKSHOT OFF)  # a shared module with sanitizers adds nothing the tests do not check
endif()
if(COOP_BUILD_HOOKSHOT AND NOT WIN32)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # the static co-op libraries go into a shared module
endif()

# No build machine folders in the programs (the server goes into the kit).
include(cmake/trim_paths.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings for our own targets only; fetched code keeps its own flags.
add_library(coop_warnings INTERFACE)
if(MSVC)
  target_compile_options(coop_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(coop_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Sanitizers apply to everything, ENet included, so its memory is checked too.
if(COOP_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "COOP_SANITIZE is for GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                      -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# ThreadSanitizer cannot be combined with ASan, so it is its own build.
if(COOP_TSAN)
  if(MSVC)
    message(FATAL_ERROR "COOP_TSAN is for GCC or Clang")
  endif()
  if(COOP_SANITIZE)
    message(FATAL_ERROR "COOP_TSAN and COOP_SANITIZE cannot be on together")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()

# The emulator brings its own ENet target (the same v1.3.18); use that one
# when it is there, so the two do not clash. It sets the include path itself.
if(TARGET enet)
  if(WIN32)
    target_link_libraries(enet INTERFACE ws2_32 winmm)
  endif()
else()
  include(cmake/enet.cmake)
endif()

# Traffic encryption (cmake/monocypher.cmake).
if(NOT TARGET monocypher)
  include(cmake/monocypher.cmake)
endif()

# Boost 1.92 (header-only Asio and Beast) for the WebSocket transport, taken
# from the emulator's CPM cache (<your Fritchii checkout>/.cache/cpm/boost/1.92.0).
# Either the modular tree (libs/<name>/include) or the release tree (a boost/
# folder at the root) works. Give it with -DCOOP_BOOST_DIR=... or the
# environment variable COOP_BOOST_DIR.
set(COOP_BOOST_DIR "$ENV{COOP_BOOST_DIR}" CACHE PATH
    "Boost 1.92 source tree (Asio and Beast headers)")
add_library(coop_boost INTERFACE)
if(EXISTS "${COOP_BOOST_DIR}/libs")
  file(GLOB _coop_boost_lib_includes "${COOP_BOOST_DIR}/libs/*/include")
  target_include_directories(coop_boost SYSTEM INTERFACE ${_coop_boost_lib_includes})
endif()
if(EXISTS "${COOP_BOOST_DIR}/boost")
  target_include_directories(coop_boost SYSTEM INTERFACE "${COOP_BOOST_DIR}")
endif()
if(NOT EXISTS "${COOP_BOOST_DIR}/libs" AND NOT EXISTS "${COOP_BOOST_DIR}/boost")
  message(FATAL_ERROR "Boost 1.92 not found at ${COOP_BOOST_DIR}; set COOP_BOOST_DIR")
endif()

# OpenSSL 4 for the TLS (wss://) WebSocket transport, taken from the emulator's
# CPM cache (<your Fritchii checkout>/.cache/cpm/openssl/4.0.1-<id>-windows-amd64).
# Same pattern as COOP_BOOST_DIR: -DCOOP_OPENSSL_DIR=... or the environment
# variable COOP_OPENSSL_DIR, and a clear error when it is missing.
set(COOP_OPENSSL_DIR "$ENV{COOP_OPENSSL_DIR}"
    CACHE PATH "OpenSSL prebuilt tree (libssl and libcrypto)")
if(NOT EXISTS "${COOP_OPENSSL_DIR}/include" OR NOT EXISTS "${COOP_OPENSSL_DIR}/lib")
  message(FATAL_ERROR "OpenSSL not found at ${COOP_OPENSSL_DIR}; set COOP_OPENSSL_DIR")
endif()
if(MSVC)
  set(_coop_openssl_suffix "lib")
else()
  set(_coop_openssl_suffix "a")
endif()
add_library(coop_openssl_ssl STATIC IMPORTED)
set_target_properties(coop_openssl_ssl PROPERTIES
    IMPORTED_LOCATION "${COOP_OPENSSL_DIR}/lib/libssl.${_coop_openssl_suffix}")
add_library(coop_openssl_crypto STATIC IMPORTED)
set_target_properties(coop_openssl_crypto PROPERTIES
    IMPORTED_LOCATION "${COOP_OPENSSL_DIR}/lib/libcrypto.${_coop_openssl_suffix}")
add_library(coop_openssl INTERFACE)
target_include_directories(coop_openssl SYSTEM INTERFACE "${COOP_OPENSSL_DIR}/include")
target_link_libraries(coop_openssl INTERFACE coop_openssl_ssl coop_openssl_crypto)
if(WIN32)
  target_link_libraries(coop_openssl INTERFACE crypt32 ws2_32)
endif()

add_subdirectory(coop)
if(COOP_BUILD_HOOKSHOT)
  add_subdirectory(hookshot)
endif()
if(COOP_BUILD_PROGRAMS)
  add_subdirectory(server)
  add_subdirectory(tools/coop_test)
endif()

if(COOP_BUILD_TESTS)
  enable_testing()
  include(cmake/doctest.cmake)
  add_subdirectory(tests)
endif()
