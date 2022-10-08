cmake_minimum_required (VERSION 3.20)
project("game")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
configure_file(${CMAKE_SOURCE_DIR}/include/config.in.h ${CMAKE_BINARY_DIR}/include/config.h)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
message("CMake build type: ${CMAKE_BUILD_TYPE}")

# ---- Compiler options ----

if (MSVC)
    # Disable RTTI
    string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GR-")
    add_compile_options(
        /wd4267
        $<$<CONFIG:DEBUG>:/Od>
        $<$<CONFIG:DEBUG>:/Zi>
        $<$<CONFIG:RELEASE>:/O2>
    )
else(MSVC)
    string(REGEX REPLACE "-frtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
    add_compile_options(
        $<$<CONFIG:DEBUG>:-O0>
        $<$<CONFIG:DEBUG>:-ggdb>
        $<$<CONFIG:RELEASE>:-O3>
    )
endif()
add_compile_definitions(
    $<$<CONFIG:RELEASE>:NDEBUG>
)

# ---- CPM ----

set(CPM_DOWNLOAD_VERSION 0.35.5)
if(CPM_SOURCE_CACHE)
    # Expand relative path. This is important if the provided path contains a tilde (~)
    get_filename_component(CPM_SOURCE_CACHE ${CPM_SOURCE_CACHE} ABSOLUTE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif()
include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage(NAME glfw
              VERSION 3.3.4
              GITHUB_REPOSITORY glfw/glfw
              GIT_TAG 3.3.4
              OPTIONS "GLFW_BUILD_DOCS OFF"
                      "GLFW_BUILD_TESTS OFF"
                      "GLFW_BUILD_EXAMPLES OFF"
                      "GLFW_INSTALL OFF")
CPMAddPackage(NAME glm
              VERSION 0.9.9.8
              GITHUB_REPOSITORY g-truc/glm
              GIT_TAG 0.9.9.8)
CPMAddPackage(NAME miniz
              URL "https://github.com/richgel999/miniz/releases/download/2.2.0/miniz-2.2.0.zip"
              DOWNLOAD_ONLY TRUE)
CPMAddPackage(NAME physicsfs
              VERSION 3.0.2
              GITHUB_REPOSITORY icculus/physfs
              GIT_TAG release-3.0.2
              OPTIONS "PHYSFS_ARCHIVE_GRP FALSE"
                      "PHYSFS_ARCHIVE_WAD FALSE"
                      "PHYSFS_ARCHIVE_HOG FALSE"
                      "PHYSFS_ARCHIVE_MVL FALSE"
                      "PHYSFS_ARCHIVE_QPAK FALSE"
                      "PHYSFS_ARCHIVE_SLB FALSE"
                      "PHYSFS_ARCHIVE_ISO9660 FALSE"
                      "PHYSFS_ARCHIVE_VDF FALSE"
                      "PHYSFS_BUILD_SHARED FALSE"
                      "PHYSFS_BUILD_TEST FALSE"
                      "PHYSFS_DISABLE_INSTALL TRUE"
                      "PHYSFS_BUILD_DOCS FALSE")
CPMAddPackage(NAME spdlog
              VERSION 1.8.5
              GITHUB_REPOSITORY gabime/spdlog
              GIT_TAG v1.8.5)
CPMAddPackage(NAME spng
              VERSION 0.7.2
              GITHUB_REPOSITORY randy408/libspng
              GIT_TAG v0.7.2
              DOWNLOAD_ONLY TRUE)
CPMAddPackage(NAME spirvreflect
              GITHUB_REPOSITORY KhronosGroup/SPIRV-Reflect
              GIT_TAG b68b5a8a5d8ab5fce79e6596f3a731291046393a
              OPTIONS "SPIRV_REFLECT_EXECUTABLE OFF"
                      "SPIRV_REFLECT_EXAMPLES OFF"
                      "SPIRV_REFLECT_STATIC_LIB ON")
find_package(Vulkan REQUIRED)

add_compile_definitions(SPNG_STATIC)
include_directories(AFTER ${physicsfs_SOURCE_DIR}/src ${spirvreflect_SOURCE_DIR} ${spng_SOURCE_DIR}/spng)
include("deps.cmake")

# ---- Shaders ----
if (WIN32)
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
    else()
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
    endif()
else()
    find_program(GLSL_VALIDATOR glslangValidator
        PATHS /usr/local/bin)
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES "${CMAKE_SOURCE_DIR}/shaders/*.glsl")
foreach(GLSL ${GLSL_SOURCE_FILES})
    cmake_path(RELATIVE_PATH GLSL BASE_DIRECTORY "${CMAKE_SOURCE_DIR}/shaders" OUTPUT_VARIABLE GLSL_REL_PATH)
    cmake_path(REMOVE_EXTENSION GLSL_REL_PATH LAST_ONLY)
    set(SPIRV "${CMAKE_SOURCE_DIR}/resources/shaders/${GLSL_REL_PATH}.spv")
    add_custom_command(OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/resources/shaders/"
        COMMAND ${GLSL_VALIDATOR} -o ${SPIRV} -V ${GLSL} --quiet
        DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()
add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})

# ---- Mipmap Generator ----
add_executable(mipmapper
    "src/mipmapper.cpp")
target_link_libraries(mipmapper spdlog spng Vulkan::Vulkan)

file(GLOB_RECURSE PNG_IMAGES "${CMAKE_SOURCE_DIR}/resources/*.png")
foreach(IMAGE ${PNG_IMAGES})
    if (${IMAGE} MATCHES ".*\\.mipdata\\.png$")
        continue()
    endif()
    cmake_path(REPLACE_EXTENSION IMAGE LAST_ONLY "mipdata.png" OUTPUT_VARIABLE MIP_IMAGE)
    add_custom_command(OUTPUT ${MIP_IMAGE}
        COMMAND $<TARGET_FILE:mipmapper> ${IMAGE} ${MIP_IMAGE}
        DEPENDS ${IMAGE})
    list(APPEND MIP_IMAGES ${MIP_IMAGE})
endforeach()
add_custom_target(mip_images DEPENDS ${MIP_IMAGES})


# ---- Application ----
add_executable(game
    "src/main.cpp"
    "src/vkw/Device.cpp"
    "src/vkw/Allocator.cpp"
    "src/vkw/Render.cpp"
    "src/vkw/CommandBuffer.cpp"
    "src/fs.cpp"
    "src/vkw/Vkresource.cpp")
add_dependencies(game mip_images shaders)
target_link_libraries(game
    glfw
    glm
    physfs-static
    spdlog
    spng
    spirv-reflect-static
    Vulkan::Vulkan
)
