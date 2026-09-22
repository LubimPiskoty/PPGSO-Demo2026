cmake_minimum_required(VERSION 4.0)
project(semestralka CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#
# Dependencies
#

# OpenGL, GLEW and OpenMP are expected to be installed system-wide
# (CMake ships FindOpenGL/FindGLEW/FindOpenMP, so no custom module needed)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenMP)

# GLFW, GLM, ASSIMP and cJSON are fetched and built from source instead
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE)

FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# We only ever load embedded .glb/.gltf models, so skip building every other
# importer and skip exporting entirely to keep the binary size down.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.4.3
        GIT_SHALLOW TRUE)

FetchContent_Declare(cmrc
        GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
        GIT_TAG 2.0.1
        GIT_SHALLOW TRUE)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.18
        GIT_SHALLOW TRUE)

# cmrc 2.0.1 predates CMake 4.0's removal of cmake_minimum_required(VERSION < 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(glfw glm assimp cmrc cjson)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# cJSON's own CMakeLists.txt doesn't call target_include_directories(), so
# without this its header isn't visible to anything linking against it.
target_include_directories(cjson PUBLIC $<BUILD_INTERFACE:${cjson_SOURCE_DIR}>)

# Dear ImGui is a debug-only dependency (in-scene overlay/inspector). It has
# no CMakeLists.txt of its own, so it's fetched as raw source and built into
# a small static lib by hand, and only in Debug builds - Release never fetches,
# builds, or links it, and main.cpp's ImGui calls are compiled out via the
# WITH_IMGUI define (see #ifdef WITH_IMGUI usage there).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.5
            GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
endif()

# stb_image is a single header with no CMakeLists.txt of its own, so it's
# just downloaded directly instead of going through FetchContent.
set(STB_IMAGE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/_stb/stb_image.h")
if(NOT EXISTS ${STB_IMAGE_HEADER})
    file(DOWNLOAD
            https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
            ${STB_IMAGE_HEADER}
            STATUS STB_IMAGE_DOWNLOAD_STATUS)
    list(GET STB_IMAGE_DOWNLOAD_STATUS 0 STB_IMAGE_DOWNLOAD_CODE)
    if(NOT STB_IMAGE_DOWNLOAD_CODE EQUAL 0)
        file(REMOVE ${STB_IMAGE_HEADER})
        message(FATAL_ERROR "Failed to download stb_image.h: ${STB_IMAGE_DOWNLOAD_STATUS}")
    endif()
endif()

#
# Resources
#
file(GLOB SHADER_FILES CONFIGURE_DEPENDS "res/*.vs" "res/*.fs")
cmrc_add_resource_library(shaders
        NAMESPACE shaders
        WHENCE res
        ${SHADER_FILES})

file(GLOB MODEL_FILES CONFIGURE_DEPENDS "res/*.glb" "res/*.gltf")
cmrc_add_resource_library(models
        NAMESPACE models
        WHENCE res
        ${MODEL_FILES})

#
# Target
#
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(semestralka ${SOURCES})

target_compile_definitions(semestralka PUBLIC GLM_FORCE_RADIANS GLEW_STATIC
        GLM_ENABLE_EXPERIMENTAL)

target_include_directories(semestralka PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/_stb)

target_link_libraries(semestralka PUBLIC
        shaders
        models
        OpenGL::GL
        GLEW::GLEW
        glfw
        glm::glm
        assimp
        cjson)

if(OpenMP_CXX_FOUND)
    target_link_libraries(semestralka PUBLIC OpenMP::OpenMP_CXX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(semestralka PUBLIC WITH_IMGUI)
    target_link_libraries(semestralka PUBLIC imgui)
endif()

#
# Installation
#
install(TARGETS semestralka DESTINATION .)
