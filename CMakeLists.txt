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
        GIT_TAG 3.4)

FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1)

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
        GIT_TAG v5.4.3)

FetchContent_Declare(cmrc
        GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
        GIT_TAG 2.0.1)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.18)

# cmrc 2.0.1 predates CMake 4.0's removal of cmake_minimum_required(VERSION < 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(glfw glm assimp cmrc cjson)
unset(CMAKE_POLICY_VERSION_MINIMUM)

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

target_compile_definitions(semestralka PUBLIC GLM_FORCE_RADIANS GLEW_STATIC)

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

#
# Installation
#
install(TARGETS semestralka DESTINATION .)
