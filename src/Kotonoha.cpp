#define _CRT_SECURE_NO_WARNINGS
#include "Headers.h"
// This function below is a entry point
int main(int argc, char* args[])
{
	kotonoha::game initGame(argc, args);
	if (initGame.env.started) {
		kotonoha::loop *behaviour;
		while (initGame.env.config.returnCode != 1)
		{
			switch (initGame.env.config.returnCode)
			{
			case 1:
				break;
			case 2:
				behaviour = new kotonoha::loop();
				initGame.env.config.returnCode = behaviour->game(initGame.env);
				delete behaviour;
				break;
			default:
				initGame.env = kotonoha::menu(initGame.env);
				break;
			};
		};
		return initGame.env.config.returnCode;
	}
	else {
		return -1;
	}
}
