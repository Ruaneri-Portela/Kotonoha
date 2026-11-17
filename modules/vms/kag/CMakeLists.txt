# Kirikiri KAG Runner virtual machine. Only depends on KotonohaEngine; the
# kirikiri formats/runtime links kotonoha_vm_kag directly.
if(TARGET kotonoha_vm_kag)
    return()
endif()

add_library(kotonoha_vm_kag STATIC "${CMAKE_CURRENT_LIST_DIR}/src/KagScript.cpp")

target_include_directories(kotonoha_vm_kag PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(kotonoha_vm_kag PUBLIC KotonohaEngine)
target_compile_features(kotonoha_vm_kag PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_kag PROPERTIES CXX_EXTENSIONS OFF)
