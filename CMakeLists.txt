cmake_minimum_required(VERSION 3.22)

project(plectro-vst VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HVST_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU)" ON)
option(HVST_BUILD_TESTS  "Build the humanizer_core unit tests" ON)
# macOS only: embed FluidSynth and its dependencies into each bundle and rewrite them to @rpath so
# the plugin loads on a clean machine with no Homebrew. Off by default so iterative dev builds stay
# fast; turn on for release/CI (-DPLECTRO_BUNDLE_DEPS=ON).
option(PLECTRO_BUNDLE_DEPS "Embed FluidSynth + deps into the macOS bundles (portable)" OFF)
# Build a minimal FluidSynth from source (no readline/libsndfile/audio drivers) instead of linking
# Homebrew's. Removes the GPL-3 readline dependency and shrinks the bundle. Use for release builds.
option(PLECTRO_MINIMAL_FLUIDSYNTH "Build a minimal FluidSynth from source" OFF)

set(PLECTRO_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    CACHE PATH "Directory holding the bundled SF2 assets (versioned in the repo via git-lfs)")

# ---------------------------------------------------------------------------
# humanizer_core: JUCE-free, engine-free pure logic. Unit tested on its own.
# ---------------------------------------------------------------------------
# Shared, public safe core. The humanization math is NOT here: it lives in source/pro/ and is
# added per edition below, so the free build never compiles the Pro algorithms.
add_library(humanizer_core STATIC
    source/humanizer/StreamingHumanizer.cpp
    source/humanizer/PresetMapping.cpp
    source/humanizer/LayerSet.cpp)
target_include_directories(humanizer_core PUBLIC source)
target_compile_features(humanizer_core PUBLIC cxx_std_20)
# Needed on Linux: this static lib is linked into the VST3 shared object.
set_target_properties(humanizer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Tests (Catch2 via FetchContent). No JUCE, no engine: fast.
# ---------------------------------------------------------------------------
if(HVST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# The plugin.
# ---------------------------------------------------------------------------
if(HVST_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    # FluidSynth backend. Two ways to get it:
    #  - Dev (default): link Homebrew's FluidSynth via pkg-config. Fast, but that build pulls in
    #    readline (GPL-3), libsndfile and portaudio, which we do not use and must not ship.
    #  - Release (PLECTRO_MINIMAL_FLUIDSYNTH=ON): build a minimal FluidSynth from source with those
    #    disabled, leaving glib as the only real dependency, so the bundled plugin ships a small,
    #    permissively licensed dependency set.
    if(PLECTRO_MINIMAL_FLUIDSYNTH)
        include(FetchContent)
        # Everything we do not use, especially readline (GPL-3) and libsndfile.
        foreach(_off readline libsndfile network ipv6 dbus jack portaudio pulseaudio sdl2 sdl3 alsa
                     oss coreaudio coremidi framework opensles oboe dart waveout dsound winmidi)
            set(enable-${_off} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE) # ship FluidSynth as a bundled dylib
        FetchContent_Declare(fluidsynth
            GIT_REPOSITORY https://github.com/FluidSynth/fluidsynth.git
            GIT_TAG v2.4.6
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(fluidsynth)
        set(FLUIDSYNTH_FOUND TRUE)
        set(PLECTRO_FLUIDSYNTH_LIB libfluidsynth)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
            set(PLECTRO_FLUIDSYNTH_LIB PkgConfig::FLUIDSYNTH)
        endif()
    endif()

    # One helper builds an edition. pro is 0 or 1; code is the 4 char JUCE plugin code;
    # sf2 is the asset file name copied into the bundle Resources.
    function(add_plectro_edition target productName code pro sf2)
        juce_add_plugin(${target}
            PRODUCT_NAME "${productName}"
            COMPANY_NAME "Do2tis"
            PLUGIN_MANUFACTURER_CODE Do2t
            PLUGIN_CODE ${code}
            FORMATS VST3 AU
            IS_SYNTH TRUE
            NEEDS_MIDI_INPUT TRUE
            NEEDS_MIDI_OUTPUT FALSE
            IS_MIDI_EFFECT FALSE
            AU_MAIN_TYPE kAudioUnitType_MusicDevice
            VST3_CATEGORIES Instrument
            COPY_PLUGIN_AFTER_BUILD FALSE)

        target_sources(${target} PRIVATE
            source/PluginProcessor.cpp
            source/EditorLookAndFeel.cpp
            source/EditorSupport.cpp
            source/engine/SoundFontEngine.cpp)
        # The humanization math and the full editor are selected at build time, no preprocessor: the
        # Pro edition compiles the algorithms and the humanization editor from source/pro/, the free
        # edition a neutral no-op and a compact editor. The free build compiles and links with
        # source/pro/ absent.
        if(pro)
        else()
            target_sources(${target} PRIVATE
                source/humanizer/NeutralVariationSource.cpp
                source/FreeEditor.cpp)
        endif()
        target_include_directories(${target} PRIVATE source)
        target_compile_definitions(${target}
            PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            PRIVATE
            PLECTRO_PRO=${pro}
            HVST_PRODUCT_NAME="${productName}"
            HVST_DEFAULT_BANK=0)
        target_link_libraries(${target} PRIVATE
            humanizer_core
            juce::juce_audio_utils
            juce::juce_dsp
            PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
        if(FLUIDSYNTH_FOUND)
            target_compile_definitions(${target} PRIVATE HVST_HAVE_FLUIDSYNTH=1)
            target_link_libraries(${target} PRIVATE ${PLECTRO_FLUIDSYNTH_LIB})
        else()
            message(WARNING "${target}: FluidSynth NOT found, building a SILENT plugin")
        endif()

        # Copy the edition SF2 into each built plugin bundle's Resources folder.
        set(_sf2 "${PLECTRO_ASSETS_DIR}/${sf2}")
        if(EXISTS "${_sf2}")
            foreach(fmt VST3 AU)
                if(TARGET ${target}_${fmt})
                    add_custom_command(TARGET ${target}_${fmt} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory
                                "$<TARGET_FILE_DIR:${target}_${fmt}>/../Resources"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${_sf2}" "$<TARGET_FILE_DIR:${target}_${fmt}>/../Resources/${sf2}"
                        VERBATIM)
                endif()
            endforeach()
        else()
            message(WARNING "${target}: SF2 asset not found at ${_sf2}; bundle will have no SoundFont")
        endif()

        # Make each bundle portable (embed FluidSynth + deps, rewrite to @rpath, ad-hoc re-sign).
        # Runs after the SF2 copy so the signature covers the final bundle contents.
        if(APPLE AND PLECTRO_BUNDLE_DEPS)
            foreach(fmt VST3 AU)
                if(TARGET ${target}_${fmt})
                    # A minimal FluidSynth built in tree is referenced via @rpath, so copy it into
                    # the bundle first; the bundler then follows its Homebrew glib dependencies.
                    if(PLECTRO_MINIMAL_FLUIDSYNTH)
                        add_custom_command(TARGET ${target}_${fmt} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E make_directory
                                    "$<TARGET_FILE_DIR:${target}_${fmt}>/../Frameworks"
                            COMMAND ${CMAKE_COMMAND} -E copy
                                    "$<TARGET_SONAME_FILE:libfluidsynth>"
                                    "$<TARGET_FILE_DIR:${target}_${fmt}>/../Frameworks/"
                            VERBATIM)
                    endif()
                    add_custom_command(TARGET ${target}_${fmt} POST_BUILD
                        COMMAND "${CMAKE_SOURCE_DIR}/scripts/bundle-macos-deps.sh"
                                "$<TARGET_FILE_DIR:${target}_${fmt}>/../.."
                        VERBATIM)
                endif()
            endforeach()
        endif()
    endfunction()

    add_plectro_edition(plectro     "Plectro"     Plec 0 "Plectro.sf2")
    # The Pro edition builds only where its private sources are present. The public repo is exported
    # without source/pro/ (the export strips this block), so it builds the free edition alone.
endif()
