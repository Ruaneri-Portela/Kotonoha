# Compiler
CXX = g++

# Executable name
EXE = $(a)

# Directory for imgui
IMGUI_DIR = ./imgui

# Source directory
SRC = ./Build/

# Source files
SOURCES = $(a).cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp

# Object files
OBJS = $(addprefix ./Build/, $(addsuffix .o, $(basename $(notdir $(SOURCES)))))

# Compiler flags
CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat 

# Libraries
LIBS = -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lSDL2main -lSDL2 -lSDL2_mixer -lSDL2_image -lSDL2_ttf -lass -lavformat -lavcodec -lavutil

UNAME_S := $(shell uname -s)
# Check if the OS is Windows
ifeq ($(UNAME_S), Linux)
	ECHO_MESSAGE = "Linux"
	LIBS += `sdl2-config --libs`
	CXXFLAGS += `sdl2-config --cflags`
	CFLAGS = $(CXXFLAGS)
endif
ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "MinGW"
#	LIBS += `pkg-config --static --libs sdl2`
#	CXXFLAGS += `pkg-config --cflags sdl2`
	CFLAGS = $(CXXFLAGS)
endif

# Compile source files
./Build/%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile imgui source files
./Build/%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile imgui backend source files
./Build/%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Build executable
all: $(EXE)
	@echo Build complete for $(a) in $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $(SRC)$@ $^ $(CXXFLAGS) $(LIBS)

# Clean build files
clean:
	rm -f ./Build/*

# Documentation
# This Makefile compiles C++ source files and imgui library files to create an executable.
# The executable name is specified by the user as a command line argument.
# The Makefile also includes flags for the SDL2 library and ffmpeg libraries.
# The Makefile checks if the OS is Windows and includes additional flags for the SDL2 library if it is.
# The Makefile also includes a clean target to remove build files.
