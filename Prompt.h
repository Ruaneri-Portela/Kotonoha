namespace kotonoha
{
	class questionObject
	{
	public:
		kotonohaData::acessMapper* exportTo = NULL;
		void push(std::vector<std::string> comand)
		{
			size_t index = comand.size() - 1;
			kotonohaData::questionData questionTemporary;
			questionTemporary.show = kotonohaTime::convertToTime(comand[1]);
			questionTemporary.out = kotonohaTime::convertToTime(comand[index]);
			for (size_t i = 2; i < index; i++)
			{
				questionTemporary.prompts.push_back(comand[i]);
			}
			exportTo->question.push_back(questionTemporary);
		};
	};
	class prompt
	{
	private:
		TTF_Font* font = NULL;
		SDL_Color color = { 0, 0, 0 };
		SDL_Rect rect[4] = { {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0} };
		int h = 0, w = 0;
		int bH = 0, bW = 0;
		std::vector<kotonohaData::questionData>::size_type questionsSize = 0;
		size_t returnValue = 0;
	public:
		kotonohaData::acessMapper* mapper = NULL;
		int init()
		{
			font = TTF_OpenFont("YoungSerif-Regular.ttf", 50);
			return 0;
		};
		int show(SDL_Renderer* renderer, SDL_Window* window)
		{
			if (!mapper->question.empty())
			{
				for (std::vector<kotonohaData::questionData>::size_type i = 0; i < mapper->question.size(); i++)
				{
					if (mapper->question[i].show < mapper->control->timer0.pushTime())
					{
						questionsSize = mapper->question[i].prompts.size();
						SDL_GetWindowSize(window, &w, &h);
						// Detect question to formating on screen
						switch (questionsSize)
						{
						case 1:
							rect[0] = { w / 2, h / 2, 0, 0 };
							break;
						case 2:
							rect[0] = { w / 4, h / 2, 0, 0 };
							rect[1] = { (w / 4) * 3, h / 2, 0, 0 };
							break;
						case 3:
							rect[0] = { w / 4, h / 2, 0, 0 };
							rect[1] = { w / 4 * 2, h / 2, 0, 0 };
							rect[2] = { w / 4 * 3, h / 2, 0, 0 };
							break;
						case 4:
							rect[0] = { w / 4, h / 4, 0, 0 };
							rect[1] = { w / 4, (h / 4) * 3, 0, 0 };
							rect[2] = { (w / 4) * 3, h / 4, 0, 0 };
							rect[3] = { (w / 4) * 3, (h / 4) * 3, 0, 0 };
							break;
						default:
							break;
						}
						// Draw text textures
						for (std::vector<kotonohaData::questionData>::size_type j = 0; j < questionsSize; j++)
						{
							SDL_Surface* surface = TTF_RenderUTF8_Blended(font, mapper->question[i].prompts[j].c_str(), color);
							SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
							SDL_QueryTexture(texture, NULL, NULL, &bW, &bH);
							rect[j] = { rect[j].x - bW / 2, rect[j].y - bH / 2, bW, bH };
							SDL_RenderCopy(renderer, texture, NULL, &rect[j]);
							SDL_FreeSurface(surface);
							SDL_DestroyTexture(texture);
							texture = NULL;
						}
						// Create processing system...
						//????
					}

				}
			}
			return 0;
		};
		size_t detectTouch(SDL_Event* event)
		{
			returnValue = 0;
			if (event->type == SDL_MOUSEBUTTONDOWN && !mapper->question.empty() && questionsSize !=  (size_t)0)
			{
				if (event->button.button == SDL_BUTTON_LEFT)
				{
					for (std::vector<kotonohaData::questionData>::size_type i = 0; i < questionsSize ; i++)
					{
						event->button.x > rect[i].x and event->button.x<rect[i].x + rect[i].w and event->button.y> rect[i].y and event->button.y < rect[i].y + rect[i].h ? returnValue = i + 1 : 0;
					}
				}
			}
			returnValue != 0 ? Mix_PlayChannel(0, mapper->root->soundFe0, 0) : 0;
			return returnValue;
		};
		int close()
		{
			TTF_CloseFont(font);
			return 0;
		};
	};
}
