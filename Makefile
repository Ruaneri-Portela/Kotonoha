CXX = g++
CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat -O3
LIBS = -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lSDL2_mixer -lSDL2_image -lSDL2_ttf -lass -lavformat -lavcodec -lavutil

IMGUI_DIR = ./imgui
BUILD_DIR = ./build

SRCS = Kotonoha.cpp
SRCS += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_sdlrenderer2.cpp
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.cpp=.o)))
TARGET = $(BUILD_DIR)/Kotonoha

ifeq ($(OS),Windows_NT)
	ECHO_MESSAGE = "UCRT64 MSYS2"
	LIBS += `pkg-config --static --libs sdl2`
	CXXFLAGS += `pkg-config --cflags sdl2`
else
	ECHO_MESSAGE = "Linux"
	LIBS += `sdl2-config --libs`
	CXXFLAGS += `sdl2-config --cflags`
endif

all: $(BUILD_DIR) $(TARGET)
	
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o : src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o : $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o : $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)
	@echo Build complete for Kotonoha in $(ECHO_MESSAGE)

clean:
	rm -r $(BUILD_DIR)/*