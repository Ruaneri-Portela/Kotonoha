# Snow Radish ADV3 virtual machine. Only depends on KotonohaEngine; the game
# session that drives it links kotonoha_vm_snow-radish directly.
if(TARGET kotonoha_vm_snow-radish)
    return()
endif()

add_library(kotonoha_vm_snow-radish STATIC "${CMAKE_CURRENT_LIST_DIR}/src/Vm.cpp")

target_include_directories(kotonoha_vm_snow-radish PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_snow-radish PUBLIC KotonohaEngine)
target_compile_features(kotonoha_vm_snow-radish PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_snow-radish PROPERTIES CXX_EXTENSIONS OFF)
