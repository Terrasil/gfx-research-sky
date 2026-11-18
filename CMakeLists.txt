cmake_minimum_required(VERSION 3.25)

project(gfx-research-sky LANGUAGES C CXX)

option(GFX_RESEARCH_BUILD_APP "Build the interactive OpenGL experiment" ON)

set(GFX_RESEARCH_BASE_GIT_TAG v0.3 CACHE STRING "gfx-research-base Git revision")
set(GFX_RESEARCH_BASE_LOCAL_PATH "" CACHE PATH "Optional local gfx-research-base checkout")

if(GFX_RESEARCH_BUILD_APP)
if(GFX_RESEARCH_BASE_LOCAL_PATH)
    add_subdirectory("${GFX_RESEARCH_BASE_LOCAL_PATH}" "${CMAKE_BINARY_DIR}/gfx-research-base")
else()
    if(NOT DEFINED CPM_SOURCE_CACHE)
        set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache")
    endif()

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake")
    set(GFX_RESEARCH_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty" CACHE PATH "Third-party source directory")

    CPMAddPackage(
        NAME gfx_research_base
        GITHUB_REPOSITORY Terrasil/gfx-research-base
        GIT_TAG "${GFX_RESEARCH_BASE_GIT_TAG}"
        SOURCE_DIR "${GFX_RESEARCH_THIRD_PARTY_DIR}/gfx-research-base"
        OPTIONS
            "GFX_RESEARCH_BASE_BUILD_EXAMPLE OFF"
    )
endif()

add_executable(gfx-research-sky
    src/main.cpp
)

target_compile_features(gfx-research-sky PRIVATE cxx_std_20)
target_link_libraries(gfx-research-sky PRIVATE gfx::research_base)
target_compile_definitions(gfx-research-sky PRIVATE
    GFX_RESEARCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GFX_RESEARCH_BUILD_TYPE="$<CONFIG>"
    GFX_RESEARCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

if(COMMAND gfx_research_configure_executable)
    gfx_research_configure_executable(gfx-research-sky)
endif()



endif()

# ================================
# Reference tests
# ================================

include(CTest)
if(BUILD_TESTING)
    add_executable(gfx-research-sky-reference-tests
        tests/reference_math.cpp
    )
    target_compile_features(gfx-research-sky-reference-tests PRIVATE cxx_std_20)
    add_test(NAME gfx-research-sky-fixed-distance-reference COMMAND gfx-research-sky-reference-tests)
    add_executable(gfx-research-sky-volume-tests tests/volume_reference.cpp)
    target_compile_features(gfx-research-sky-volume-tests PRIVATE cxx_std_20)
    add_test(NAME gfx-research-sky-volumetric-reference COMMAND gfx-research-sky-volume-tests)
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL "Disable automatic build-system regeneration" FORCE)
endif()
