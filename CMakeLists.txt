cmake_minimum_required(VERSION 3.15)
project(NAPROCK_Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 1. CUDA Setup ─────────────────────────────────────────────────────────────
option(USE_CUDA "Enable CUDA GPU acceleration" OFF)

if(USE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # 75=Turing, 80=Ampere, 86=RTX30xx, 89=RTX40xx — remove SMs you don't have
    set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89)
endif()

# ── 2. OpenMP ─────────────────────────────────────────────────────────────────
if(MSVC)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
else()
    find_package(OpenMP REQUIRED)
endif()

# ── 3. vcpkg Dependencies ─────────────────────────────────────────────────────
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ── 4. Define Targets ─────────────────────────────────────────────────────────
if(USE_CUDA)
    add_executable(app       main-v1.1.2.cpp      cuda_beam_search.cu)
    add_executable(app_v111  main-v1.1.1.cpp      cuda_beam_search.cu)
    add_executable(app_v120  main-v1.1.1_cuda.cpp cuda_beam_search.cu)
    add_executable(app_alter alter_main.cpp        cuda_beam_search.cu)
else()
    add_executable(app       main-v1.1.2.cpp)
    add_executable(app_v111  main-v1.1.1.cpp)
    add_executable(app_v120  main-v1.1.1_cuda.cpp)
    add_executable(app_alter alter_main.cpp)
endif()

# ── 5. Optimization Flags ─────────────────────────────────────────────────────
# MSVC flags guarded to CXX only — nvcc never sees these
set(MSVC_OPTIMIZATIONS
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Ox>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/fp:fast>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Oi>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Ot>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/GT>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/GL>
)

# nvcc optimization flags (only applied to .cu files)
set(NVCC_OPTIMIZATIONS
    $<$<COMPILE_LANGUAGE:CUDA>:-O3>
    $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Ox>
    $<$<COMPILE_LANGUAGE:CUDA>:--ptxas-options=-v>
)

# ── 6. Per-target settings ────────────────────────────────────────────────────
foreach(target app app_v111 app_v120 app_alter)
    target_include_directories(${target} PRIVATE .)

    # Forward USE_CUDA to preprocessor — fixes IntelliSense greying out #ifdef blocks
    if(USE_CUDA)
        target_compile_definitions(${target} PRIVATE USE_CUDA)
    endif()

    target_compile_options(${target} PRIVATE ${MSVC_OPTIMIZATIONS})

    if(USE_CUDA)
        target_compile_options(${target} PRIVATE ${NVCC_OPTIMIZATIONS})
        set_target_properties(${target} PROPERTIES
            CUDA_SEPARABLE_COMPILATION ON
        )
    endif()

    target_link_libraries(${target} PRIVATE
        cpr::cpr
        nlohmann_json::nlohmann_json
        OpenMP::OpenMP_CXX
    )

    if(MSVC)
        target_link_options(${target} PRIVATE /LTCG)
    endif()
endforeach()

# ── 7. Status report ──────────────────────────────────────────────────────────
message(STATUS "----------------------------------------")
message(STATUS "CUDA acceleration : ${USE_CUDA}")
if(USE_CUDA)
    message(STATUS "CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")