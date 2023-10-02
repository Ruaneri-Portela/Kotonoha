namespace kotonohaData
{
    typedef struct controlData
    {
        // To check on load files
        bool nonVideo = true;
        bool nonAudio = true;
        bool nonImage = true;
        // To chech on runtime (layers of video)
        bool display[4];
        // Put image to display
        bool sendFrame = false;
        // If thread end
        bool videoEnd = false;
        bool audioEnd = false;
        bool imageEnd = false;
        // To reset and exit signals
        bool exit = false;
        bool reset = false;
        bool menu = false;
        // Universal timer
        kotonohaTime::timerEngine timer0;
    } globalControl;
    typedef struct audioData
    {
        std::string path = "";
        Mix_Chunk *sound = NULL;
        double play = 0;
        double end = 0;
        bool played = false;
        int channel = 0;
    } soundData;
    typedef struct videoData
    {
        std::string path = "";
        double play = 0;
        double end = 0;
        bool played = false;
    } videoData;
    typedef struct imageData
    {
        SDL_Texture *texture;
        std::string path = "";
        bool played = false;
        double play = 0;
        double end = 0;
    } imageData;
    typedef struct rootData
    {
        SDL_Renderer *renderer = NULL;
        kotonoha::logger *log0 = NULL;
        SDL_Window *window = NULL;
        void *audio0 = NULL;
        void *video0 = NULL;
        void *image0= NULL;
        SDL_Event event;
    } rootData;
    typedef struct acessMapper
    {
        controlData *control = NULL;
        std::vector<kotonohaData::videoData> video;
        std::vector<kotonohaData::imageData> image;
        std::vector<kotonohaData::audioData> audio;
        kotonohaData::rootData *root = NULL;
    } acessMapper;
}
