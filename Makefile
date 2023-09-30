CXX = g++
EXE = $(a)
IMGUI_DIR = ./imgui
SRC = ./Build/
SOURCES = $(a).cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp
OBJS = $(addprefix ./Build/, $(addsuffix .o, $(basename $(notdir $(SOURCES)))))
CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat
LIBS = -lSDL2_mixer -lSDL2_image -lSDL2_ttf -lavformat -lavcodec -lavutil

ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "MinGW"
	LIBS += `pkg-config --static --libs sdl2`
	CXXFLAGS += `pkg-config --cflags sdl2`
	CFLAGS = $(CXXFLAGS)
endif

./Build/%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

./Build/%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

./Build/%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

all: $(EXE)
	
	@echo Build complete for $(a) in $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $(SRC)$@ $^ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(SRC)$(EXE) $(SRC)$(OBJS)
