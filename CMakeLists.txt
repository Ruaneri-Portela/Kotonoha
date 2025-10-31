cmake_minimum_required(VERSION 3.18)

# Define o projeto
project(Kotonoha VERSION 0.1.0 LANGUAGES C CXX)

# Diretórios de saída para os binários
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Diretório de includes
set(KOTONOHA_INCLUDE_DIR include/)

# Configurações
set(SDLTTF_FREETYPE ON)
set(SDLTTF_HARFBUZZ ON)

# Coleta os arquivos fonte
file(GLOB SOURCES
        src/*.cpp
        src/*.c
        src/components/*.cpp
        src/parsers/*.c
        src/renders/*.c
        src/utils/*.c
)

# Adiciona diretórios de bibliotecas externas
add_subdirectory(external/SDL)
add_subdirectory(external/SDL_ttf)

# Configura as bibliotecas a serem usadas
set(USE_THIS_LIBRARIES SDL3::SDL3 SDL3_ttf::SDL3_ttf)

# Configurações para sistemas não Android
if(NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)
    pkg_check_modules(LIBASS REQUIRED libass)

    link_directories( 
    ${FFMPEG_LIBRARY_DIRS} 
    ${LIBASS_LIBRARY_DIRS}
    )

    # Configuração específica para Windows
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message("Configuração para Windows")
        list(APPEND SOURCES src/Kotonoha.rc)
    endif()

    # Cria o executável
    add_executable(Kotonoha ${SOURCES})

    if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        message("Configuração para Haiku")
        target_compile_options(Kotonoha PRIVATE -fPIC)
    endif()

    # Adiciona diretórios de include
    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        ${LIBASS_INCLUDE_DIRS}
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
    )

    # Adiciona bibliotecas
    list(APPEND USE_THIS_LIBRARIES
        ${FFMPEG_LIBRARIES}
        ${LIBASS_LIBRARIES}
    )

else()
    # Configurações para Android
    message("Configuração para Android")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(ANDROID_ARCH "arm64-android")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(ANDROID_ARCH "x64-android")
    else()
        message(FATAL_ERROR "Arquitetura não suportada: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    add_library(Kotonoha SHARED ${SOURCES})

    # Adiciona diretórios de include para Android
    target_include_directories(Kotonoha PRIVATE
        ${KOTONOHA_INCLUDE_DIR}
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
        "$ENV{VCPKG_ROOT}/installed/${ANDROID_ARCH}/include"
    )

    # Adiciona diretórios de bibliotecas para Android
    link_directories(
        "$ENV{VCPKG_ROOT}/installed/${ANDROID_ARCH}/lib"
    )

    # Coleta as bibliotecas estáticas
    file(GLOB STATIC_LIBRARIES "$ENV{VCPKG_ROOT}/installed/${ANDROID_ARCH}/lib/*.a")

    # Adiciona bibliotecas estáticas e android
    list(APPEND USE_THIS_LIBRARIES
    	${STATIC_LIBRARIES}
    	android
    	mediandk
    )
    
endif()

# Linka as bibliotecas com o executável ou biblioteca
target_link_libraries(Kotonoha PRIVATE ${USE_THIS_LIBRARIES})
