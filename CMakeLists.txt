cmake_minimum_required(VERSION 3.22)

project(Duckling VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_options(/MP /utf-8)
endif()

set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory("${JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Source lists (globbed per module so new files are picked up on the next configure)
# ---------------------------------------------------------------------------
file(GLOB DK_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
file(GLOB DK_PLUGIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp")
file(GLOB DK_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
file(GLOB DK_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

# ---------------------------------------------------------------------------
# Embedded resources (fonts, logo)
# ---------------------------------------------------------------------------
file(GLOB DK_RESOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/*.png")
juce_add_binary_data(DucklingData
    NAMESPACE DucklingBinary
    HEADER_NAME DucklingBinary.h
    SOURCES ${DK_RESOURCE_FILES})
set_target_properties(DucklingData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(DK_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra)

set(DK_JUCE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ---------------------------------------------------------------------------
# The plugin (VST3). An audio effect with a sidechain input and a MIDI input (MIDI trigger mode).
# ---------------------------------------------------------------------------
juce_add_plugin(Duckling
    COMPANY_NAME "Kujar3"
    PRODUCT_NAME "Duckling"
    BUNDLE_ID "com.kujar3.duckling"
    PLUGIN_MANUFACTURER_CODE Kuja
    PLUGIN_CODE Dkln
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/logo_512.png"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Duckling PRIVATE ${DK_CORE_SOURCES} ${DK_PLUGIN_SOURCES} ${DK_UI_SOURCES})
target_include_directories(Duckling PRIVATE src)
target_compile_definitions(Duckling PUBLIC ${DK_JUCE_DEFINES})
target_link_libraries(Duckling
    PRIVATE DucklingData ${DK_JUCE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Pure C++ core unit tests (no JUCE, compiles in seconds)
# ---------------------------------------------------------------------------
add_executable(CoreTests ${DK_TEST_SOURCES} ${DK_CORE_SOURCES})
target_include_directories(CoreTests PRIVATE src tests)
if(MSVC)
    target_compile_options(CoreTests PRIVATE /W4 /permissive-)
endif()

# ---------------------------------------------------------------------------
# Dev tool: processor self-test + editor snapshot renderer
#   DucklingDev selftest
#   DucklingDev snapshot <outDir>
# ---------------------------------------------------------------------------
juce_add_console_app(DucklingDev PRODUCT_NAME "DucklingDev")
file(GLOB DK_DEV_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/*.cpp")
target_sources(DucklingDev PRIVATE ${DK_DEV_SOURCES} ${DK_CORE_SOURCES} ${DK_PLUGIN_SOURCES} ${DK_UI_SOURCES})
target_include_directories(DucklingDev PRIVATE src)
target_compile_definitions(DucklingDev PRIVATE
    ${DK_JUCE_DEFINES}
    JucePlugin_Name="Duckling"
    DUCKLING_DEV_TOOL=1)
target_link_libraries(DucklingDev
    PRIVATE DucklingData ${DK_JUCE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Optional CLAP format (clap-juce-extensions). Only active when the checkout exists
# (scripts\fetch_clap.bat clones the pinned version).
#   scripts\build.bat build-clap Duckling_CLAP
#   -> build-clap\Duckling_artefacts\Release\CLAP\Duckling.clap
# ---------------------------------------------------------------------------
option(DUCKLING_CLAP "Build the CLAP format (needs third_party/clap-juce-extensions)" ON)
set(DUCKLING_CLAP_EXT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap-juce-extensions"
    CACHE PATH "Path to the clap-juce-extensions checkout (with submodules)")
if(DUCKLING_CLAP)
    if(EXISTS "${DUCKLING_CLAP_EXT_PATH}/CMakeLists.txt"
       AND EXISTS "${DUCKLING_CLAP_EXT_PATH}/clap-libs/clap/CMakeLists.txt"
       AND EXISTS "${DUCKLING_CLAP_EXT_PATH}/clap-libs/clap-helpers/CMakeLists.txt")
        add_subdirectory("${DUCKLING_CLAP_EXT_PATH}" clap-juce-extensions EXCLUDE_FROM_ALL)
        clap_juce_extensions_plugin(TARGET Duckling
            CLAP_ID "com.kujar3.duckling"
            CLAP_FEATURES audio-effect compressor utility stereo)
        message(STATUS "Duckling: CLAP enabled (target Duckling_CLAP)")
    else()
        message(STATUS "Duckling: CLAP disabled, ${DUCKLING_CLAP_EXT_PATH} not found "
                       "(run scripts/fetch_clap.bat to enable it)")
    endif()
endif()
