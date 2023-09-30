#include "Headers.h"
namespace timeUtils
{
    class timerEngine
    {
    private:
        double timePass = 0.0;
        std::chrono::time_point<std::chrono::high_resolution_clock> timeInitial;
        bool started = false;

    public:
        double pushTime()
        {
            // Retorna o tempo passado
            if (started)
            {
                clock();
                return timePass;
            }
            return 0.0;
        }

        bool initTimeCapture()
        {
            // Iniciar captura de tempo
            started = true;
            timeInitial = std::chrono::high_resolution_clock::now();
            return true;
        }

        bool clock()
        {
            // Registrar o tempo passado apenas se a captura estiver iniciada
            if (started)
            {
                std::chrono::time_point<std::chrono::high_resolution_clock> timeToCompare = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> duration = timeToCompare - timeInitial;
                timePass = duration.count();
                return true;
            }
            return false;
        }

        bool timeReset()
        {
            // Resetar o tempo decorrido e parar a contagem
            timePass = 0.0;
            started = false;
            return true;
        }
    };

    double convertToTime(std::string str)
    {
        // convert basead intro xx:xx:xx format (MM:SS:DD)
        double time = 0.0f;
        int minutes, seconds, decimeters;
        std::string token;
        std::vector<std::string> stringTimeVector;
        std::istringstream stringStream(str);
        while (std::getline(stringStream, token, ':'))
        {
            stringTimeVector.push_back(token);
        }
        minutes = atoi(stringTimeVector[0].c_str());
        seconds = atoi(stringTimeVector[1].c_str());
        decimeters = atoi(stringTimeVector[2].c_str());
        time = minutes * 60 + seconds + decimeters * 0.01;
        return time;
    }
    int convertToMs(double seconds)
    {
        int milliseconds = static_cast<int>(seconds * 1000);
        return milliseconds;
    }
    void delay(int ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
};
namespace textExtract
{
    std::vector<std::vector<std::string>> readFileLineAndTab(const std::string path)
    {
        std::vector<std::vector<std::string>> vector;
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "Fail to open file " << path << std::endl;
            return vector;
        }
        std::string linha;
        while (std::getline(file, linha))
        {
            std::istringstream linhaStream(linha);
            std::string value;
            std::vector<std::string> text;
            while (std::getline(linhaStream, value, '\t'))
            {
                if (!value.empty())
                {
                    text.push_back(value);
                }
            }
            if (!text.empty())
            {
                vector.push_back(text);
            }
        }
        file.close();
        return vector;
    }
}
struct drawControl
{
    bool videoD = true;
    bool uiD = false;
    bool sendFrame = false;
    bool videoE = false;
    bool exit = false;
    timeUtils::timerEngine timer0;
};
typedef struct soundData
{
    drawControl *dataDraw = NULL;
    Mix_Chunk *sound = NULL;
    std::string filename = "";
    double timeToPlay = 0;
    double timeToEnd = 0;
    int channel = 0;
    bool played = false;
    soundData *prev = NULL;
    soundData *next = NULL;
    timeUtils::timerEngine *timer;
} soundData;
typedef struct videoData
{
    SDL_Window *window = NULL;
    drawControl *dataDraw = NULL;
    timeUtils::timerEngine *timer;
    SDL_Renderer *renderer = NULL;
    std::string filename = "";
    double timeToPlay = 0;
    double timeToEnd = 0;
    bool played = false;
} videoData;