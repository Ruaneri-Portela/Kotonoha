# Kotonoha Novel Engine α

This project is in the alpha phase, many functions are still being implemented, or defects

![Kotonoha Novel Engine](https://github.com/Ruaneri-Portela/Kotonoha/blob/378c55fe9ea7277847b54267e8046a66357c9b21/Kotonoha.gif)

**Kotonoha Novel Engine** is a C++ project designed for processing .ORS scripts. Its main objective is to provide a free, open-source alternative to the engine used in games by Japanese developer Overflow, known for titles like School Days. This engine is not only a reconstruction of the original, but also a versatile tool for individuals interested in creating interactive novels without prior programming experience.

The project focuses on mapping .ORS script commands to a more modern engine, supporting both Windows and Linux operating systems. It utilizes various multimedia libraries and tools to offer a comprehensive set of features for novel creation.

## Features

The Kotonoha Novel Engine includes the following capabilities:

- **Script System** (PATH): The engine parses and executes .ORS scripts.
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

Fist for all, create Dep folder in the root of the project. This folder will be used to store all dependencies.

In Dep clone IMGui, use comando below:

    mkdir Dep
    cd Dep
    git clone https://github.com/ocornut/imgui.git

### Windows MSVC

1. Dependencies

    Download the following dependencies and extract them to the Dep folder:

    - [SDL2](https://www.libsdl.org/download-2.0.php)
    - [SDL2_image](https://www.libsdl.org/projects/SDL_image/)
    - [SDL2_mixer](https://www.libsdl.org/projects/SDL_mixer/)
    - [SDL2_ttf](https://www.libsdl.org/projects/SDL_ttf/)
    - [FFMPEG](https://github.com/ShiftMediaProject/FFmpeg/releases)
    - [libAss](https://github.com/ShiftMediaProject/libass/releases)

    Attention, if the extracted file contains a folder, the folder must be pasted into "Dep", and all downlods must be to VC/MSVC!


2. Build

    - Open the Kotonoha.vcxproj
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
