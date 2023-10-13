// Files file contain the majority of custrom data structs.
namespace kotonohaData
{
    // Here is storage shared triggers for control game behaviour
    struct controlData
    {
        // To check on load files
        bool nonVideo = true;
        bool nonAudio = true;
        bool nonImage = true;
        // To chech on runtime (layers of video)
        bool display[4];
        // If thread end
        bool videoEnd = false;
        bool audioEnd = false;
        bool imageEnd = false;
        // To reset and exit signals
        int outCode = -1;
        // Universal timer
        kotonohaTime::timerEngine timer0;
        // Time to end script
        double endTime = 0;
    };
    // Auto explicative
    struct audioData
    {
        std::string path = "";
        Mix_Chunk *sound = NULL;
        double play = 0;
        double end = 0;
        bool played = false;
        int channel = 0;
    };
    // Auto explicative
    struct videoData
    {
        std::string path = "";
        double play = 0;
        double end = 0;
        bool played = false;
    };
    // Auto explicative
    struct imageData
    {
        std::string path = "";
        SDL_Texture *texture = NULL;
        bool played = false;
        double play = 0;
        double end = 0;
        bool touched = false;
    };
    // This struct contain one String, this string represent one valid .ass file
    struct textData
    {
        std::stringstream stream;
        bool init = false;
    };
    // All file path, in this struct is used to create a valid path to find assets files
    struct configsData
    {
        bool configured = false;
        char mediaPath[256] = "";
        char audioExtension[32] = "";
        char videoExtension[32] = "";
        char imageExtension[32] = "";
    };
    // rootData manager all the interactive objects
    struct rootData
    {
        SDL_Renderer *renderer = NULL;
        kotonoha::logger *log0 = NULL;
        kotonohaData::configsData *fileConfigs = NULL;
        SDL_Window *window = NULL;
        void *audio0 = NULL;
        void *video0 = NULL;
        void *image0 = NULL;
        void *text0 = NULL;
        SDL_Event event;
    };
    // AcessMapper is a struct to manager all events that can be happening in the game runtime
    struct acessMapper
    {
        controlData *control = NULL;
        std::vector<kotonohaData::videoData> video;
        std::vector<kotonohaData::imageData> image;
        std::vector<kotonohaData::audioData> audio;
        kotonohaData::textData text;
        kotonohaData::rootData *root = NULL;
    };
}
