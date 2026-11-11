cmake_minimum_required(VERSION 4.0)
project(semestralka CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# `make install` goes to build/dist unless a prefix is given (before the
# dependencies, they read it)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/dist CACHE PATH "" FORCE)
endif()

#
# Dependencies
#

# OpenGL and GLEW are expected to be installed system-wide
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# GLFW, GLM and ASSIMP are fetched and built from source
include(FetchContent)
set(FETCHCONTENT_QUIET OFF) # show git clone progress
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL)

FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL)

# Only embedded .glb models are loaded: just the glTF importer, no exporters
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.4.3
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL)

FetchContent_Declare(cmrc
        GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
        GIT_TAG 2.0.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL)

# cmrc 2.0.1 predates CMake 4.0's removal of cmake_minimum_required(VERSION < 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(glfw glm assimp cmrc)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# EXCLUDE_FROM_ALL above: only what the executable links gets built, and
# `make install` installs nothing of theirs

# Dear ImGui (the debug overlay) only in Debug: it has no CMakeLists.txt, so
# it's built into a small static lib here. Release never fetches it, the UI
# code is compiled out by WITH_IMGUI.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.5
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
        EXCLUDE_FROM_ALL)
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

# stb_image (loading) and stb_image_write (saving framebuffers) are single
# headers, downloaded directly
set(STB_DIR ${CMAKE_CURRENT_BINARY_DIR}/_stb)
foreach(STB_NAME stb_image stb_image_write)
    set(STB_HEADER ${STB_DIR}/${STB_NAME}.h)
    if(NOT EXISTS ${STB_HEADER})
        file(DOWNLOAD
                https://raw.githubusercontent.com/nothings/stb/master/${STB_NAME}.h
                ${STB_HEADER}
                SHOW_PROGRESS
                STATUS STB_STATUS)
        list(GET STB_STATUS 0 STB_CODE)
        if(NOT STB_CODE EQUAL 0)
            file(REMOVE ${STB_HEADER})
            message(FATAL_ERROR "Failed to download ${STB_NAME}.h: ${STB_STATUS}")
        endif()
    endif()
endforeach()

#
# Resources, embedded in the executable
#
# WHENCE strips the folder, so files are looked up by bare name
# (e.g. "default.vs", "HPF_Anvil.glb")
file(GLOB SHADERS_FILES CONFIGURE_DEPENDS
        "res/shaders/*.vs" "res/shaders/*.fs" "res/shaders/*.comp")
file(GLOB MODELS_FILES CONFIGURE_DEPENDS "res/models/*.glb")
file(GLOB TEXTURES_FILES CONFIGURE_DEPENDS
        "res/textures/*.png" "res/textures/*.jpg")

foreach(RES shaders models textures)
    string(TOUPPER ${RES} RES_UPPER)
    cmrc_add_resource_library(${RES}
            NAMESPACE ${RES}
            WHENCE res/${RES}
            ${${RES_UPPER}_FILES})
    # The resources are big byte arrays: in Debug, -g would copy each one
    # again into the debug info (the executable twice as big)
    if(NOT MSVC)
        target_compile_options(${RES} PRIVATE -g0)
    endif()
endforeach()

#
# Target
#
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(semestralka ${SOURCES})

target_compile_definitions(semestralka PRIVATE GLEW_STATIC GLM_ENABLE_EXPERIMENTAL)
target_include_directories(semestralka PRIVATE ${STB_DIR})
target_link_libraries(semestralka PRIVATE
        shaders models textures
        OpenGL::GL GLEW::GLEW glfw glm::glm assimp)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(semestralka PRIVATE WITH_IMGUI)
    target_link_libraries(semestralka PRIVATE imgui)
endif()

# Texture decoding (loading) and physics (every frame) are too slow
# unoptimized, keep them optimized in Debug too
file(GLOB PHYSICS_SOURCES CONFIGURE_DEPENDS "src/physics/*.cpp")
if(MSVC)
    set(FAST_FLAGS /O2)
else()
    set(FAST_FLAGS -O2)
endif()
set_source_files_properties(src/loaders/stb_image_impl.cpp ${PHYSICS_SOURCES}
        PROPERTIES COMPILE_OPTIONS ${FAST_FLAGS})

#
# Installation: `make install` (or `cmake --install .`) puts a stripped
# executable in the prefix (build/dist by default), packed with UPX when it's installed (LZMA, about 10%
# smaller: the textures are PNG/JPG already, little is left to squeeze)
#
option(PACK_UPX "Pack the installed executable with UPX" ON)
find_program(UPX_EXECUTABLE upx)
set(INSTALLED_EXE "\${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:semestralka>")
# The last packed copy would count as up to date and not be replaced
install(CODE "file(REMOVE \"${INSTALLED_EXE}\")")
install(TARGETS semestralka DESTINATION .)
# MSVC keeps the debug info in a .pdb, there is nothing to strip
if(CMAKE_STRIP AND NOT MSVC)
    install(CODE "execute_process(COMMAND \"${CMAKE_STRIP}\" \"${INSTALLED_EXE}\" COMMAND_ERROR_IS_FATAL ANY)")
endif()
if(PACK_UPX AND UPX_EXECUTABLE)
    install(CODE "
        message(STATUS \"Packing ${INSTALLED_EXE} with UPX (takes a few minutes)\")
        execute_process(COMMAND \"${UPX_EXECUTABLE}\" -q --lzma -f \"${INSTALLED_EXE}\" COMMAND_ERROR_IS_FATAL ANY)")
elseif(PACK_UPX)
    message(STATUS "UPX not found, the installed executable won't be packed")
endif()
