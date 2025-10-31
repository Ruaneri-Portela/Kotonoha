# Kotonoha Novel Engine α

Para português brasileiro desça!

## English

This project is in the alpha phase; many functions are still being implemented or may contain defects.

**Kotonoha Novel Engine** is a C++ project designed to process `.ORS` scripts. Its main goal is to provide a free, open-source alternative to the engine used in games by Japanese developer Overflow, known for titles like *School Days*. This engine is not only a reconstruction of the original but also a flexible tool for people who want to create interactive novels without prior programming experience.

The project maps `.ORS` script commands to a modern engine with support for Windows, Linux, and macOS. It uses modern multimedia libraries and tools to deliver a complete experience for novel creation.

## Features

* **Script System**: Parses and executes `.ORS` scripts.
* **Audio Calls**: Audio playback support.
* **Video Calls**: Video playback via FFmpeg.
* **Image Calls**: Image display support.
* **Subtitle Calls**: Subtitles via libAss.
* **Choice Prompt (A/B)**: Reader choices that affect the story.

## Technology Stack

* **SDL3**: Graphics, input, and audio backend.
* **FFmpeg**: Video and audio decoding.
* **libAss**: Subtitle rendering.

## Getting Started

To use **Kotonoha Novel Engine**, follow these steps:

1. Clone the repository:

   ```
   git clone --recursive https://github.com/Ruaneri-Portela/Kotonoha.git
   ```

   *(If you already cloned without submodules, run:)*

   ```
   git submodule update --init --recursive
   ```

2. Build the project using **CMake** as shown below for your platform.

3. Start creating or playing `.ORS`-based novels!

## Build Guide

Kotonoha now uses **CMake** and integrates dependencies through **git submodules** inside the `external` folder.

Submodules currently included:

* `SDL` (SDL3)
* `SDL_ttf` (SDL3_ttf)

Other dependencies must be installed from your package manager or prebuilt binaries:

* FFmpeg
* libAss

### Building on Windows

0. **MSVC Alternative**

   * You can compile using Visual Studio 2022 with the Kotonova.sln file.

1. **Dependencies**

   * Ensure you have CMake and Git installed.
   * Install or provide paths for:

     * FFmpeg
     * libAss 


2. **Build**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build
   ```


### Building on Linux

1. **Install dependencies**

   Example for Debian/Ubuntu:

   ```
   sudo apt install cmake g++ libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libass-dev
   ```

2. **Build**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build
   ```

### Building on macOS

1. **Install dependencies** using Homebrew:

   ```
   brew install cmake ffmpeg libass
   ```

2. **Build**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build
   ```

## License

This project is open-source under the [MIT License](LICENSE.md).

---

## Português

Este projeto está em fase alfa; muitas funções ainda estão sendo implementadas ou podem conter defeitos.

**Kotonoha Novel Engine** é um projeto em C++ projetado para processar scripts `.ORS`. Seu principal objetivo é fornecer uma alternativa gratuita e de código aberto ao motor usado em jogos da desenvolvedora japonesa Overflow, conhecida por títulos como *School Days*. Este motor não é apenas uma reconstrução do original, mas também uma ferramenta versátil para quem deseja criar novels interativas sem experiência prévia em programação.

O projeto mapeia comandos `.ORS` para um mecanismo moderno com suporte a Windows, Linux e macOS. Ele utiliza bibliotecas multimídia modernas para oferecer uma experiência completa de criação de novels.

## Características

* **Sistema de Script**: Analisa e executa scripts `.ORS`.
* **Chamadas de Áudio**: Suporte a reprodução de áudio.
* **Chamadas de Vídeo**: Reprodução de vídeo via FFmpeg.
* **Chamadas de Imagem**: Exibição de imagens na novel.
* **Chamadas de Legendas**: Renderização de legendas via libAss.
* **Prompt de Escolha (A/B)**: Permite escolhas do leitor que alteram a história.

## Tecnologias

* **SDL3**: Backend gráfico, de áudio e entrada.
* **FFmpeg**: Decodificação de vídeo e áudio.
* **IMGui**: Interface para depuração e desenvolvimento.
* **libAss**: Renderização de legendas.

## Como Começar

1. Clone o repositório:

   ```
   git clone --recursive https://github.com/Ruaneri-Portela/Kotonoha.git
   ```

   *(Se já clonou sem submódulos, use:)*

   ```
   git submodule update --init --recursive
   ```

2. Compile o projeto com o **CMake** conforme as instruções da sua plataforma.

3. Comece a criar ou jogar novels baseadas em `.ORS`!

## Guia de Compilação

O Kotonoha agora usa **CMake** e integra dependências via **submódulos git** dentro da pasta `external`.

Submódulos incluídos:

* `SDL` (SDL3)
* `SDL_ttf` (SDL3_ttf)

Outras dependências devem ser instaladas via gerenciador de pacotes ou binários prontos:

* FFmpeg
* libAss

### Windows

0. **MSVC Alternativa**

   * Pode compilar usando o Visual Studio 2022 com o arquivo Kotonova.sln

1. **Dependências**

   * Tenha CMake e Git instalados.
   * Instale ou configure caminhos para:

     * FFmpeg
     * libAss

2. **Compilar**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```

### Linux

1. **Dependências**

   ```
   sudo apt install cmake g++ libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libass-dev
   ```

2. **Compilar**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```

### macOS

1. **Dependências**

   ```
   brew install cmake ffmpeg libass
   ```

2. **Compilar**

   ```
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```

## Licença

Este projeto é de código aberto e licenciado sob a [MIT License](LICENSE.md).
