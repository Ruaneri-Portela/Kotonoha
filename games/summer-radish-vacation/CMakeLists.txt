cmake_minimum_required(VERSION 3.16)

project(SummerRadishVacation VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET kotonoha_provider_cmp)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/providers/cmp"
        "${CMAKE_CURRENT_BINARY_DIR}/providers/cmp")
endif()

add_library(SummerRadishFormats STATIC src/Map.cpp src/Transition.cpp src/Native.cpp)
target_include_directories(SummerRadishFormats PUBLIC include)
target_link_libraries(SummerRadishFormats PUBLIC kotonoha_provider_cmp)
target_compile_features(SummerRadishFormats PUBLIC cxx_std_17)
set_target_properties(SummerRadishFormats PROPERTIES CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../modules/vms/summer-radish/CMakeLists.txt")
target_link_libraries(SummerRadishFormats PUBLIC kotonoha_vm_summer-radish)

add_library(summer_radish_vacation STATIC src/SummerRuntime.cpp)
target_include_directories(summer_radish_vacation PUBLIC src)
set_target_properties(summer_radish_vacation PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(summer_radish_vacation PUBLIC
    KotonohaEngine
    SummerRadishFormats
    kotonoha_vm_summer-radish
)

if(KOTONOHA_BUILD_SUMMER OR CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    kotonoha_add_app(SummerRadish src/main.cpp)
    target_link_libraries(SummerRadish PRIVATE summer_radish_vacation)
    set_target_properties(SummerRadish PROPERTIES CXX_EXTENSIONS OFF)
    kotonoha_configure_app(SummerRadish
        BUNDLE_ID me.hirameki.kotonoha.summerradishvacation GAME summer-radish-vacation)
endif()

if(KOTONOHA_BUILD_HOST_TOOLS)
    add_executable(SummerInspect tools/SummerInspect.cpp)
    target_link_libraries(SummerInspect PRIVATE SummerRadishFormats)
endif()

if(BUILD_TESTING)
add_subdirectory(tests)
endif()
