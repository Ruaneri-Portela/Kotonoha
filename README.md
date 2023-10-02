# Kotonoha Novel Engine

An engine written in C++ based on reading .ORS scripts.

The engine's primary purpose is to be a free implementation of the engine used in games by Japanese developer Overflow, which has produced games such as School Days.

This project aims to map the .ORS calls into a new, more modern engine, targeting Windows and Linux. Secondly, as a general-purpose engine for those who want to make novels without programming skills. 

Functions that exist (or intend to exist in the engine) consider the items between paranthesis as accepted parameters.

## Script Capabilities

- Script system (PATH)
- Audio call (AUDIO INIT, PATH, AUDIO END) (partial implemented)
- Video call (INITIATE AUDIO, PATH, END AUDIO) (partial implemented)
- Image call (INITIATE AUDIO, PATH, END AUDIO)
- Subtitle call (INITIATE AUDIO, PATH, END AUDIO)
- Prompt of choice (A/B)
- Load a new script (PATH)

The engine uses SDL2 as the graphics and audio backend, and FFMPEG as the video backend, to game UI IMGui

The engine intends to work with .ass (alpha substation) texts as the subtitle engine. 
