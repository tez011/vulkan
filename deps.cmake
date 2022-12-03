cmake_minimum_required(VERSION 3.20)

CPMAddPackage(NAME ktx
              VERSION 4.1.0
              GITHUB_REPOSITORY KhronosGroup/KTX-Software
              GIT_TAG v4.1.0-rc3
              OPTIONS "KTX_FEATURE_STATIC_LIBRARY ON"
                      "KTX_FEATURE_TESTS OFF"
                      "KTX_FEATURE_VULKAN OFF"
                      "KTX_FEATURE_GL_UPLOAD OFF")
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
find_package(Vulkan REQUIRED)

# ---- KTX cleanup ----
set_target_properties(
    ktx2check ktx2ktx2 ktxsc ktxinfo
    PROPERTIES EXCLUDE_FROM_ALL True)

add_compile_definitions(SPNG_STATIC)
include_directories(AFTER ${physicsfs_SOURCE_DIR}/src ${spirvreflect_SOURCE_DIR} ${spng_SOURCE_DIR}/spng)
add_subdirectory(lib)
