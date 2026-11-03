# Several test executables share a resource directory. Serialize their copies
# so concurrent post-build commands cannot race while creating directories.
if(NOT DEFINED ASSET_SOURCE OR NOT DEFINED ASSET_DESTINATION)
    message(FATAL_ERROR "ASSET_SOURCE and ASSET_DESTINATION are required")
endif()
get_filename_component(parent "${ASSET_DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${parent}")
file(LOCK "${ASSET_DESTINATION}.lock" GUARD PROCESS TIMEOUT 120)
file(MAKE_DIRECTORY "${ASSET_DESTINATION}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${ASSET_SOURCE}" "${ASSET_DESTINATION}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot copy resources from ${ASSET_SOURCE} to ${ASSET_DESTINATION}")
endif()
