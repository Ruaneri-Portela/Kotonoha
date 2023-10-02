namespace kotonohaTime
{
    /**
     * @brief A class that provides functionality for measuring time intervals.
     */
    class timerEngine
    {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> timeInitial; /**< The initial time point. */
        double timePass = 0.0; /**< The time passed since the timer was started. */
        bool started = false; /**< A flag indicating whether the timer has been started. */

    public:
        /**
         * @brief Returns the time passed since the timer was started.
         * @return The time passed since the timer was started, or -1.0 if the timer has not been started.
         */
        double pushTime();

        /**
         * @brief Initializes the timer.
         * @return true if the timer was successfully initialized, false otherwise.
         */
        bool initTimeCapture();

        /**
         * @brief Updates the timer.
         * @return true if the timer was successfully updated, false otherwise.
         */
        bool clock();
    };

    /**
     * @brief Converts a string in the format "MM:SS:DD" to a double representing the time in seconds.
     * @param str The string to convert.
     * @return The time in seconds.
     */
    double convertToTime(std::string str);

    /**
     * @brief Converts a time in seconds to milliseconds.
     * @param seconds The time in seconds.
     * @return The time in milliseconds.
     */
    int convertToMs(double seconds);

    /**
     * @brief Delays the execution of the current thread for a specified number of milliseconds.
     * @param ms The number of milliseconds to delay the execution.
     */
    void delay(int ms);
};
namespace kotonohaTime
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
            return -1.0;
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
namespace kotonoha
{
    std::vector<std::vector<std::string>> readScript(const std::string path)
    {
        std::vector<std::vector<std::string>> vector;
        std::ifstream file(path);
        if (!file.is_open())
        {

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
    typedef struct logger
    {
        ImGuiTextBuffer Buf;
        bool ScrollToBottom;
        bool clear;
        bool enable = false;
        void appendLog(std::string Log)
        {
            Buf.appendf("\n");
            Buf.appendf(Log.c_str());
            ScrollToBottom = true;
        }
        void drawLogger()
        {
            ImGui::Begin("Logger");
            ImGui::TextUnformatted(Buf.begin());
            if (ScrollToBottom)
                ImGui::SetScrollHereY(1.0f);
            ScrollToBottom = false;
            if (ImGui::Button("Clear"))
            {
                Buf.clear();
            }
            ImGui::End();
        }
    } logger;
};