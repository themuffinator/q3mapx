cmake_minimum_required(VERSION 3.25)
project(q3mapx VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(Q3MAPX_ENABLE_LTO "Enable interprocedural optimization in release builds" OFF)
option(Q3MAPX_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(Q3MAPX_BUILD_BENCHMARKS "Build standalone performance measurement tools" OFF)
option(Q3MAPX_ENABLE_OPENCL "Enable optional dynamically loaded OpenCL GPU compute" ON)
option(Q3MAPX_BUILD_GUI "Build the Qt 6 desktop workbench" ON)

# Instrument first-party support libraries and regression targets as well as the
# executable. Instrumenting only q3mapx misses defects inside static libraries.
if(Q3MAPX_ENABLE_SANITIZERS)
    if(MINGW OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ASan/UBSan preset requires GCC/Clang on Linux; MinGW/MSVC runtimes are not supported by this preset")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

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
add_library(q3mapx_io STATIC libs/q3mapx/atomic_file.cpp libs/q3mapx/portal_graph.cpp
    libs/q3mapx/cell_graph.cpp libs/q3mapx/exact_predicates.cpp libs/q3mapx/planar_reduction.cpp
    libs/q3mapx/quake3_materials.cpp libs/q3mapx/uv_fit.cpp libs/q3mapx/bezier_uv.cpp)
target_include_directories(q3mapx_io PUBLIC libs)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/libs/q3mapx/columns.cl Q3MAPX_COLUMN_KERNEL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS libs/q3mapx/columns.cl)
configure_file(cmake/columns_kernel.h.in generated/columns_kernel.h @ONLY)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/libs/q3mapx/area_factors.cl Q3MAPX_AREA_KERNEL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS libs/q3mapx/area_factors.cl)
configure_file(cmake/area_factors_kernel.h.in generated/area_factors_kernel.h @ONLY)
add_library(q3mapx_compute STATIC libs/q3mapx/columns.cpp libs/q3mapx/compute.cpp)
target_include_directories(q3mapx_compute PUBLIC libs PRIVATE libs/thirdparty/opencl ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(q3mapx_compute PRIVATE Q3MAPX_ENABLE_OPENCL=$<BOOL:${Q3MAPX_ENABLE_OPENCL}>)
target_link_libraries(q3mapx_compute PRIVATE ${CMAKE_DL_LIBS})
if(NOT MSVC)
    target_compile_options(q3mapx_compute PRIVATE -ffp-contract=off)
endif()

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
    add_test(NAME game_profiles COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/game_profiles.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/game-profiles)
    set_tests_properties(game_profiles PROPERTIES TIMEOUT 360)
    add_test(NAME compiler_pipeline COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/integration)
    set_tests_properties(compiler_pipeline PROPERTIES TIMEOUT 180)
    add_test(NAME lighting_materials COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lighting.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/lighting)
    set_tests_properties(lighting_materials PROPERTIES TIMEOUT 180)
    add_test(NAME bsp_validation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsp_validation.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/bsp-validation)
    set_tests_properties(bsp_validation PROPERTIES TIMEOUT 180)
    add_executable(index_validation_test tests/index_validation.cpp)
    target_include_directories(index_validation_test PRIVATE libs)
    add_test(NAME index_validation COMMAND index_validation_test)
    set_tests_properties(index_validation PROPERTIES TIMEOUT 120)
    add_test(NAME index_validation_cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/index_validation.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/index-validation)
    set_tests_properties(index_validation_cli PROPERTIES TIMEOUT 180)
    add_test(NAME bsp_inspect COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsp_inspect.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/bsp-inspect)
    set_tests_properties(bsp_inspect PROPERTIES TIMEOUT 240)
    add_test(NAME bsp_analyze COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsp_analyze.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/bsp-analyze)
    set_tests_properties(bsp_analyze PROPERTIES TIMEOUT 90)
    add_test(NAME bsp_evidence COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bsp_evidence.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/bsp-evidence)
    set_tests_properties(bsp_evidence PROPERTIES TIMEOUT 240)
    add_test(NAME lighting_evidence COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lighting_evidence.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/lighting-evidence)
    set_tests_properties(lighting_evidence PROPERTIES TIMEOUT 300)
    add_executable(bezier_uv_test tests/bezier_uv.cpp)
    target_link_libraries(bezier_uv_test PRIVATE q3mapx_io)
    add_test(NAME bezier_uv COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/bezier_uv.py --unit $<TARGET_FILE:bezier_uv_test>
        --work-dir ${CMAKE_BINARY_DIR}/tests/bezier-uv)
    set_tests_properties(bezier_uv PROPERTIES TIMEOUT 180)
    add_test(NAME lighting_curves COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lighting_curves.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/lighting-curves)
    set_tests_properties(lighting_curves PROPERTIES TIMEOUT 600)
    add_test(NAME light_probes COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/light_probes.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/light-probes)
    set_tests_properties(light_probes PROPERTIES TIMEOUT 600)
    add_test(NAME light_comparison COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/light_comparison.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/light-comparison)
    set_tests_properties(light_comparison PROPERTIES TIMEOUT 600)
    add_test(NAME point_fitting COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/point_fitting.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/point-fitting)
    set_tests_properties(point_fitting PROPERTIES TIMEOUT 900)
    add_test(NAME spot_fitting COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/spot_fitting.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/spot-fitting)
    set_tests_properties(spot_fitting PROPERTIES TIMEOUT 1800)
    if(Q3MAPX_ENABLE_SANITIZERS)
        set_tests_properties(spot_fitting PROPERTIES TIMEOUT 5400)
    endif()
    add_test(NAME light_recovery COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/light_recovery.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/light-recovery)
    set_tests_properties(light_recovery PROPERTIES TIMEOUT 1800)
    add_executable(ftx_test tests/ftx.cpp)
    target_include_directories(ftx_test PRIVATE libs)
    add_test(NAME ftx COMMAND ftx_test)
    add_executable(terrain_test tests/terrain.cpp)
    target_include_directories(terrain_test PRIVATE libs)
    add_test(NAME terrain COMMAND terrain_test)
    add_test(NAME native_fakk COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_fakk.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/native-fakk)
    set_tests_properties(native_fakk PROPERTIES TIMEOUT 180)
    add_test(NAME native_mohaa COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_mohaa.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/native-mohaa)
    set_tests_properties(native_mohaa PROPERTIES TIMEOUT 180)
    add_test(NAME native_early COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_early.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/tests/native_early)
    set_tests_properties(native_early PROPERTIES TIMEOUT 240)
    add_test(NAME mesh_export COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/mesh_export.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/tests/mesh_export)
    set_tests_properties(mesh_export PROPERTIES TIMEOUT 240)
    add_test(NAME export_outputs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/export_outputs.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/tests/export_outputs)
    set_tests_properties(export_outputs PROPERTIES TIMEOUT 180)
    add_test(NAME recovery_outputs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/recovery_outputs.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/tests/recovery_outputs)
    set_tests_properties(recovery_outputs PROPERTIES TIMEOUT 180)
    add_executable(decompile_math_test tests/decompile_math.cpp)
    target_include_directories(decompile_math_test PRIVATE libs)
    add_test(NAME decompile_math COMMAND decompile_math_test)
    add_executable(uv_fit_test tests/uv_fit.cpp)
    target_link_libraries(uv_fit_test PRIVATE q3mapx_io)
    add_test(NAME uv_fit COMMAND uv_fit_test)
    add_test(NAME uv_fit_oracle COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/uv_fit.py --unit $<TARGET_FILE:uv_fit_test>
        --work-dir ${CMAKE_BINARY_DIR}/tests/uv-fit)
    set_tests_properties(uv_fit_oracle PROPERTIES TIMEOUT 120)
    add_test(NAME uv_recovery COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/uv_recovery.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/uv-recovery)
    set_tests_properties(uv_recovery PROPERTIES TIMEOUT 600)
    add_test(NAME uv_output COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/uv_output.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/uv-output)
    set_tests_properties(uv_output PROPERTIES TIMEOUT 300)
    add_executable(lighting_math_test tests/lighting_math.cpp tools/quake3/q3map2/lighting_math.cpp)
    target_include_directories(lighting_math_test PRIVATE libs tools/quake3/common tools/quake3/q3map2)
    add_test(NAME lighting_math COMMAND lighting_math_test)
    add_executable(area_factors_test tests/area_factors.cpp tools/quake3/q3map2/lighting_math.cpp)
    target_include_directories(area_factors_test PRIVATE tools/quake3/common tools/quake3/q3map2)
    target_link_libraries(area_factors_test PRIVATE q3mapx_compute)
    add_test(NAME area_factors COMMAND area_factors_test)
    set_tests_properties(area_factors PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_test(NAME area_factors_disabled COMMAND area_factors_test --disabled)
    set_tests_properties(area_factors_disabled PROPERTIES TIMEOUT 30 ENVIRONMENT "Q3MAPX_DISABLE_GPU=1")
    add_test(NAME lighting_gpu COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lighting_gpu.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/lighting-gpu)
    set_tests_properties(lighting_gpu PROPERTIES TIMEOUT 180)
    add_test(NAME decompile_recovery COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/decompile.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/decompile)
    set_tests_properties(decompile_recovery PROPERTIES TIMEOUT 90)
    add_test(NAME recovery_classification COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/recovery_classification.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/recovery-classification)
    set_tests_properties(recovery_classification PROPERTIES TIMEOUT 300)
    add_test(NAME recovery_order COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/recovery_order.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/tests/recovery-order)
    set_tests_properties(recovery_order PROPERTIES TIMEOUT 360)
    add_test(NAME brush_cells COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/brush_cells.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/brush-cells)
    set_tests_properties(brush_cells PROPERTIES TIMEOUT 360)
    add_executable(recovery_group_order_test tests/recovery_group_order.cpp tools/quake3/q3map2/recovery_groups.cpp)
    target_include_directories(recovery_group_order_test PRIVATE tools/quake3/q3map2)
    add_test(NAME recovery_group_order COMMAND recovery_group_order_test)
    set_tests_properties(recovery_group_order PROPERTIES TIMEOUT 30)
    add_test(NAME recovery_groups COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/recovery_groups.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/recovery-groups)
    set_tests_properties(recovery_groups PROPERTIES TIMEOUT 360)
    add_executable(job_pool_test tests/job_pool.cpp)
    target_link_libraries(job_pool_test PRIVATE q3mapx_jobs)
    add_test(NAME job_pool COMMAND job_pool_test)
    set_tests_properties(job_pool PROPERTIES TIMEOUT 60)
    add_executable(lightgrid_test tests/lightgrid.cpp)
    target_include_directories(lightgrid_test PRIVATE libs)
    add_test(NAME lightgrid COMMAND lightgrid_test)
    set_tests_properties(lightgrid PROPERTIES TIMEOUT 60)
    add_test(NAME lightgrid_cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lightgrid_cli.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/lightgrid)
    set_tests_properties(lightgrid_cli PROPERTIES TIMEOUT 60)
    add_test(NAME raven_compat COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/raven_compat.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/raven-compat)
    set_tests_properties(raven_compat PROPERTIES TIMEOUT 90)
    add_test(NAME cli_options COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_options.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/cli-options)
    set_tests_properties(cli_options PROPERTIES TIMEOUT 90)
    add_test(NAME cli_numbers COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_numbers.py --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/cli-numbers)
    set_tests_properties(cli_numbers PROPERTIES TIMEOUT 300)
    add_executable(atomic_file_test tests/atomic_file.cpp)
    target_link_libraries(atomic_file_test PRIVATE q3mapx_io)
    add_test(NAME atomic_file COMMAND atomic_file_test ${CMAKE_BINARY_DIR}/tests/atomic-file)
    add_test(NAME binary_outputs COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/binary_outputs.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/binary-outputs)
    set_tests_properties(binary_outputs PROPERTIES TIMEOUT 120)
    add_executable(columns_test tests/columns.cpp)
    target_link_libraries(columns_test PRIVATE q3mapx_compute)
    add_test(NAME columns COMMAND columns_test)
    add_test(NAME minimap COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/minimap.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/minimap)
    set_tests_properties(minimap PROPERTIES TIMEOUT 90)
    add_test(NAME gpu_columns COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu.py
        --compiler $<TARGET_FILE:q3mapx>
        --work-dir ${CMAKE_BINARY_DIR}/tests/gpu)
    set_tests_properties(gpu_columns PROPERTIES TIMEOUT 180)
    add_test(NAME portal_validation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_validation.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/portals)
    set_tests_properties(portal_validation PROPERTIES TIMEOUT 90)
    add_executable(portal_graph_test tests/portal_graph.cpp)
    target_link_libraries(portal_graph_test PRIVATE q3mapx_io)
    add_test(NAME portal_graph COMMAND portal_graph_test)
    add_executable(cell_graph_test tests/cell_graph.cpp)
    target_link_libraries(cell_graph_test PRIVATE q3mapx_io)
    add_test(NAME cell_graph COMMAND cell_graph_test)
    set_tests_properties(cell_graph PROPERTIES TIMEOUT 60)
    add_executable(planar_reduction_test tests/planar_reduction.cpp)
    target_link_libraries(planar_reduction_test PRIVATE q3mapx_io)
    add_test(NAME planar_reduction COMMAND planar_reduction_test)
    set_tests_properties(planar_reduction PROPERTIES TIMEOUT 180)
    add_test(NAME planar_reduction_oracle COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/planar_reduction.py
        --unit $<TARGET_FILE:planar_reduction_test> --work-dir ${CMAKE_BINARY_DIR}/tests/planar-reduction)
    set_tests_properties(planar_reduction_oracle PROPERTIES TIMEOUT 240)
    add_test(NAME planar_reduction_native COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/planar_reduction_native.py
        --compiler $<TARGET_FILE:q3mapx> --unit $<TARGET_FILE:planar_reduction_test>
        --work-dir ${CMAKE_BINARY_DIR}/tests/planar-reduction-native)
    set_tests_properties(planar_reduction_native PROPERTIES TIMEOUT 180)
    add_executable(quake3_materials_test tests/quake3_materials.cpp)
    target_link_libraries(quake3_materials_test PRIVATE q3mapx_io)
    add_test(NAME quake3_materials COMMAND quake3_materials_test)
    add_test(NAME geometry_optimize COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/geometry_optimize.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/geometry-optimize)
    set_tests_properties(geometry_optimize PROPERTIES TIMEOUT 300)
    add_test(NAME meta_order COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/meta_order.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/meta-order)
    set_tests_properties(meta_order PROPERTIES TIMEOUT 360)
    add_test(NAME patch_input COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/patch_input.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/patch-input)
    set_tests_properties(patch_input PROPERTIES TIMEOUT 360)
    add_test(NAME script_input COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/script_input.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/script-input)
    set_tests_properties(script_input PROPERTIES TIMEOUT 360)
    add_test(NAME brush_input COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/brush_input.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/brush-input)
    set_tests_properties(brush_input PROPERTIES TIMEOUT 600)
    add_test(NAME cell_adjacency COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cell_adjacency.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/cell-adjacency)
    set_tests_properties(cell_adjacency PROPERTIES TIMEOUT 360)
    add_executable(vis_mask_test tests/vis_mask.cpp)
    target_include_directories(vis_mask_test PRIVATE libs)
    add_test(NAME vis_mask COMMAND vis_mask_test)
    set_tests_properties(vis_mask PROPERTIES TIMEOUT 30)
    add_executable(vis_clip_test tests/vis_clip.cpp)
    target_include_directories(vis_clip_test PRIVATE libs)
    if(NOT MSVC)
        target_compile_options(vis_clip_test PRIVATE -fno-strict-aliasing)
    endif()
    add_test(NAME vis_clip COMMAND vis_clip_test)
    set_tests_properties(vis_clip PROPERTIES TIMEOUT 60)
    add_test(NAME vis_passage_clipping COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vis_passage_clipping.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/vis-passage-clipping)
    # Hundreds of real compiler starts are substantially slower under ASan.
    # Individual child processes still have their own 30-second deadline.
    set_tests_properties(vis_passage_clipping PROPERTIES TIMEOUT 600)
    add_test(NAME vis_passage_storage COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vis_passage_storage.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/vis-passage-storage)
    set_tests_properties(vis_passage_storage PROPERTIES TIMEOUT 180)
    add_test(NAME portal_evidence COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_evidence.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/portal-evidence)
    set_tests_properties(portal_evidence PROPERTIES TIMEOUT 180)
    add_test(NAME vis_merge COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vis_merge.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/vis-merge)
    set_tests_properties(vis_merge PROPERTIES TIMEOUT 360)
    add_test(NAME vis_reproducible COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vis_reproducible.py
        --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/vis-reproducible
        --grid $<IF:$<BOOL:${Q3MAPX_ENABLE_SANITIZERS}>,5,9>)
    set_tests_properties(vis_reproducible PROPERTIES TIMEOUT 180)
endif()

include(GNUInstallDirs)
if(Q3MAPX_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
    add_library(q3mapx_workbench_core STATIC apps/workbench/project.cpp apps/workbench/job_queue.cpp apps/workbench/job_queue.h
        apps/workbench/game_catalog.cpp apps/workbench/game_catalog.h
        apps/workbench/bsp_inspector.cpp apps/workbench/bsp_inspector.h
        apps/workbench/device_inventory.cpp apps/workbench/device_inventory.h
        apps/workbench/light_fit.cpp apps/workbench/light_fit.h)
    set_target_properties(q3mapx_workbench_core PROPERTIES AUTOMOC ON)
    target_include_directories(q3mapx_workbench_core PUBLIC apps/workbench)
    target_include_directories(q3mapx_workbench_core PRIVATE libs)
    target_link_libraries(q3mapx_workbench_core PUBLIC Qt6::Core)
    set(Q3MAPX_WORKBENCH_UI apps/workbench/window.cpp apps/workbench/window.h
        apps/workbench/inspection_page.cpp apps/workbench/inspection_page.h
        apps/workbench/hardware_page.cpp apps/workbench/hardware_page.h
        apps/workbench/light_recovery_page.cpp apps/workbench/light_recovery_page.h)
    qt_add_executable(q3mapx-workbench WIN32 apps/workbench/main.cpp ${Q3MAPX_WORKBENCH_UI})
    set_target_properties(q3mapx-workbench PROPERTIES AUTOMOC ON)
    target_link_libraries(q3mapx-workbench PRIVATE q3mapx_workbench_core Qt6::Widgets)
    target_compile_definitions(q3mapx-workbench PRIVATE Q3MAPX_VERSION="${PROJECT_VERSION}")
    install(TARGETS q3mapx-workbench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(BUILD_TESTING)
        add_executable(game_catalog_test tests/game_catalog.cpp)
        target_link_libraries(game_catalog_test PRIVATE q3mapx_workbench_core)
        add_test(NAME game_catalog COMMAND game_catalog_test $<TARGET_FILE:q3mapx>)
        set_tests_properties(game_catalog PROPERTIES TIMEOUT 45)
        add_executable(bsp_inspector_test tests/bsp_inspector.cpp)
        target_link_libraries(bsp_inspector_test PRIVATE q3mapx_workbench_core)
        add_test(NAME workbench_inspector COMMAND bsp_inspector_test $<TARGET_FILE:q3mapx> ${CMAKE_BINARY_DIR}/tests/workbench-inspector)
        set_tests_properties(workbench_inspector PROPERTIES TIMEOUT 60)
        add_executable(device_inventory_test tests/device_inventory.cpp)
        target_link_libraries(device_inventory_test PRIVATE q3mapx_workbench_core)
        add_test(NAME workbench_devices COMMAND device_inventory_test $<TARGET_FILE:q3mapx> ${CMAKE_BINARY_DIR}/tests/workbench-devices)
        set_tests_properties(workbench_devices PROPERTIES TIMEOUT 60)
        add_executable(workbench_test tests/workbench.cpp)
        target_link_libraries(workbench_test PRIVATE q3mapx_workbench_core)
        add_test(NAME workbench COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench.py
            --test $<TARGET_FILE:workbench_test> --compiler $<TARGET_FILE:q3mapx>
            --work-dir ${CMAKE_BINARY_DIR}/tests/workbench)
        set_tests_properties(workbench PROPERTIES TIMEOUT 120 FIXTURES_SETUP workbench_run)
        add_executable(workbench_window_test tests/workbench_window.cpp ${Q3MAPX_WORKBENCH_UI})
        set_target_properties(workbench_window_test PROPERTIES AUTOMOC ON)
        target_compile_definitions(workbench_window_test PRIVATE Q3MAPX_VERSION="${PROJECT_VERSION}")
        target_link_libraries(workbench_window_test PRIVATE q3mapx_workbench_core Qt6::Widgets)
        add_test(NAME workbench_window COMMAND workbench_window_test -platform offscreen
            ${CMAKE_BINARY_DIR}/tests/workbench/project.q3mapx.json
            ${CMAKE_BINARY_DIR}/tests/workbench/window-state $<TARGET_FILE:device_inventory_test>)
        set_tests_properties(workbench_window PROPERTIES TIMEOUT 45 FIXTURES_REQUIRED workbench_run)
        add_executable(workbench_lights_test tests/workbench_lights.cpp ${Q3MAPX_WORKBENCH_UI})
        set_target_properties(workbench_lights_test PROPERTIES AUTOMOC ON)
        target_compile_definitions(workbench_lights_test PRIVATE Q3MAPX_VERSION="${PROJECT_VERSION}")
        target_link_libraries(workbench_lights_test PRIVATE q3mapx_workbench_core Qt6::Widgets)
        add_test(NAME workbench_lights COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_lights.py --test $<TARGET_FILE:workbench_lights_test>
            --compiler $<TARGET_FILE:q3mapx> --work-dir ${CMAKE_BINARY_DIR}/tests/workbench-lights)
        set_tests_properties(workbench_lights PROPERTIES TIMEOUT 1800)
        add_test(NAME workbench_preview COMMAND q3mapx-workbench -platform offscreen
            --project ${CMAKE_BINARY_DIR}/tests/workbench/project.q3mapx.json
            --state-dir ${CMAKE_BINARY_DIR}/tests/workbench/ui-state
            --render-preview ${CMAKE_BINARY_DIR}/tests/workbench/workbench.png)
        set_tests_properties(workbench_preview PROPERTIES TIMEOUT 30 FIXTURES_REQUIRED workbench_run)
    endif()
endif()
install(TARGETS q3mapx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES COPYING GPL LGPL LICENSE CONTRIBUTORS DESTINATION ${CMAKE_INSTALL_DATADIR}/q3mapx)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/q3mapx/licenses)
install(FILES libs/thirdparty/opencl/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/q3mapx/licenses RENAME OpenCL-Headers.txt)
