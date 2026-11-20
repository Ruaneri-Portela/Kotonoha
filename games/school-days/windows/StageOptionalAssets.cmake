# Windows-only staging of user-supplied, untracked School Days artwork.
# Never modify the sources or touch any GPK; missing optional art warns visibly.
foreach(_required IN ITEMS KOTONOHA_ASSET_KIND KOTONOHA_ASSET_SOURCE
                          KOTONOHA_ASSET_DESTINATION)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing stage parameter: ${_required}")
    endif()
endforeach()

if(KOTONOHA_ASSET_KIND STREQUAL "launcher")
    if(NOT EXISTS "${KOTONOHA_ASSET_SOURCE}" OR
       IS_DIRECTORY "${KOTONOHA_ASSET_SOURCE}")
        message(WARNING "School Days launcher artwork missing: ${KOTONOHA_ASSET_SOURCE}; output may be stale")
        return()
    endif()
    get_filename_component(_destination_dir "${KOTONOHA_ASSET_DESTINATION}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_dir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${KOTONOHA_ASSET_SOURCE}" "${KOTONOHA_ASSET_DESTINATION}"
        RESULT_VARIABLE _result)
elseif(KOTONOHA_ASSET_KIND STREQUAL "display-options")
    set(_selected_source "${KOTONOHA_ASSET_SOURCE}")
    if(NOT EXISTS "${_selected_source}/System/Option/Option_Display.png" OR
       NOT EXISTS "${_selected_source}/System/Option/Option_Display.cmap")
        set(_selected_source "${KOTONOHA_ASSET_FALLBACK_SOURCE}")
        if(NOT _selected_source OR
           NOT EXISTS "${_selected_source}/System/Option/Option_Display.png" OR
           NOT EXISTS "${_selected_source}/System/Option/Option_Display.cmap")
            message(WARNING "School Days Display Options PNG/CMAP missing under: ${KOTONOHA_ASSET_SOURCE}; output may be stale")
            return()
        endif()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${_selected_source}" "${KOTONOHA_ASSET_DESTINATION}"
        RESULT_VARIABLE _result)
else()
    message(FATAL_ERROR "Unknown School Days artwork kind: ${KOTONOHA_ASSET_KIND}")
endif()

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "School Days artwork staging failed (${KOTONOHA_ASSET_KIND}): ${_result}")
endif()
