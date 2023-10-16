# Kotonoha Novel Engine

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

    ```bash
    git clone https://github.com/Ruaneri-Portela/Kotonoha.git
    ```

2. Build the project according to your platform's requirements. See below for more information.

3. Start creating interactive novels with ease!

## How to build

Fist for all, create Dep folder in the root of the project. This folder will be used to store all dependencies.

In Dep clone IMGui, use comando below:

    ```bash
    mkdir Dep
    cd Dep
    git clone https://github.com/ocornut/imgui.git
    ```

### Windows MSVC

1. Dependencies

 - Download the following dependencies and extract them to the Dep folder:
   - [SDL2](https://www.libsdl.org/download-2.0.php)
   - [SDL2_image](https://www.libsdl.org/projects/SDL_image/)
   - [SDL2_mixer](https://www.libsdl.org/projects/SDL_mixer/)
   - [SDL2_ttf](https://www.libsdl.org/projects/SDL_ttf/)
   - [FFMPEG](https://github.com/ShiftMediaProject/FFmpeg/releases)
   - [libAss](https://github.com/ShiftMediaProject/libass/releases)
 - Attention, if the extracted file contains a folder, the folder must be pasted into "Dep", AND all downlods must be to VC/MSVC!


2. Build

 - Open the Kotonoha.vcxproj
 - Select the configuration you want to build (Debug or Release)
 - Select the platform you want to build (x86 or x64)
 - Build the project

### Windows UCRT64_MSYS2/Mingw64

### Linux

## Contribution

Contributions to this project are welcome. Whether you are a developer, designer, or a novel enthusiast, you can help make this engine even better. Please refer to our [contribution guidelines](CONTRIBUTING.md) for more information.

## License

This project is open-source and is licensed under the [MIT License](LICENSE.md).

Feel free to contact us if you have any questions or need assistance.

Happy novel creation!

[GitHub Repository](https://github.com/Ruaneri-Portela/Kotonoha)

![Kotonoha Novel Engine](https://github.com/Ruaneri-Portela/Kotonoha/blob/378c55fe9ea7277847b54267e8046a66357c9b21/Kotonoha.gif)
