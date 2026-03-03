cmake_minimum_required(VERSION 3.15)
project(NAPROCK_Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
    set(OpenMP_CXX_LIB_NAMES omp)
    set(OpenMP_omp_LIBRARY /opt/homebrew/opt/libomp/lib/libomp.dylib)
endif()

find_package(OpenMP REQUIRED)

if(MSVC)
    add_compile_options(/Ox /fp:fast /Oi /Ot /GT /GL)
    add_link_options(/LTCG)
else()
    add_compile_options(-Ofast -march=native -flto)
endif()

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_library(METAL_LIBRARY Metal)
find_library(FOUNDATION_LIBRARY Foundation)

add_executable(app main-v1.1.2.cpp metal_beam_search.mm)

target_include_directories(app PRIVATE .)

target_link_libraries(app PRIVATE 
    cpr::cpr 
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
    ${METAL_LIBRARY}
    ${FOUNDATION_LIBRARY}
)

add_executable(app_v111 main-v1.1.1.cpp metal_beam_search.mm)
target_include_directories(app_v111 PRIVATE .)
target_link_libraries(app_v111 PRIVATE 
    cpr::cpr 
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
    ${METAL_LIBRARY}
    ${FOUNDATION_LIBRARY}
)

# Optimized v1.2.0 (Zobrist hashing + delta-undo + incremental scoring)
add_executable(app_v120 main-v1.2.0.cpp)

target_include_directories(app_v120 PRIVATE .)

target_link_libraries(app_v120 PRIVATE
    cpr::cpr
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
)

