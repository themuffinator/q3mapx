cmake_minimum_required(VERSION 3.25)
project(q3mapx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(Q3MAPX_ENABLE_LTO "Enable interprocedural optimization in release builds" OFF)
option(Q3MAPX_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(Q3MAPX_BUILD_BENCHMARKS "Build standalone performance measurement tools" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(XML REQUIRED IMPORTED_TARGET libxml-2.0)
find_package(assimp CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(q3mapx_jobs STATIC libs/q3mapx/job_pool.cpp)
target_include_directories(q3mapx_jobs PUBLIC libs)
target_link_libraries(q3mapx_jobs PUBLIC Threads::Threads)
add_library(q3mapx_io STATIC libs/q3mapx/atomic_file.cpp)
target_include_directories(q3mapx_io PUBLIC libs)
add_library(q3mapx_compute STATIC libs/q3mapx/columns.cpp)
target_include_directories(q3mapx_compute PUBLIC libs)

# Preserve NRC's source layout so changes remain easy to compare with upstream.
file(GLOB Q3MAPX_COMPILER_SOURCES CONFIGURE_DEPENDS tools/quake3/q3map2/*.cpp)
file(GLOB Q3MAPX_COMMON_SOURCES CONFIGURE_DEPENDS tools/quake3/common/*.cpp)
add_executable(q3mapx
    ${Q3MAPX_COMPILER_SOURCES}
    ${Q3MAPX_COMMON_SOURCES}
    libs/crnlib/crnlib.cpp
    libs/ddslib/ddslib.c
    libs/etclib.c
    libs/filematch.c
    libs/l_net/l_net.c
    libs/l_net/l_net_wins.c
    libs/webplib/webplib.cpp
)
target_include_directories(q3mapx PRIVATE tools/quake3/common libs include)
target_link_libraries(q3mapx PRIVATE
    PkgConfig::GLIB PkgConfig::XML assimp::assimp
    PNG::PNG JPEG::JPEG ZLIB::ZLIB Threads::Threads
    q3mapx_jobs q3mapx_io q3mapx_compute
)
target_compile_definitions(q3mapx PRIVATE
    Q3MAP_VERSION="2.5.17n-q3mapx-${PROJECT_VERSION}"
    Q3MAPX_VERSION="${PROJECT_VERSION}"
    Q3MAPX_UPSTREAM_REVISION="8216133984031afaa9a857b56ea66dd9c3d54b26"
    RADIANT_VERSION="NRC-8216133"
    RADIANT_MAJOR_VERSION="1"
    RADIANT_MINOR_VERSION="6"
)

if(WIN32)
    target_compile_definitions(q3mapx PRIVATE WIN32 _WIN32 NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(q3mapx PRIVATE ws2_32 user32 gdi32 ole32)
else()
    target_compile_definitions(q3mapx PRIVATE POSIX)
endif()

if(MSVC)
    target_compile_options(q3mapx PRIVATE /W3 /bigobj /FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_config.h)
    target_link_options(q3mapx PRIVATE /STACK:4194304)
else()
    # The legacy compiler and Crunch decoder require aliasing to be disabled.
    target_compile_options(q3mapx PRIVATE -Wall -Wextra -Wno-unused-parameter
        -Wno-unused-function -fno-strict-aliasing
        -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_config.h)
    if(MINGW)
        target_link_options(q3mapx PRIVATE -Wl,--stack,4194304)
    endif()
    if(Q3MAPX_ENABLE_SANITIZERS)
        target_compile_options(q3mapx PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(q3mapx PRIVATE -fsanitize=address,undefined)
    endif()
endif()

if(Q3MAPX_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(NOT ipo_supported)
        message(FATAL_ERROR "LTO requested but unsupported: ${ipo_error}")
    endif()
    set_property(TARGET q3mapx PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET q3mapx PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

include(CTest)
if(Q3MAPX_BUILD_BENCHMARKS)
    add_executable(scheduler_benchmark benchmarks/scheduler.cpp)
    target_link_libraries(scheduler_benchmark PRIVATE q3mapx_jobs)
endif()
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME cli_help COMMAND q3mapx -help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:" TIMEOUT 15)
    add_test(NAME cli_games COMMAND q3mapx -help -game)
    set_tests_properties(cli_games PROPERTIES PASS_REGULAR_EXPRESSION "Available games:" TIMEOUT 15)
    add_test(NAME compiler_pipeline COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/integration)
    set_tests_properties(compiler_pipeline PROPERTIES TIMEOUT 180)
    add_test(NAME bsp_validation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsp_validation.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/bsp-validation)
    set_tests_properties(bsp_validation PROPERTIES TIMEOUT 180)
    add_executable(decompile_math_test tests/decompile_math.cpp)
    target_include_directories(decompile_math_test PRIVATE libs)
    add_test(NAME decompile_math COMMAND decompile_math_test)
    add_test(NAME decompile_recovery COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/decompile.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/decompile)
    set_tests_properties(decompile_recovery PROPERTIES TIMEOUT 90)
    add_executable(job_pool_test tests/job_pool.cpp)
    target_link_libraries(job_pool_test PRIVATE q3mapx_jobs)
    add_test(NAME job_pool COMMAND job_pool_test)
    set_tests_properties(job_pool PROPERTIES TIMEOUT 60)
    add_test(NAME cli_options COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_options.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/cli-options)
    set_tests_properties(cli_options PROPERTIES TIMEOUT 90)
    add_executable(atomic_file_test tests/atomic_file.cpp)
    target_link_libraries(atomic_file_test PRIVATE q3mapx_io)
    add_test(NAME atomic_file COMMAND atomic_file_test ${CMAKE_BINARY_DIR}/tests/atomic-file)
    add_executable(columns_test tests/columns.cpp)
    target_link_libraries(columns_test PRIVATE q3mapx_compute)
    add_test(NAME columns COMMAND columns_test)
    add_test(NAME minimap COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/minimap.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/minimap)
    set_tests_properties(minimap PROPERTIES TIMEOUT 90)
endif()

include(GNUInstallDirs)
install(TARGETS q3mapx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES COPYING GPL LGPL LICENSE CONTRIBUTORS DESTINATION ${CMAKE_INSTALL_DATADIR}/q3mapx)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/q3mapx/licenses)
