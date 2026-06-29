cmake_minimum_required(VERSION 3.22)
project(MonoFlux VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch JUCE 7.0.12 ────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        7.0.12
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── webkit2gtk (Linux only) ───────────────────────────────────────────────────
if(LINUX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WEBKIT webkit2gtk-4.1 QUIET)
  if(NOT WEBKIT_FOUND)
    pkg_check_modules(WEBKIT webkit2gtk-4.0 REQUIRED)
  endif()
endif()

# ── Embed index.html as binary data ──────────────────────────────────────────
juce_add_binary_data(MonoFluxAssets
  NAMESPACE BinaryData
  SOURCES   index.html
)
set_target_properties(MonoFluxAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(MonoFlux
  COMPANY_NAME              "RSL Music"
  PLUGIN_MANUFACTURER_CODE  Rslm
  PLUGIN_CODE               Mnfx
  FORMATS                   VST3 Standalone
  PRODUCT_NAME              "MonoFlux"
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES           "Fx"
)

juce_generate_juce_header(MonoFlux)

target_sources(MonoFlux PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
)

target_compile_definitions(MonoFlux
  PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(MonoFlux
  PRIVATE
    MonoFluxAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(LINUX)
  target_link_libraries(MonoFlux PRIVATE ${WEBKIT_LIBRARIES})
  target_include_directories(MonoFlux PRIVATE ${WEBKIT_INCLUDE_DIRS})
  target_compile_options(MonoFlux PRIVATE ${WEBKIT_CFLAGS_OTHER})
endif()
