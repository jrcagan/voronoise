cmake_minimum_required(VERSION 3.22)

project(Voronoise VERSION 0.1.0)

juce_add_plugin(${PROJECT_NAME}
   COMPANY_NAME JuneSound
   IS_SYNTH TRUE 
   NEEDS_MIDI_INPUT TRUE
   NEEDS_MIDI_OUTPUT FALSE 
   PLUGIN_MANUFACTURER_CODE Jsnd
   PLUGIN_CODE Vrnz
   FORMATS VST3 STANDALONE
   PRODUCT_NAME "Voronoise"
)

juce_generate_juce_header(${PROJECT_NAME})

set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SOURCE_FILES source/PluginEditor.cpp 
                 source/PluginProcessor.cpp 
                 source/synth/WavetableOscillator.cpp 
                 source/synth/WavetableSynth.cpp)
set(HEADER_FILES ${INCLUDE_DIR}/Voronoise/PluginEditor.h ${INCLUDE_DIR}/Voronoise/PluginProcessor.h ${INCLUDE_DIR}/synth/WavetableOscillator.h ${INCLUDE_DIR}/synth/WavetableSynth.h)

target_sources(${PROJECT_NAME} PRIVATE ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_BINARY_DIR}/JuceLibraryCode 
)

target_link_libraries(${PROJECT_NAME}
   PRIVATE
      juce::juce_audio_utils
   PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags
)

target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

set_source_files_properties(${SOURCE_FILES} PROPERTIES COMPILE_OPTIONS "${PROJECT_WARNINGS_CXX}")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/..)
