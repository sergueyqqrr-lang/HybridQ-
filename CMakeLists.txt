cmake_minimum_required(VERSION 3.22)

project(HybridQ VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
# Opción A (recomendada para CI / sandboxes): FetchContent descarga JUCE.
# Opción B (recomendada para desarrollo diario): añade JUCE como submódulo git
#   en ./JUCE y comenta el bloque FetchContent, dejando solo add_subdirectory(JUCE).
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12   # fija una versión conocida; sube cuando quieras probar otra
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(HybridQ
    COMPANY_NAME            "TuEmpresa"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Tuco
    PLUGIN_CODE             Hybq
    FORMATS                 VST3 Standalone   # AU solo en macOS; AAX requiere el SDK de Avid aparte
    PRODUCT_NAME            "HybridQ"
)

# JUCE genera macros de versión/binario en un target "juce_generated"
target_compile_definitions(HybridQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(HybridQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/BandProcessor.cpp
        Source/BandHandleComponent.cpp
        Source/SpectrumDisplayComponent.cpp
        Source/BandControlPanel.cpp
)

target_link_libraries(HybridQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
