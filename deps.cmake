cmake_minimum_required (VERSION 3.11)

add_library(spng STATIC
    ${spng_SOURCE_DIR}/spng/spng.c
    ${miniz_SOURCE_DIR}/miniz.c)
target_compile_definitions(spng PRIVATE SPNG_USE_MINIZ SPNG_SSE=4)
target_include_directories(spng
    PUBLIC ${spng_SOURCE_DIR}/spng
    PRIVATE ${miniz_SOURCE_DIR})
