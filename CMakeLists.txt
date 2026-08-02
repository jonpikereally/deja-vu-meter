cmake_minimum_required(VERSION 3.22)

project(TimelineVU VERSION 0.14.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Defaults to the host arch (Apple Silicon); override with
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal (distribution) build.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

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
    BUNDLE_ID                com.jonpike.dejavumeter
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU Meter"
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

# ---------------------------------------------------------------------------
# Third plugin: Deja VU LUFS (loudness + true peak, timeline recording)
# ---------------------------------------------------------------------------
juce_add_plugin(DejaVULUFS
    PLUGIN_MANUFACTURER_CODE Jpke
    PLUGIN_CODE              Dvlu
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavulufs
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU LUFS"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)
juce_generate_juce_header(DejaVULUFS)
target_sources(DejaVULUFS PRIVATE
    Source/LUFS/LUFSProcessor.cpp
    Source/LUFS/LUFSEditor.cpp
)
target_compile_definitions(DejaVULUFS PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DejaVULUFS PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Fourth plugin: Deja VU Pitch (pitch / intonation, timeline recording)
# ---------------------------------------------------------------------------
juce_add_plugin(DejaVUPitch
    PLUGIN_MANUFACTURER_CODE Jpke
    PLUGIN_CODE              Dvpi
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavupitch
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU Pitch"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)
juce_generate_juce_header(DejaVUPitch)
target_sources(DejaVUPitch PRIVATE
    Source/Pitch/PitchProcessor.cpp
    Source/Pitch/PitchEditor.cpp
)
target_compile_definitions(DejaVUPitch PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DejaVUPitch PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Fifth plugin: Deja VU Width (stereo field: correlation / width / balance)
# ---------------------------------------------------------------------------
juce_add_plugin(DejaVUWidth
    PLUGIN_MANUFACTURER_CODE Jpke
    PLUGIN_CODE              Dvwi
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavuwidth
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU Width"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)
juce_generate_juce_header(DejaVUWidth)
target_sources(DejaVUWidth PRIVATE
    Source/Width/WidthProcessor.cpp
    Source/Width/WidthEditor.cpp
)
target_compile_definitions(DejaVUWidth PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DejaVUWidth PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Sixth plugin: Deja VU Suite (Meter + LUFS + Spectrum + Width in one, with
# selectable multi-panel layout and a unified take that records all four)
# ---------------------------------------------------------------------------
juce_add_plugin(DejaVUSuite
    PLUGIN_MANUFACTURER_CODE Jpke
    PLUGIN_CODE              Dvsu
    COMPANY_NAME             "Jon Pike"
    BUNDLE_ID                com.jonpike.dejavusuite
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Deja VU Suite"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)
juce_generate_juce_header(DejaVUSuite)
target_sources(DejaVUSuite PRIVATE
    Source/Suite/SuiteProcessor.cpp
    Source/Suite/SuiteEditor.cpp
)
target_compile_definitions(DejaVUSuite PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DejaVUSuite PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
