# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat 

# Libraries
LIBS = -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lSDL2_mixer -lSDL2_image -lSDL2_ttf -lass -lavformat -lavcodec -lavutil

# Executable name
EXE = Kotonoha
EXT =

# Directory for imgui
IMGUI_DIR = ./imgui

# Source directory
SRC = ./MakeBuilds/

# Source files
SOURCES = Kotonoha.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp

# Object files
OBJS = $(addprefix ./MakeBuilds/, $(addsuffix .o, $(basename $(notdir $(SOURCES)))))

# Check if the OS is Windows
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Linux)
	ECHO_MESSAGE = "Linux"
	LIBS += `sdl2-config --libs`
	CXXFLAGS += `sdl2-config --cflags`
	CFLAGS = $(CXXFLAGS)
endif
ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "UCRT64 MSYS2"
	LIBS += `pkg-config --static --libs sdl2`
	CXXFLAGS += `pkg-config --cflags sdl2`
	CFLAGS = $(CXXFLAGS)
        EXT += .exe
endif

# Compile source files
./MakeBuilds/%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile imgui source files
./MakeBuilds/%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile imgui backend source files
./MakeBuilds/%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Build executable
all: $(EXE)
	@echo Build complete for Kotonoha in $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $(SRC)$@ $^ $(CXXFLAGS) $(LIBS)

# Clean build files
clean:
	rm -f ./MakeBuilds/*

cleanKot:
	rm -f ./MakeBuilds/Kotonoha*
# Run
run:
	$(SRC)$(EXE)$(EXT)

# Clean only kotonoha, compile, rim
ccr:
	make clean
	make
	make run
