cmake_minimum_required(VERSION 3.16)

project(SummerRadishVacation VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

if(NOT TARGET Kotonoha::ProviderCMP)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/cmp"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/cmp")
endif()

add_library(SummerRadishFormats STATIC src/Map.cpp src/Transition.cpp src/Native.cpp)
target_include_directories(SummerRadishFormats PUBLIC include)
target_link_libraries(SummerRadishFormats PUBLIC Kotonoha::ProviderCMP)
target_compile_features(SummerRadishFormats PUBLIC cxx_std_17)
set_target_properties(SummerRadishFormats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/summer-radish/CMakeLists.txt")
target_link_libraries(SummerRadishFormats PUBLIC Kotonoha::Vmsummer-radish)

add_library(summer_radish_vacation STATIC src/SummerRuntime.cpp)
target_include_directories(summer_radish_vacation PUBLIC src)
set_target_properties(summer_radish_vacation PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(summer_radish_vacation PUBLIC
    Kotonoha::Engine
    SummerRadishFormats
    Kotonoha::Vmsummer-radish
)

kotonoha_add_app(SummerRadish src/main.cpp)
target_link_libraries(SummerRadish PRIVATE summer_radish_vacation)
set_target_properties(SummerRadish PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(SummerRadish
    BUNDLE_ID me.hirameki.kotonoha.summerradishvacation GAME summer-radish-vacation)

if(NOT ANDROID)
    add_executable(SummerInspect tools/SummerInspect.cpp)
    target_link_libraries(SummerInspect PRIVATE SummerRadishFormats)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
