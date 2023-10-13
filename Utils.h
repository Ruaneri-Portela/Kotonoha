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
    long convertToMs(double seconds)
    {
        // convert seconds to milliseconds
        long milliseconds = static_cast<int>(seconds * 1000);
        return milliseconds;
    }
    void delay(int ms)
    {
        // delay in milliseconds
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
            std::vector<std::string> line;
            std::istringstream linhaStream(linha);
            std::string comand, value;
            if (std::getline(linhaStream, comand, '='))
            {
                if (std::getline(linhaStream, value))
                {
                    line.push_back(comand);
                    size_t tabPos = value.find('\t');
                    if (tabPos != std::string::npos)
                    {
                        std::istringstream valueStream(value);
                        std::string split;
                        while (std::getline(valueStream, split, '\t'))
                        {
                            if (!split.empty())
                            {
                                line.push_back(split);
                            }
                        }
                    }
                    else
                    {
                        std::istringstream valueStream(value);
                        while (std::getline(valueStream, value, ','))
                        {
                            if (!value.empty())
                            {
                                value.erase(0, 1);
                                line.push_back(value);
                            }
                        }
                    }
                }
            }
            if (line.size() > 0)
            {
                vector.push_back(line);
            }
        }
        file.close();
        return vector;
    }
    struct logger
    {
        ImGuiTextBuffer Buf;
        bool ScrollToBottom;
        bool clear;
        bool enable = false;
        void appendLog(std::string Log)
        {
            Buf.appendf("\n");
            std::cout << Log << std::endl;
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
    };
};