cmake_minimum_required(VERSION 3.22)
project(RoomBreaker VERSION 2.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: read sources as UTF-8 (comments use arrows and symbols), allow big objects.
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>" "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>")

# OFF  → full plugin (fetches JUCE) + core + tests
# ON   → JUCE-free DSP core + offline test harness only
option(RB_CORE_ONLY "Build only the JUCE-free DSP core and offline tests" OFF)

#-------------------------------------------------------------------------------
# JUCE-free DSP core (unit-testable without the framework)
#-------------------------------------------------------------------------------
add_library(rb_core STATIC
    Source/DSP/StressTracker.cpp
    Source/DSP/EarlyReflectionEngine.cpp
    Source/DSP/DynamicFDN.cpp
    Source/DSP/ModalDebrisGenerator.cpp
    Source/DSP/ImpactGenerator.cpp
    Source/DSP/FractureGranulator.cpp
    Source/DSP/Engine.cpp)
target_include_directories(rb_core PUBLIC Source/DSP)
set_target_properties(rb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(rb_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()

add_executable(rb_offline_render Tests/OfflineRender.cpp)
target_link_libraries(rb_offline_render PRIVATE rb_core)

enable_testing()
add_test(NAME rb_offline_render COMMAND rb_offline_render ${CMAKE_BINARY_DIR})

if(RB_CORE_ONLY)
    return()
endif()

#-------------------------------------------------------------------------------
# JUCE plugin
#-------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Audio Units only exist on macOS; Windows and Linux get VST3 + Standalone.
if(APPLE)
    set(RB_FORMATS VST3 AU Standalone)
else()
    set(RB_FORMATS VST3 Standalone)
endif()

juce_add_plugin(RoomBreaker
    COMPANY_NAME              "RoomBreaker"
    VERSION                   ${PROJECT_VERSION}
    PRODUCT_NAME              "RoomBreaker"
    PLUGIN_MANUFACTURER_CODE  Rbrk
    PLUGIN_CODE               RmBk
    FORMATS                   ${RB_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES           Fx Reverb
    AU_MAIN_TYPE              kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(RoomBreaker PRIVATE
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/PresetLibrary.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/HouseView.cpp
    Source/UI/HousePaint.cpp)

# Typefaces are embedded so the UI looks the same on every system (both SIL Open Font Licence,
# licence texts alongside): Barlow Semi Condensed for words, JetBrains Mono for numbers.
juce_add_binary_data(RoomBreakerFonts
    NAMESPACE RoomBreakerFonts
    SOURCES
        Resources/fonts/BarlowSemiCondensed-Medium.ttf
        Resources/fonts/BarlowSemiCondensed-SemiBold.ttf
        Resources/fonts/JetBrainsMono.ttf)
set_target_properties(RoomBreakerFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(RoomBreaker PRIVATE Source)

target_compile_definitions(RoomBreaker PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RoomBreaker
    PRIVATE
        rb_core
        RoomBreakerFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# JUCE re-signs the VST3 bundle after its post-build step, but the AU and Standalone
# bundles only carry the linker's ad-hoc signature on the binary, which macOS reports as
# "code has no resources but signature indicates they must be present". Sign the whole
# bundle (ad-hoc) so auval, Logic and Gatekeeper accept local builds.
if(APPLE)
    foreach(format AU Standalone)
        if(TARGET RoomBreaker_${format})
            add_custom_command(TARGET RoomBreaker_${format} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:RoomBreaker_${format}>"
                COMMENT "Ad-hoc signing RoomBreaker ${format} bundle"
                VERBATIM)
        endif()
    endforeach()
endif()

#-------------------------------------------------------------------------------
# Headless smoke test of the real AudioProcessor (no host, nothing installed)
#-------------------------------------------------------------------------------
add_executable(rb_plugin_smoke Tests/PluginSmoke.cpp)
target_compile_definitions(rb_plugin_smoke PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_include_directories(rb_plugin_smoke PRIVATE Source)
target_link_libraries(rb_plugin_smoke PRIVATE
    RoomBreaker
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
add_test(NAME rb_plugin_smoke COMMAND rb_plugin_smoke ${CMAKE_BINARY_DIR})
