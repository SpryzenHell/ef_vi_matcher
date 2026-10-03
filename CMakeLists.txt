cmake_minimum_required(VERSION 3.20)
project(EFVI_ZeroCopy_Matcher VERSION 2.0.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "EF_VI Zero-Copy Matcher requires Linux for mmap/HUGETLB and the DPDK dataplane")
endif()

option(EFVI_ENABLE_DPDK "Build the optional DPDK dataplane" ON)
option(EFVI_BUILD_TESTS "Build correctness/invariant tests" ON)
option(EFVI_BUILD_BENCHMARKS "Build latency and allocator benchmarks" ON)
option(EFVI_BUILD_EXAMPLES "Build executable examples" ON)
option(EFVI_ENABLE_SANITIZERS "Build tests with ASan/UBSan" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

add_library(efvi_core INTERFACE)
target_include_directories(efvi_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(efvi_core INTERFACE cxx_std_17)
target_compile_options(efvi_core INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
)
target_link_libraries(efvi_core INTERFACE Threads::Threads)

if(EFVI_ENABLE_SANITIZERS)
  target_compile_options(efvi_core INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(efvi_core INTERFACE -fsanitize=address,undefined)
endif()

add_executable(efvi_matcher src/main.cpp)
target_link_libraries(efvi_matcher PRIVATE efvi_core)

add_executable(efvi_hugepage_probe src/hugepage_probe.cpp)
target_link_libraries(efvi_hugepage_probe PRIVATE efvi_core)

if(EFVI_BUILD_BENCHMARKS)
  add_executable(efvi_benchmark benchmarks/efvi_benchmark.cpp)
  target_link_libraries(efvi_benchmark PRIVATE efvi_core)

  add_executable(efvi_allocator_benchmark benchmarks/allocator_benchmark.cpp)
  target_link_libraries(efvi_allocator_benchmark PRIVATE efvi_core)
endif()

if(EFVI_BUILD_EXAMPLES)
  add_executable(efvi_order_flow_demo examples/order_flow_demo.cpp)
  target_link_libraries(efvi_order_flow_demo PRIVATE efvi_core)
endif()

if(EFVI_BUILD_TESTS)
  enable_testing()
  add_executable(efvi_tests tests/efvi_tests.cpp)
  target_link_libraries(efvi_tests PRIVATE efvi_core)
  add_test(NAME efvi_tests COMMAND efvi_tests)
endif()

if(EFVI_ENABLE_DPDK)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(DPDK QUIET IMPORTED_TARGET libdpdk)
  endif()
  if(DPDK_FOUND)
    message(STATUS "DPDK ${DPDK_VERSION} found: enabling mbuf/mempool loopback dataplane")
    add_library(efvi_dpdk STATIC src/dpdk_adapter.cpp)
    target_compile_definitions(efvi_dpdk PUBLIC EFVI_WITH_DPDK=1)
    target_link_libraries(efvi_dpdk PUBLIC efvi_core PkgConfig::DPDK)

    if(EFVI_BUILD_BENCHMARKS)
      add_executable(efvi_dpdk_benchmark benchmarks/efvi_dpdk_benchmark.cpp)
      target_link_libraries(efvi_dpdk_benchmark PRIVATE efvi_dpdk)
    endif()

    if(EFVI_BUILD_EXAMPLES)
      add_executable(efvi_dpdk_nic_rx_probe examples/dpdk_nic_rx.cpp)
      target_link_libraries(efvi_dpdk_nic_rx_probe PRIVATE efvi_dpdk)
    endif()
  else()
    message(STATUS "DPDK not found: core matcher and reference dataplane remain buildable")
  endif()
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS efvi_matcher efvi_hugepage_probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(EFVI_BUILD_BENCHMARKS)
  install(TARGETS efvi_benchmark efvi_allocator_benchmark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(EFVI_BUILD_EXAMPLES)
  install(TARGETS efvi_order_flow_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
