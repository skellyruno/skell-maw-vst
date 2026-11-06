cmake_minimum_required(VERSION 3.22)
project(SkellMaw VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

# ---- JUCE (same approach as the StopDial template) ------------------------
include(FetchContent)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Shared artwork + shared library --------------------------------------
juce_add_binary_data(MawArt HEADER_NAME "MawArtData.h" NAMESPACE MawArtData SOURCES Assets/art.jpg)
set_target_properties(MawArt PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_library(MawCommon STATIC
    Common/MawProcessorBase.h Common/MawProcessorBase.cpp
    Common/MawEditor.h Common/MawEditor.cpp
    Common/MawLookAndFeel.h Common/MawLookAndFeel.cpp
    Common/PluginSpec.h Common/Theme.h Common/DspUtil.h)
set_target_properties(MawCommon PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_include_directories(MawCommon PUBLIC Common)
target_compile_definitions(MawCommon PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MawCommon PUBLIC
    MawArt
    juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---- Plugins ----------------------------------------------------------------
set(MAW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MAW_FORMATS AU)
endif()

function(maw_add_plugin target product code description)
    juce_add_plugin(${target}
        COMPANY_NAME "Skell Audio"
        BUNDLE_ID "audio.skell.${target}"
        PLUGIN_MANUFACTURER_CODE SkAu
        PLUGIN_CODE ${code}
        PRODUCT_NAME "${product}"
        DESCRIPTION "${description}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS ${MAW_FORMATS})
    target_sources(${target} PRIVATE Plugins/${target}/${target}.cpp)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_link_libraries(${target} PRIVATE MawCommon)
endfunction()

maw_add_plugin(DarkForest     "Dark Forest"     DkFr "Reverb - Chapter 1")
maw_add_plugin(Hypnosis       "Hypnosis"        HyPn "Phaser - Chapter 2")
maw_add_plugin(SerpentsMaw    "Serpents Maw"    SpMw "Distortion - Chapter 3")
maw_add_plugin(ButterflyDelay "Butterfly Delay" BfDl "Ping-pong delay - Chapter 4")
maw_add_plugin(ShedSkin       "Shed Skin"       ShSk "Freeze and glitch - Chapter 5")
maw_add_plugin(Constrictor    "Constrictor"     CnSt "Compressor - Chapter 6")
maw_add_plugin(NightBloom     "Night Bloom"     NtBl "Harmonic exciter - Chapter 7")
