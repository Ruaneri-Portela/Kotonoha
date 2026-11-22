# Pure C++17 Artemis Engine .ast/.ipt parser and VM (zero SDL dependency).
if(TARGET kotonoha_vm_artemis)
    return()
endif()

add_library(kotonoha_vm_artemis STATIC
    src/AstParser.cpp
    src/ArtemisVm.cpp
)

target_include_directories(kotonoha_vm_artemis PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_compile_features(kotonoha_vm_artemis PUBLIC cxx_std_17)
set_target_properties(kotonoha_vm_artemis PROPERTIES CXX_EXTENSIONS OFF)

if(BUILD_TESTING AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(KotonohaArtemisVmTests tests/AstParserAndVmTest.cpp)
    target_link_libraries(KotonohaArtemisVmTests PRIVATE kotonoha_vm_artemis)
    add_test(NAME Kotonoha.Artemis.Vm COMMAND KotonohaArtemisVmTests)
    set_tests_properties(Kotonoha.Artemis.Vm PROPERTIES LABELS "synthetic;fast")
endif()
