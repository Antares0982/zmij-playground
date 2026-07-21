cmake_minimum_required(VERSION 3.16)
project(zmij-benchmark C)

option(SSE41 "Enable SSE4.1 for all zmij builds" OFF)
option(USE_GCC "Use gcc instead of clang" OFF)
option(WITH_RUST "Build the Rust implementation (auto-detected if not set)" ON)

# --------------------------------------------------------------------------- #
# Build shared libraries via build_libs.py
# --------------------------------------------------------------------------- #

set(LIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/libs")
set(PYTHON_SCRIPT "${CMAKE_SOURCE_DIR}/build_libs.py")

set(BUILD_LIBS_ARGS "${PYTHON_SCRIPT}" "--output-dir" "${LIB_OUTPUT_DIR}")

if(SSE41)
  list(APPEND BUILD_LIBS_ARGS "--sse41")
endif()
if(USE_GCC)
  list(APPEND BUILD_LIBS_ARGS "--cc" "gcc")
endif()

# Rust is optional: build it only when explicitly requested *and* cargo exists.
find_program(CARGO_EXECUTABLE cargo)
if(WITH_RUST AND CARGO_EXECUTABLE)
  set(BUILD_RUST ON)
  message(STATUS "zmij-rust: enabled (cargo: ${CARGO_EXECUTABLE})")
elseif(WITH_RUST)
  set(BUILD_RUST OFF)
  message(STATUS "zmij-rust: disabled (cargo not found in PATH)")
else()
  set(BUILD_RUST OFF)
  message(STATUS "zmij-rust: disabled (WITH_RUST=OFF)")
endif()

set(LIB_OUTPUTS "${LIB_OUTPUT_DIR}/libzmij_cpp.so"
                "${LIB_OUTPUT_DIR}/libxjb.so")
set(LIB_SOURCES
    "${CMAKE_SOURCE_DIR}/zmij-cpp/zmij.cc"
    "${CMAKE_SOURCE_DIR}/zmij-cpp/zmij.h" "${CMAKE_SOURCE_DIR}/xjb/ftoa.cpp")

if(BUILD_RUST)
  list(APPEND LIB_OUTPUTS "${LIB_OUTPUT_DIR}/libzmij_rust.so")
  list(APPEND LIB_SOURCES "${CMAKE_SOURCE_DIR}/zmij-rust/src/lib.rs"
       "${CMAKE_SOURCE_DIR}/zmij-rust/Cargo.toml")
else()
  list(APPEND BUILD_LIBS_ARGS "--no-rust")
endif()

add_custom_command(
  OUTPUT ${LIB_OUTPUTS}
  COMMAND python3 ${BUILD_LIBS_ARGS}
  DEPENDS "${PYTHON_SCRIPT}" ${LIB_SOURCES}
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building zmij shared libraries via build_libs.py"
  VERBATIM)

add_custom_target(ftoa_libs ALL DEPENDS ${LIB_OUTPUTS})

# --------------------------------------------------------------------------- #
# Benchmark executable
# --------------------------------------------------------------------------- #

add_executable(zmij_benchmark src/benchmark.c)
target_link_libraries(zmij_benchmark PRIVATE dl m)
target_compile_options(zmij_benchmark PRIVATE -O2 -Wall -Wextra)
add_dependencies(zmij_benchmark ftoa_libs)

# Make the lib output dir available as a compile definition so the benchmark can
# locate .so files at runtime by default.
target_compile_definitions(zmij_benchmark
                           PRIVATE DEFAULT_LIB_DIR="${LIB_OUTPUT_DIR}")

# --------------------------------------------------------------------------- #
# Generic dtoa benchmark (any library)
# --------------------------------------------------------------------------- #

add_executable(any_ftoa_benchmark src/any_ftoa_benchmark.c)
target_link_libraries(any_ftoa_benchmark PRIVATE dl m)
target_compile_options(any_ftoa_benchmark PRIVATE -O2 -Wall -Wextra)

# --------------------------------------------------------------------------- #
# Dtoa verifier
# --------------------------------------------------------------------------- #

add_executable(verifier src/verifier.c)
target_link_libraries(verifier PRIVATE dl)
target_compile_options(verifier PRIVATE -O2 -Wall -Wextra)
