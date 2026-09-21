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
    test_detection_gate.cpp
    test_layer_set.cpp)

    target_sources(core_tests PRIVATE ../source/core/NeutralVariationSource.cpp)

target_link_libraries(core_tests PRIVATE plectro_core Catch2::Catch2WithMain)
target_compile_features(core_tests PRIVATE cxx_std_20)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(core_tests)

# Verify the generated articulation artifacts (C++ header, host maps, docs) are in sync with the
# single source of truth and are syntactically valid. Fails if the JSON was edited without
# regenerating, or a generated file was hand-edited. Skipped where Python or the tool is absent.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_articulation_maps.py")
    add_test(NAME articulation_maps_check
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_articulation_maps.py" --check)
endif()
