# Artemis Engine PFS archive & layered AssetProvider.
if(TARGET kotonoha_provider_artemis)
    return()
endif()

add_library(kotonoha_provider_artemis STATIC
    src/PfsArchive.cpp
    src/ArtemisAssets.cpp
)
target_include_directories(kotonoha_provider_artemis PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_link_libraries(kotonoha_provider_artemis PUBLIC
    KotonohaEngine
)
target_compile_features(kotonoha_provider_artemis PUBLIC cxx_std_17)
set_target_properties(kotonoha_provider_artemis PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaArtemisPfsTests tests/PfsArchiveTest.cpp)
    target_link_libraries(KotonohaArtemisPfsTests PRIVATE kotonoha_provider_artemis)
    add_test(NAME Kotonoha.Artemis.PfsArchive COMMAND KotonohaArtemisPfsTests)
    set_tests_properties(Kotonoha.Artemis.PfsArchive PROPERTIES LABELS "synthetic;fast")
endif()
