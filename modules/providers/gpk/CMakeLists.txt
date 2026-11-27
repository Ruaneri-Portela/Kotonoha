# School Days on this fork already compiles its retail GPK archive, index,
# assets and LxH-aware mount into `school_days`. The upstream provider target
# references three source files that do not exist under this module here;
# compiling the upstream mount alongside the game-specific implementation
# would duplicate global mount state and break DLC/translation lookups.
# Expose the current public provider boundary without building duplicate code.
if(TARGET kotonoha_provider_gpk)
    return()
endif()

find_package(ZLIB REQUIRED)

add_library(kotonoha_provider_gpk INTERFACE)
target_include_directories(kotonoha_provider_gpk INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(kotonoha_provider_gpk INTERFACE KotonohaEngine ZLIB::ZLIB)
target_compile_features(kotonoha_provider_gpk INTERFACE cxx_std_17)
