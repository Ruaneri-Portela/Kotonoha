cmake_minimum_required(VERSION 3.16)

project(Kirikiri VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET kotonoha_provider_xp3)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/xp3"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/xp3")
endif()

add_library(kirikiri_formats STATIC
    src/KirikiriRuntime.cpp
    src/EmoteRenderer.cpp
)
target_include_directories(kirikiri_formats PUBLIC src include)
target_compile_features(kirikiri_formats PUBLIC cxx_std_17)
set_target_properties(kirikiri_formats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/kag/CMakeLists.txt")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/tjs2/CMakeLists.txt")
find_package(ZLIB REQUIRED)
target_link_libraries(kirikiri_formats PUBLIC
    KotonohaEngine
    kotonoha_provider_xp3
    ZLIB::ZLIB
    kotonoha_vm_kag
    kotonoha_vm_tjs2
)

kotonoha_add_app(Kirikiri src/main.cpp)
target_link_libraries(Kirikiri PRIVATE kirikiri_formats)
set_target_properties(Kirikiri PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(Kirikiri
    BUNDLE_ID me.hirameki.kotonoha.kirikiri GAME kirikiri)

set(KIRIKIRI_TEST_DATA "" CACHE PATH "Kirikiri2/KirikiriZ original data for integration tests")

if(BUILD_TESTING)
    add_executable(KirikiriXp3Tests tests/Xp3Test.cpp)
    target_link_libraries(KirikiriXp3Tests PRIVATE kirikiri_formats)
    add_test(NAME KirikiriXp3 COMMAND KirikiriXp3Tests)

    # Retail evidence: the original XP3 stack is protected, while the loose
    # VNX+ translation scripts are readable. Both report 77 when no data is
    # supplied; a protected XP3 is a SKIP with the exact parser error, never
    # a false pass.
    add_executable(KirikiriRetailTests tests/RetailXp3Test.cpp)
    target_link_libraries(KirikiriRetailTests PRIVATE kirikiri_formats)
    add_test(NAME Kirikiri.RetailXp3Startup
        COMMAND KirikiriRetailTests --xp3 "${KIRIKIRI_TEST_DATA}")
    add_test(NAME Kirikiri.RetailVnxScripts
        COMMAND KirikiriRetailTests --vnx "${KIRIKIRI_TEST_DATA}")
    set_tests_properties(Kirikiri.RetailXp3Startup Kirikiri.RetailVnxScripts
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    kotonoha_label_directory_tests(REAL Kirikiri.RetailXp3Startup
        Kirikiri.RetailVnxScripts)
endif()
