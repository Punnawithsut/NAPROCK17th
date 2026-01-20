# NAPROCK17th

---

# C++ API Client (CPR & JSON)

This project demonstrates how to make HTTP POST requests using the `cpr` library and handle JSON data with `nlohmann/json`. It uses **vcpkg** as a package manager and **CMake** as the build system.

## 1. Prerequisites

### Windows

* **Visual Studio 2022** (with "Desktop development with C++" workload).
* **Git** installed.
* **vcpkg** installed. (Set the system environment to vcpkg.exe)
* **CMake** installed.

### macOS

* **Xcode Command Line Tools**: Install via `xcode-select --install`.
* **Homebrew**: (Optional, but recommended for installing CMake).
* **vcpkg** installed (usually in `~/vcpkg`).
* * **CMake** installed.

---

## 2. Install Dependencies

Before building, you need to download the required libraries using vcpkg. Run these commands in your terminal:

```bash
vcpkg install cpr nlohmann-json
```

---

## 3. Project Configuration Files

Ensure your `CMakeLists.txt` is set up to find these packages:

```cmake
cmake_minimum_required(VERSION 3.15)
project(NAPROCK_Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Ox /fp:fast /Oi /Ot /GT /GL)
    add_link_options(/LTCG)
else()
    add_compile_options(-Ofast -march=native -flto)
endif()

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(app Frame_Strategy.cpp)

target_include_directories(app PRIVATE .)
target_link_libraries(app PRIVATE cpr::cpr nlohmann_json::nlohmann_json)
```

---

## 4. Build Instructions

### **Windows (PowerShell/CMD)**

To build:

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="PATH_TO_YOUR_VCPKG_TOOL_CHAIN"
#PATH_TO_YOUR_VCPKG_TOOL_CHAIN = ~/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

To run:
```powershell
./build/Release/app.exe
```

### **macOS (Terminal)**

On macOS, you will likely point to the vcpkg path in your home directory:

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="PATH_TO_YOUR_VCPKG_TOOL_CHAIN"
#PATH_TO_YOUR_VCPKG_TOOL_CHAIN = ~/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

```bash
./build/app
```

---

## Troubleshooting

* **Missing OpenSSL (Linux/macOS):** `cpr` requires SSL. If the build fails, install it via `brew install openssl` (macOS) or `sudo apt install libssl-dev` (Linux).
* **Path with spaces:** If your Windows username has a space (like "Punnawith Sutisukon"), always wrap the `-DCMAKE_TOOLCHAIN_FILE` path in **double quotes**.
* **Architecture:** If you are on an M1/M2 Mac, ensure your vcpkg triplet matches (usually `arm64-osx`).

---

**Would you like me to help you write a `.gitignore` file to ensure you don't accidentally commit your build folders and binaries?**
