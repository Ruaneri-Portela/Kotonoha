# PONscripter SAR/NSA/VFS asset provider.
if(TARGET kotonoha_provider_ponscripter)
    return()
endif()

if(NOT TARGET kotonoha_vm_ponscripter)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../vms/ponscripter"
        "${CMAKE_CURRENT_BINARY_DIR}/vms/ponscripter")
endif()

add_library(kotonoha_provider_ponscripter STATIC
    src/PonscripterAssets.cpp
)
target_include_directories(kotonoha_provider_ponscripter PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_link_libraries(kotonoha_provider_ponscripter PUBLIC
    kotonoha_vm_ponscripter
    KotonohaEngine
)
target_compile_features(kotonoha_provider_ponscripter PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_ponscripter PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaPonscripterArchivesTests tests/ArchiveProviderTest.cpp)
    target_link_libraries(KotonohaPonscripterArchivesTests PRIVATE kotonoha_provider_ponscripter)
    add_test(NAME Kotonoha.Ponscripter.Archives COMMAND KotonohaPonscripterArchivesTests)
    set_tests_properties(Kotonoha.Ponscripter.Archives PROPERTIES LABELS synthetic)
endif()
