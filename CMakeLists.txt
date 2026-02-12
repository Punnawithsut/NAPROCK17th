cmake_minimum_required(VERSION 3.15)
project(NAPROCK_Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

if(MSVC)
    add_compile_options(/Ox /fp:fast /Oi /Ot /GT /GL)
    add_link_options(/LTCG)
else()
    add_compile_options(-Ofast -march=native -flto)
endif()

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(app editsmall.cpp)

target_include_directories(app PRIVATE .)

target_link_libraries(app PRIVATE 
    cpr::cpr 
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
)