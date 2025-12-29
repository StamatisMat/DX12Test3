cmake_minimum_required(VERSION 3.20)
project(D3D12Test3 CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(D3D12_VERSION "1.618.5")
set(SLANG_VERSION "2025.19.1")

include(FetchContent)

# =============
# Slang Library 
# =============

if(WIN32)
    set(SLANG_ASSET_NAME "slang-${SLANG_VERSION}-windows-x86_64.zip")
else()
    message(FATAL_ERROR "Unsupported platform for DirectX binaries for now. Compile for WIN32 and use Wine.")
endif()

set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_ASSET_NAME}")

message(STATUS "Downloading Slang ${SLANG_VERSION} from ${SLANG_URL}...")

FetchContent_Declare(
    slang_binaries
    URL ${SLANG_URL}
)
FetchContent_MakeAvailable(slang_binaries)

add_library(slang::slang SHARED IMPORTED GLOBAL)

set(SLANG_ROOT "${slang_binaries_SOURCE_DIR}")

target_include_directories(slang::slang INTERFACE "${SLANG_ROOT}/include")

if(WIN32)
    set_target_properties(slang::slang PROPERTIES
        IMPORTED_IMPLIB "${SLANG_ROOT}/lib/slang.lib"
        IMPORTED_LOCATION "${SLANG_ROOT}/bin/slang.dll"
    )
endif()

# ===============
# DirectX Headers
# ===============

#FetchContent_Declare(
#    directx_headers
#    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
#    GIT_TAG     v1.618.5
#)
#FetchContent_MakeAvailable(directx_headers)
#
#set(DIRECTX_HEADERS_ROOT "${directx_headers_SOURCE_DIR}/include"
#
#target_include_directories(D3D12Test3 PRIVATE "${DIRECTX_HEADERS_ROOT}")

# ==========
# D3D12 SDK
# ==========

set(D3D12_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${D3D12_VERSION}")

message(STATUS "Downloading D3D12 ${D3D12_VERSION} SDK from ${D3D12_URL}...")

FetchContent_Declare(d3d12_nuget 
                    URL ${D3D12_URL}
                    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

#FetchContent_Populate(d3d12_nuget)
FetchContent_MakeAvailable(d3d12_nuget)

set(D3D12_ROOT "${d3d12_nuget_SOURCE_DIR}")
set(D3D12_INCLUDE "${d3d12_nuget_SOURCE_DIR}/build/native/include")
set(D3D12_BIN_DIR "${d3d12_nuget_SOURCE_DIR}/build/native/bin/x64")

# ==================
# Other dependencies
# ==================

set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(LIBRARIES_DIR "${CMAKE_SOURCE_DIR}/lib")

# SOURCE COLLECTION
file(GLOB_RECURSE SRC_FILES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/src/*.slang" # Just to appear on the ide
)

add_executable(D3D12Test3 ${SRC_FILES})
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT D3D12Test3)

target_include_directories(D3D12Test3 PRIVATE "${D3D12_INCLUDE}" "${INCLUDE_DIR}")
target_link_directories(D3D12Test3 PRIVATE "${LIBRARIES_DIR}")
target_link_libraries(D3D12Test3 PRIVATE slang::slang glfw3 d3d12 dxgi d3dcompiler)

if(WIN32)
    set_target_properties(D3D12Test3 PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:D3D12Test3>")
    add_custom_command(TARGET D3D12Test3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SLANG_ROOT}/bin/slang.dll"
        $<TARGET_FILE_DIR:D3D12Test3>
        COMMENT "Copying Slang DLLs to output directory..."
    )
    add_custom_command(TARGET D3D12Test3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:D3D12Test3>/D3D12
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${D3D12_BIN_DIR}/D3D12Core.dll"
            $<TARGET_FILE_DIR:D3D12Test3>/D3D12
        COMMENT "Copying D3D12Core.dll (Agility) to output directory..."
    )
endif()
add_custom_command(TARGET D3D12Test3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/shaders
    $<TARGET_FILE_DIR:D3D12Test3>/shaders
    COMMENT "Copying slang shaders to output directory"
)

# ALWAYS /permissive- on MSVC
if (MSVC)
    target_compile_options(D3D12Test3 PRIVATE /W4 /permissive-)
endif()