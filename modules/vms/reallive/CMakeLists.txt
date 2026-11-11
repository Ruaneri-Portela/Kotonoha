# RealLive Session virtual machine. Reads RealLive/Script.hpp and the
# libreallive vendor headers exposed by RealLiveFormats.
if(TARGET Kotonoha::Modules::VMs::RealLive)
    return()
endif()

if(NOT TARGET Kotonoha::Engine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../lib"
        "${CMAKE_CURRENT_BINARY_DIR}/kotonoha")
endif()

add_library(kotonoha_vm_reallive STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Session.cpp")
add_library(Kotonoha::Modules::VMs::RealLive ALIAS kotonoha_vm_reallive)
add_library(Kotonoha::Vmreallive ALIAS kotonoha_vm_reallive)

target_include_directories(kotonoha_vm_reallive PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_reallive PUBLIC Kotonoha::Engine PRIVATE RealLiveFormats)

if(MSVC)
    target_compile_options(kotonoha_vm_reallive PUBLIC /J)
else()
    target_compile_options(kotonoha_vm_reallive PUBLIC -funsigned-char)
endif()

target_compile_features(kotonoha_vm_reallive PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_reallive PROPERTIES CXX_EXTENSIONS OFF)
