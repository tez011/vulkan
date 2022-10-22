cmake_minimum_required (VERSION 3.20)
cmake_policy(SET CMP0054 NEW)
project("game")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
configure_file(${CMAKE_SOURCE_DIR}/include/config.in.h ${CMAKE_BINARY_DIR}/include/config.h)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)
message("CMake build type: ${CMAKE_BUILD_TYPE}")

# ---- Global compiler options ----

if (MSVC)
    foreach (flag_var
             CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
             CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
             CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/W3" "/W4" ${flag_var} "${${flag_var}}")
        string(REPLACE "/GR" "" ${flag_var} "${${flag_var}}")
    endforeach()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GR-")
    add_compile_options(
        /wd4267
        $<$<CONFIG:DEBUG>:/Od>
        $<$<CONFIG:DEBUG>:/Zi>
        $<$<CONFIG:RELEASE>:/O2>
    )
else(MSVC)
    string(REGEX REPLACE "-frtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fno-rtti")
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

# ---- Application ----
add_executable(game
    "src/main.cpp"
    "src/vkw/Device.cpp"
    "src/vkw/Allocator.cpp"
    "src/vkw/Render.cpp"
    "src/vkw/CommandBuffer.cpp"
    "src/vkw/Vkresource.cpp"
    "src/fs.cpp")
add_dependencies(game shaders)
target_link_libraries(game
    ktx_read
    glfw
    glm
    physfs-static
    spdlog
    spng
    spirv-reflect-static
    Vulkan::Vulkan
)
