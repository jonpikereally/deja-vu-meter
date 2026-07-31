cmake_minimum_required(VERSION 3.22)

project(TimelineVU VERSION 0.11.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build for the Mac you're on (Apple Silicon). Add x86_64 here for a universal binary.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# Fetch JUCE automatically (no manual install). First configure will git-clone
# JUCE into the build tree; subsequent configures reuse it.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# ---------------------------------------------------------------------------
# The plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(TimelineVU
    PLUGIN_MANUFACTURER_CODE Jpke          # 4 chars, first upper
    PLUGIN_CODE              Djvu          # 4 chars, unique per plugin
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavu
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE          # auto-installs to ~/Library/Audio/Plug-Ins
)

juce_generate_juce_header(TimelineVU)

target_sources(TimelineVU PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(TimelineVU PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TimelineVU PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Second plugin: Deja VU Spectrum (FFT spectrum + timeline recording)
# ---------------------------------------------------------------------------
juce_add_plugin(DejaVUSpectrum
    PLUGIN_MANUFACTURER_CODE Jpke
    PLUGIN_CODE              Dvsp
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavuspectrum
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU Spectrum"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)

juce_generate_juce_header(DejaVUSpectrum)

target_sources(DejaVUSpectrum PRIVATE
    Source/Spectrum/SpectrumProcessor.cpp
    Source/Spectrum/SpectrumEditor.cpp
)

target_compile_definitions(DejaVUSpectrum PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DejaVUSpectrum PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
