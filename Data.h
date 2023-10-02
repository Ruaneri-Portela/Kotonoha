/**
 * @file Data.h
 * @brief Contains the definition of various data structures used in Kotonoha project.
 * 
 * This file contains the definition of the following data structures:
 * - controlData: A structure containing various control flags and a timer.
 * - soundData: A structure containing information about an audio file.
 * - videoData: A structure containing information about a video file.
 * - imageData: A structure containing information about an image file.
 * - rootData: A structure containing various SDL objects and a logger object.
 * - acessMapper: A structure containing pointers to various data structures.
 * 
 * All the data structures are defined inside the namespace kotonohaData.
 */
namespace kotonohaData
{
    typedef struct controlData
    {
        // To check on load files
        bool nonVideo = true;
        bool nonAudio = true;
        bool nonImage = true;
        // To chech on runtime (layers of video)
        bool display[3];
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
        // Time to end script
        double endTime = 0;
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
        SDL_Surface *surface = NULL;
        std::string path = "";
        bool played = false;
        double play = 0;
        double end = 0;
        bool touched = false;
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
