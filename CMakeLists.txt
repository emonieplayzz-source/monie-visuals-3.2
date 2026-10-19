cmake_minimum_required(VERSION 3.22)
project(MonieVisuals VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)

option(MONIE_BUILD_TESTS "Build the analyzer unit test" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    ON)
# Offline / pre-cloned dependencies: pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<dir> -DFETCHCONTENT_SOURCE_DIR_STB=<dir>
FetchContent_MakeAvailable(JUCE stb)

juce_add_plugin(MonieVisuals
    COMPANY_NAME "monie"
    PRODUCT_NAME "monie visuals"
    PLUGIN_MANUFACTURER_CODE Mnvs
    PLUGIN_CODE Mvis
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(MonieShaders SOURCES
    Shaders/pass.vert Shaders/scene.frag Shaders/fx.frag Shaders/layer.frag Shaders/composite.frag Shaders/rd.frag Shaders/final.frag)

target_sources(MonieVisuals PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Render/Compositor.cpp
    Source/Render/TextRaster.cpp
    Source/Media/MediaPool.cpp
    Source/UI/PreviewComponent.cpp
    Source/UI/Inspectors.cpp
    Source/UI/LayerTimeline.cpp
    Source/CaptionService.cpp
    Source/ExportJob.cpp)

target_include_directories(MonieVisuals PRIVATE Source Source/Core ${stb_SOURCE_DIR})

target_compile_definitions(MonieVisuals PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Headless Linux dev/test builds (no X11 extension / ALSA dev packages installed). Ignored on Windows.
option(MONIE_MINIMAL_LINUX_DEPS "Disable optional X11/ALSA/JACK features (CI sandbox only)" OFF)
if(MONIE_MINIMAL_LINUX_DEPS)
    target_compile_definitions(MonieVisuals PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
        JUCE_USE_XRENDER=0 JUCE_USE_XSHM=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(MonieVisuals
    PRIVATE MonieShaders juce::juce_audio_utils juce::juce_audio_processors juce::juce_opengl
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(MONIE_BUILD_TESTS)
    enable_testing()
    # Pure-C++ engine tests (no JUCE, no GPU): analyzer, drive, gradients, animation, project, captions, ASR chunker, export plan ...
    file(GLOB MONIE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
    foreach(src ${MONIE_TEST_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        add_executable(${name} ${src})
        target_include_directories(${name} PRIVATE Source/Core)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
endif()
