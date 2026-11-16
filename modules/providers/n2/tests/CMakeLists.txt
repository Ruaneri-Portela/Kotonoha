cmake_minimum_required(VERSION 3.16)
project(KotonohaN2ArchiveStandalone LANGUAGES CXX)
enable_testing()

# Fast-format validation without SDL, FFmpeg or a full Kotonoha toolchain.
add_executable(KotonohaN2ArchiveStandalone
    "${CMAKE_CURRENT_LIST_DIR}/ArchiveTest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/Archive.cpp")
target_include_directories(KotonohaN2ArchiveStandalone PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../include")
target_compile_features(KotonohaN2ArchiveStandalone PRIVATE cxx_std_17)
set_target_properties(KotonohaN2ArchiveStandalone
    PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME N2System.StandaloneArchives COMMAND KotonohaN2ArchiveStandalone)
