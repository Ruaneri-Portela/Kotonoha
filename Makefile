PATHFILEC = Build/Kotonoha
ifeq ($(OS),Windows_NT)
    EXECUTABLE = $(PATHFILEC).exe
	
else
    EXECUTABLE = $(PATHFILEC)
endif
all:
	make compile

compile:
	gcc Kotonoha.cpp -o $(EXECUTABLE) -static-libgcc -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lSDL2main -lavformat -lavcodec -lavutil -lswscale -pthread -lSDL2 -lSDL2main -lSDL2_mixer -lSDL2_image -lSDL2_ttf

clean:
	rm -fr src/*

run:
	$(EXECUTABLE)

clean-compile-run:
	make clean
	make compile
	make run
