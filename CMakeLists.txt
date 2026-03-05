cmake_minimum_required(VERSION 3.15)
project(NAPROCK_Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. CUDA Setup
option(USE_CUDA "Enable CUDA GPU acceleration" OFF)

if(USE_CUDA)
    enable_language(CUDA)
    add_compile_definitions(USE_CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES 75 80 86)
endif()

# 2. OpenMP Fix (Avoids OpenMP_CUDA error)
if(MSVC)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
else()
    find_package(OpenMP REQUIRED)
endif()

# 3. vcpkg Dependencies
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# 4. Define Targets
if(USE_CUDA)
    add_executable(app      main-v1.1.2.cpp      cuda_beam_search.cu)
    add_executable(app_v111 main-v1.1.1.cpp      cuda_beam_search.cu)
    # ADD THE .cu FILE HERE TOO:
    add_executable(app_v120 main-v1.1.1_cuda.cpp cuda_beam_search.cu) 
else()
    add_executable(app      main-v1.1.2.cpp)
    add_executable(app_v111 main-v1.1.1.cpp)
    add_executable(app_v120 main-v1.1.1_cuda.cpp)
endif()

# 5. Optimization Flags & Linking
# This block ensures MSVC optimizations DO NOT touch the CUDA compiler (nvcc)
set(MSVC_OPTIMIZATIONS 
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Ox>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/fp:fast>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Oi>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Ot>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/GT>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/GL>
)

foreach(target app app_v111 app_v120)
    target_include_directories(${target} PRIVATE .)
    
    # Apply the optimizations safely
    target_compile_options(${target} PRIVATE ${MSVC_OPTIMIZATIONS})
    
    # Link libraries
    target_link_libraries(${target} PRIVATE 
        cpr::cpr 
        nlohmann_json::nlohmann_json 
        OpenMP::OpenMP_CXX
    )
    
    # Global link-time code generation for MSVC
    if(MSVC)
        target_link_options(${target} PRIVATE /LTCG)
    endif()
endforeach()