cmake_minimum_required(VERSION 3.16)

project(RealLive VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

add_library(RealLiveFormats STATIC
    src/Config.cpp
    src/Images.cpp
    src/Voice.cpp
    src/Script.cpp
    src/System.cpp
    vendor/libreallive/archive.cc
    vendor/libreallive/bytecode.cc
    vendor/libreallive/compression.cc
    vendor/libreallive/expression.cc
    vendor/libreallive/filemap.cc
    vendor/libreallive/scenario.cc
)
target_include_directories(RealLiveFormats PUBLIC include vendor)
target_compile_features(RealLiveFormats PUBLIC cxx_std_17)
set_target_properties(RealLiveFormats PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(RealLiveFormats PUBLIC /J)
else()
    target_compile_options(RealLiveFormats PUBLIC -funsigned-char)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/CMakeLists.txt")
# Formats expose VM declarations as headers but never link backwards into
# the VM. The VM links Formats, keeping the static-library DAG acyclic.
target_include_directories(RealLiveFormats PUBLIC
    $<TARGET_PROPERTY:kotonoha_vm_reallive,INTERFACE_INCLUDE_DIRECTORIES>)

if(KOTONOHA_BUILD_HOST_TOOLS)
    add_executable(RealLiveInspect tools/RealLiveInspect.cpp)
    target_link_libraries(RealLiveInspect PRIVATE RealLiveFormats)
endif()

find_package(Iconv REQUIRED)
target_link_libraries(RealLiveFormats PUBLIC KotonohaEngine Iconv::Iconv)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/reallive"
    "${CMAKE_CURRENT_BINARY_DIR}/providers/reallive")

add_library(reallive_port STATIC src/RealLiveRuntime.cpp)
target_include_directories(reallive_port PUBLIC src)
set_target_properties(reallive_port PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(reallive_port PUBLIC
    RealLiveFormats
    KotonohaEngine
    Iconv::Iconv
    kotonoha_provider_reallive
    kotonoha_vm_reallive
)

kotonoha_add_app(RealLive src/main.cpp)
target_link_libraries(RealLive PRIVATE reallive_port)
set_target_properties(RealLive PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(RealLive
    BUNDLE_ID me.hirameki.kotonoha.reallive GAME reallive)

if(BUILD_TESTING)
    # A completed RGBA scene must be presented without alpha masking. This
    # synthetic renderer test uses no original game artwork or archives.
    add_executable(RealLiveSceneTextureTests tests/SceneTextureTest.cpp)
    target_include_directories(RealLiveSceneTextureTests PRIVATE src)
    target_compile_features(RealLiveSceneTextureTests PRIVATE cxx_std_17)
    target_link_libraries(RealLiveSceneTextureTests PRIVATE SDL3::SDL3)
    add_test(NAME RealLive.SceneTexture COMMAND RealLiveSceneTextureTests)
    set_tests_properties(RealLive.SceneTexture PROPERTIES
        LABELS synthetic
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    # Synthetic gamepad/touch regressions require no game archive.
    add_executable(RealLiveInputTests tests/InputTest.cpp)
    target_include_directories(RealLiveInputTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/include")
    target_compile_features(RealLiveInputTests PRIVATE cxx_std_17)
    target_link_libraries(RealLiveInputTests PRIVATE
        SDL3::SDL3 SDL3_ttf::SDL3_ttf)
    add_test(NAME RealLive.Input COMMAND RealLiveInputTests)
    set_tests_properties(RealLive.Input PROPERTIES LABELS synthetic)

    # Regression: RealLive's little-endian reads must work with both plain
    # char modes, independent of the legacy -funsigned-char build setting.
    foreach(char_mode IN ITEMS signed unsigned)
        set(test_target "RealLiveEndianRead${char_mode}Tests")
        add_executable(${test_target} tests/EndianReadTest.cpp)
        target_include_directories(${test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
        target_compile_features(${test_target} PRIVATE cxx_std_17)
        set_target_properties(${test_target} PROPERTIES CXX_EXTENSIONS OFF)
        if(MSVC)
            if(char_mode STREQUAL "unsigned")
                target_compile_options(${test_target} PRIVATE /J)
            endif()
        else()
            target_compile_options(${test_target} PRIVATE "-f${char_mode}-char")
        endif()
        add_test(NAME "RealLive.EndianRead.${char_mode}" COMMAND ${test_target})
        set_tests_properties("RealLive.EndianRead.${char_mode}"
            PROPERTIES LABELS synthetic)
    endforeach()
    foreach(format Images Voice Script Session System)
        add_executable(RealLive${format}Tests tests/${format}Test.cpp)
        target_link_libraries(RealLive${format}Tests PRIVATE RealLiveFormats kotonoha_vm_reallive)
        if(format STREQUAL "Session")
            # These older tests use assert(), which Release -DNDEBUG would
            # otherwise compile away instead of checking retail scenarios.
            if(MSVC)
                target_compile_options(RealLive${format}Tests PRIVATE /UNDEBUG)
            else()
                target_compile_options(RealLive${format}Tests PRIVATE -UNDEBUG)
            endif()
        endif()
        if(format STREQUAL "Session" AND REALLIVE_TEST_DATA)
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests "${REALLIVE_TEST_DATA}/Seen.txt")
        elseif(format STREQUAL "Session")
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests)
            set_tests_properties(RealLive${format} PROPERTIES SKIP_RETURN_CODE 77)
        else()
            add_test(NAME RealLive${format} COMMAND RealLive${format}Tests)
        endif()
    endforeach()

    # Data-independent verification of switch/case selection semantics.
    # Pure C++17 tests for Str.digits / Str.digit opcode parameters.
    # Strict, data-free tests for atomic Str.strclear range validation.
    add_executable(RealLiveBoundedRangeTests tests/BoundedRangeTest.cpp)
    target_include_directories(RealLiveBoundedRangeTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/include")
    target_compile_features(RealLiveBoundedRangeTests PRIVATE cxx_std_17)
    add_test(NAME RealLive.BoundedRange COMMAND RealLiveBoundedRangeTests)
    set_tests_properties(RealLive.BoundedRange PROPERTIES LABELS synthetic)

    add_executable(RealLiveDecimalDigitsTests tests/DecimalDigitsTest.cpp)
    target_include_directories(RealLiveDecimalDigitsTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/include")
    target_compile_features(RealLiveDecimalDigitsTests PRIVATE cxx_std_17)
    add_test(NAME RealLive.DecimalDigits COMMAND RealLiveDecimalDigitsTests)
    set_tests_properties(RealLive.DecimalDigits PROPERTIES LABELS synthetic)

    add_executable(RealLiveStringOpsTests tests/StringOpsTest.cpp)
    target_include_directories(RealLiveStringOpsTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/include")
    target_compile_features(RealLiveStringOpsTests PRIVATE cxx_std_17)
    add_test(NAME RealLive.StringOps COMMAND RealLiveStringOpsTests)
    set_tests_properties(RealLive.StringOps PROPERTIES LABELS synthetic)

    add_executable(RealLiveCaseSelectionTests tests/CaseSelectionTest.cpp)
    target_include_directories(RealLiveCaseSelectionTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/reallive/include")
    target_compile_features(RealLiveCaseSelectionTests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(RealLiveCaseSelectionTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(RealLiveCaseSelectionTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME RealLive.CaseSelection COMMAND RealLiveCaseSelectionTests)
    set_tests_properties(RealLive.CaseSelection PROPERTIES LABELS synthetic)

    add_executable(RealLiveEngineTests tests/EngineTest.cpp)
    target_link_libraries(RealLiveEngineTests PRIVATE reallive_port)
    add_executable(RealLiveConfigTests tests/ConfigTest.cpp)
    target_link_libraries(RealLiveConfigTests PRIVATE reallive_port)
    add_test(NAME RealLiveConfig COMMAND RealLiveConfigTests)
endif()

kotonoha_label_directory_tests(REAL
    RealLiveSession
)
