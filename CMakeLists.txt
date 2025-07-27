cmake_minimum_required(VERSION 3.22)

project(Voronoise)

set(CMAKE_CXX_STANDARD 23)

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
include(cmake/cpm.cmake)
 
CPMAddPackage(
   NAME Juce 
   GITHUB_REPOSITORY juce-framework/JUCE
   GIT_TAG 8.0.8
   VERSION 8.0.8
   SOURCE_DIR ${LIB_DIR}/juce
)

if (MSVC)
   add_compile_options(/WX)
else()
   add_compile_options(-Wextra -Wpedantic)
endif()

add_subdirectory(plugin)