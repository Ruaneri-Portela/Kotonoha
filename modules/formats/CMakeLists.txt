# Pure file-format decoders. One folder per FILE FORMAT under its media
# type. Every codec returns neutral RGBA/PCM data (std-library only).

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/cz" "${CMAKE_CURRENT_BINARY_DIR}/image/cz")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/tlg" "${CMAKE_CURRENT_BINARY_DIR}/image/tlg")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/tim2" "${CMAKE_CURRENT_BINARY_DIR}/image/tim2")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/tex" "${CMAKE_CURRENT_BINARY_DIR}/image/tex")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/crgb" "${CMAKE_CURRENT_BINARY_DIR}/image/crgb")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/evd" "${CMAKE_CURRENT_BINARY_DIR}/image/evd")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/image/ui-atlas" "${CMAKE_CURRENT_BINARY_DIR}/image/ui-atlas")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/audio/adpcm" "${CMAKE_CURRENT_BINARY_DIR}/audio/adpcm")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/audio/cri" "${CMAKE_CURRENT_BINARY_DIR}/audio/cri")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/audio/spu" "${CMAKE_CURRENT_BINARY_DIR}/audio/spu")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/audio/pcm-block" "${CMAKE_CURRENT_BINARY_DIR}/audio/pcm-block")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/audio/smfd" "${CMAKE_CURRENT_BINARY_DIR}/audio/smfd")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/animation/anm" "${CMAKE_CURRENT_BINARY_DIR}/animation/anm")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/data/psb" "${CMAKE_CURRENT_BINARY_DIR}/data/psb")
