include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(Catch2)

# Tests that do not depend on the humanization math (safe in the exported public tree).
add_executable(core_tests
    test_edition.cpp
    test_edition_gating.cpp
    test_preset_mapping.cpp
    test_keyswitch_layout.cpp
    test_layer_set.cpp)

# The humanization math lives under source/pro/ (private). Where it is present, also build the
# tests that exercise it and link the humanized VariationSource; otherwise (the exported public
# tree) build the free-safe tests only and link the neutral implementation.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../source/pro")
    target_sources(core_tests PRIVATE
        test_rng.cpp
        test_tremolo.cpp
        test_humanizer.cpp
        test_streaming.cpp
        ../source/pro/HumanizedVariationSource.cpp
        ../source/pro/Humanizer.cpp
        ../source/pro/TremoloDetector.cpp)
else()
    target_sources(core_tests PRIVATE ../source/humanizer/NeutralVariationSource.cpp)
endif()

target_link_libraries(core_tests PRIVATE humanizer_core Catch2::Catch2WithMain)
target_compile_features(core_tests PRIVATE cxx_std_20)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(core_tests)
