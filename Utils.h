namespace timeUtils
{
    class timerEngine
    {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> timeInitial;
        double timePass = 0.0;
        bool started = false;

    public:
        double pushTime()
        {
            // Return clock time
            if (started)
            {
                clock();
                return timePass;
            }
            return 0.0;
        }
        bool initTimeCapture()
        {
            // Reset or init capture
            started = true;
            timeInitial = std::chrono::high_resolution_clock::now();
            return true;
        }
        bool clock()
        {
            // Update clock
            if (started)
            {
                std::chrono::time_point<std::chrono::high_resolution_clock> timeToCompare = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> duration = timeToCompare - timeInitial;
                timePass = duration.count();
                return true;
            }
            return false;
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
typedef struct drawControl
{
    // To check on load files
    bool nonVideo = true;
    bool nonAudio = true;
    bool nonImage = true;
    // To chech on runtime (layers of video)
    bool imageD = false;
    bool videoD = true;
    bool uiD = false;
    // Put image to display
    bool sendFrame = false;
    // If thread end
    bool videoE = false;
    bool audioE = false;
    bool imageE = false;
    // To reset and exit signals
    bool exit = false;
    bool reset = false;
    // Universal timer
    timeUtils::timerEngine timer0;
} drawControl;
typedef struct soundData
{
    drawControl *dataDraw = NULL;
    Mix_Chunk *sound = NULL;
    soundData *prev = NULL;
    soundData *next = NULL;
    std::string filename = "";
    double timeToPlay = 0;
    bool played = false;
    double timeToEnd = 0;
    int channel = 0;
    timeUtils::timerEngine *timer;
} soundData;
typedef struct videoData
{
    SDL_Renderer *renderer = NULL;
    drawControl *dataDraw = NULL;
    SDL_Window *window = NULL;
    timeUtils::timerEngine *timer;
    std::string filename = "";
    double timeToPlay = 0;
    double timeToEnd = 0;
    bool played = false;
} videoData;
typedef struct imageData
{
    SDL_Renderer *renderer = NULL;
    drawControl *dataDraw = NULL;
    SDL_Texture *texture = NULL;
    SDL_Window *window = NULL;
    imageData *next = NULL;
    imageData *prev = NULL;
    timeUtils::timerEngine *timer;
    std::string filename = "";
    bool played = false;
    double timeToPlay;
    double timeToEnd;
} imageData;