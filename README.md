# Kotonoha Novel Engine α

Para português brasileiro desça!

## English. 

This project is in the alpha phase, many functions are still being implemented, or defects

![Kotonoha Novel Engine](https://github.com/Ruaneri-Portela/Kotonoha/blob/378c55fe9ea7277847b54267e8046a66357c9b21/Kotonoha.gif)

**Kotonoha Novel Engine** is a C++ project designed for processing .ORS scripts. Its main objective is to provide a free, open-source alternative to the engine used in games by Japanese developer Overflow, known for titles like School Days. This engine is not only a reconstruction of the original, but also a versatile tool for individuals interested in creating interactive novels without prior programming experience.

The project focuses on mapping .ORS script commands to a more modern engine, supporting both Windows and Linux operating systems. It utilizes various multimedia libraries and tools to offer a comprehensive set of features for novel creation.

## Features

The Kotonoha Novel Engine includes the following capabilities:

- **Script System**: The engine parses and executes .ORS scripts.
- **Audio Calls**: Allows for audio playback within the novel.
- **Video Calls**: Supports video playback for immersive storytelling.
- **Image Calls**: Enables the display of images within the novel.
- **Subtitle Calls**: Adds subtitles to the content.
- **Prompt of Choice (A/B)**: Allows the reader to make choices, shaping the story.

## Technology Stack

The engine utilizes the following technologies:

- **SDL2**: Serves as the graphics and audio backend.
- **FFMPEG**: Provides video processing capabilities.
- **IMGui**: Used for the game's user interface.
- **libAss**: Manages subtitle rendering.

## Getting Started

To use the Kotonoha Novel Engine, follow these steps:

1. Clone the repository:

        git clone https://github.com/Ruaneri-Portela/Kotonoha.git

2. Build the project according to your platform's requirements. See below for more information.

3. Start creating interactive novels with ease! Or play your favorite novels.

## How to build

Clone IMGui, use comando below:

    git clone https://github.com/ocornut/imgui.git

### Windows MSVC

1. Dependencies

    Download the following dependencies using VCPKG

    - SDL2
    - SDL2_image
    - SDL2_mixer
    - SDL2_ttf
    - FFMPEG
    - libAss

    If you no have VCPKG installed install it [here](https://vcpkg.io/en/getting-started), And if your have VCPKG installed by Visual Studio installer, remove and installer de git version.

    Exclame, if your have a CMake error, disable MSYS2 path ou Mingw Path, re-run comand and add VCPKG CMake path in enviroment variables. VCPK compile from git all dependencies, if your have a slow pc this action can take a long time.


2. Build

    - Open the Kotonoha.vcxproj
    - Change in Imgui Backends headers from <SDLx.h> to <SDL2/SDLx.h>
    - Select the configuration you want to build (Debug or Release)
    - Select the platform you want to build (x86 or x64)
    - Build the project

### Windows UCRT64_MSYS2/Mingw64

The entire process below must be done INSIDE the Kotonoha folder created when cloning

For building on Windows, the tutorial will use MSYS2 in the UCRT64 environment

To install MSYS2, download it from:

    winget install MSYS2.MSYS2

1. Dependencies

    If you've come this far, I assume you know how to compile C++, but if you have any doubts about MSYS2, check out [this tutorial]([https://github.com/ShiftMediaProject/libass/releases])

    The only thing to note about this tutorial is that it uses mingw64 instead of UCRT64 so change the path from mingw64 to ucrt64 and the toolchain command to mingw-w64-ucrt-x86_64-toolchain.

    With the C++ compilation properly set, download the dependencies

        pacman -S mingw-w64-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL_image mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-libass 

2. Build

    - Create the MakeBuild target directory
      
           mkdir MakeBuild
      
    - Compile
      
          make
      
     - Execute

           make run

### Linux

I could list how to download on ubuntu, debian but, no, if you're a linux user I assume you're not lazy, download g++ and the dependencies below! Needless to say, you must have already cloned IMGUI in .Dep? Look for them in dev mode in your favorite package manager!

The entire process below must be done INSIDE the Kotonoha folder created when cloning

1. Dependencies

    - SDL2
    - SDL2_image
    - SDL2_mixer
    - SDL2_ttf
    - FFMPEG
    - libAss

2. Build

    - Create the MakeBuild target directory
      
           mkdir MakeBuild
      
    - Compile
      
          make
      
     - Execute

           make run

## License

This project is open-source and is licensed under the [MIT License](LICENSE.md).

Feel free to contact us if you have any questions or need assistance.


## Português

Este projeto está em fase alfa, muitas funções ainda estão sendo implementadas ou apresentam defeitos

**Kotonoha Novel Engine** é um projeto C++ projetado para processar scripts .ORS. Seu principal objetivo é fornecer uma alternativa gratuita e de código aberto ao motor usado em jogos da desenvolvedora japonesa Overflow, conhecida por títulos como School Days. Este mecanismo não é apenas uma reconstrução do original, mas também uma ferramenta versátil para indivíduos interessados em criar romances interativos sem experiência prévia em programação.

O projeto se concentra no mapeamento de comandos de script .ORS para um mecanismo mais moderno, com suporte aos sistemas operacionais Windows e Linux. Ele utiliza várias bibliotecas e ferramentas multimídia para oferecer um conjunto abrangente de recursos para a criação de romances.

## Características

O Kotonoha Novel Engine inclui os seguintes recursos:

- **Sistema de Script**: O mecanismo analisa e executa scripts .ORS.
- **Chamadas de áudio**: permite a reprodução de áudio dentro do romance.
- **Chamadas de vídeo**: suporta reprodução de vídeo para contar histórias envolventes.
- **Chamadas de Imagens**: Permite a exibição de imagens dentro da novel.
- **Chamadas de legendas**: Adiciona legendas ao conteúdo.
- **Prompt de Escolha (A/B)**: Permite ao leitor fazer escolhas, moldando a história.

## Bibliotecas

O motor utiliza as seguintes tecnologias:

- **SDL2**: Serve como back-end gráfico e de áudio.
- **FFMPEG**: Fornece recursos de processamento de vídeo.
- **IMGui**: Usado para a interface de usuário do jogo.
- **libAss**: Gerencia a renderização de legendas.

## Começando

Para usar o Kotonoha Novel Engine, siga estas etapas:

1. Clone o repositório:

         git clone https://github.com/Ruaneri-Portela/Kotonoha.git

2. Construa o projeto de acordo com os requisitos da sua plataforma. Veja abaixo para mais informações.

3. Comece a criar romances interativos com facilidade! Ou jogue seus romances favoritos.

## Como construir

Primeiro de tudo, crie a pasta Dep na raiz do projeto. Esta pasta será usada para armazenar todas as dependências.

No Dep clone IMGui, use o comando abaixo:

     mkdir Dep
     cd ./Dep
     git clone https://github.com/ocornut/imgui.git

### Windows MSVC

1. Dependências

     Baixe as seguintes dependências e extraia-as para a pasta Dep:

     - [SDL2](https://www.libsdl.org/download-2.0.php)
     - [SDL2_image](https://www.libsdl.org/projects/SDL_image/)
     - [SDL2_mixer](https://www.libsdl.org/projects/SDL_mixer/)
     - [SDL2_ttf](https://www.libsdl.org/projects/SDL_ttf/)
     - [FFMPEG](https://github.com/ShiftMediaProject/FFmpeg/releases)
     - [libAss](https://github.com/ShiftMediaProject/libass/releases)

     Atenção, caso o arquivo extraído contenha uma pasta, a pasta deverá ser colada em "Dep", e todos os downlods deverão ser em VC/MSVC!


2. Construir

     - Abra o Kotonoha.vcxproj
     - Selecione a configuração que deseja construir (Debug ou Release)
     - Selecione a plataforma que deseja construir (x86 ou x64)
     - Construir o projeto

### Windows UCRT64_MSYS2/Mingw64

Todo o processo abaixo deve ser feito DENTRO da pasta Kotonoha criada na clonagem

Para construção no Windows, o tutorial usará MSYS2 no ambiente UCRT64

Para instalar o MSYS2, baixe-o em:

     winget install MSYS2.MSYS2

1. Dependências

     Se você chegou até aqui, presumo que saiba compilar C++, mas se tiver alguma dúvida sobre o MSYS2, confira [este tutorial]([https://github.com/ShiftMediaProject/libass/releases])

     A única coisa a notar sobre este tutorial é que ele usa mingw64 em vez de UCRT64, então mude o caminho de mingw64 para ucrt64 e o comando do conjunto de ferramentas para mingw-w64-ucrt-x86_64-toolchain.

     Com a compilação C++ configurada corretamente, baixe as dependências

         pacman -S mingw-w64-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL_image mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-libass 

2. Construir

     - Crie o diretório de destino do MakeBuild
      
            mkdir MakeBuild
      
     - Compilar
      
            make
      
      - Executar

            make build

### Linux

Eu poderia listar como fazer download no ubuntu, debian mas, não, se você é usuário linux presumo que não seja preguiçoso, baixe g++ e as dependências abaixo! Nem preciso dizer que você já deve ter clonado o IMGUI em .Dep? Procure-os no modo dev no seu gerenciador de pacotes favorito!

Todo o processo abaixo deve ser feito DENTRO da pasta Kotonoha criada na clonagem

1. Dependências

     - SDL2
     - SDL2_imagem
     - SDL2_mixer
     - SDL2_ttf
     - FFMPEG
     - libAss

2. Construir

     - Crie o diretório de destino do MakeBuild
      
            mkdir MakeBuild
      
     - Compilar
      
            make
      
      - Executar

            make build

## Licença

Este projeto é de código aberto e licenciado sob a [MIT License](LICENSE.md).

Sinta-se à vontade para entrar em contato conosco se tiver alguma dúvida ou precisar de ajuda.
