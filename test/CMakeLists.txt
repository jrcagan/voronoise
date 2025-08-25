cmake_minimum_required(VERSION 3.22)

project(VoronoiseTests)

enable_testing()

add_executable(${PROJECT_NAME} 
   VoronoiTests.cpp
)

target_include_directories(${PROJECT_NAME}
   PRIVATE 
      ${GOOGLETEST_SOURCE_DIR}/googletest/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include
      ${JUCE_SOURCE_DIR}/modules
      ${CMAKE_CURRENT_SOURCE_DIR}/../build/plugin/Voronoise_artefacts/JuceLibraryCode
)

target_link_libraries(${PROJECT_NAME}
   PRIVATE 
      Voronoise 
      GTest::gtest_main
)