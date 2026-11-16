# Synthetic tests require no user-supplied retail game assets.
# Real tests require a game installation/disc or retail-data route.
# Invoke from the directory that registered the tests (CMake 3.16 compatible).
function(kotonoha_label_directory_tests)
    cmake_parse_arguments(GROUP "" "" "REAL" ${ARGN})
    get_property(_registered DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY TESTS)
    foreach(_name IN LISTS _registered)
        if(_name IN_LIST GROUP_REAL)
            set_tests_properties("${_name}" PROPERTIES LABELS "real")
        else()
            set_tests_properties("${_name}" PROPERTIES LABELS "synthetic")
        endif()
    endforeach()
endfunction()
