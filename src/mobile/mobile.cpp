#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

extern "C" void Kotonoha_MobileSetupShared(const char* dataDir,
	const char* platformName) {
	const std::string directory =
		dataDir != nullptr && *dataDir != '\0' ? dataDir : ".";
	const std::string stdoutPath = directory + "/info.log";
	const std::string stderrPath = directory + "/error.log";

	const auto now = std::chrono::system_clock::now();
	const std::time_t actualTime = std::chrono::system_clock::to_time_t(now);
	std::tm localTime{};
#ifdef _WIN32
	const bool hasLocalTime = localtime_s(&localTime, &actualTime) == 0;
#else
	const bool hasLocalTime = localtime_r(&actualTime, &localTime) != nullptr;
#endif

	const bool stdoutRedirected = std::freopen(stdoutPath.c_str(), "a", stdout) != nullptr;
	const bool stderrRedirected = std::freopen(stderrPath.c_str(), "a", stderr) != nullptr;
	if (!stdoutRedirected || !stderrRedirected) {
		std::cerr << "Failed to redirect mobile logs (stdout: "
			<< (stdoutRedirected ? "ok" : "failed")
			<< ", stderr: " << (stderrRedirected ? "ok" : "failed")
			<< ").\n";
	}

	std::stringstream header;
	header << '\n' << "New execution: ";
	if (hasLocalTime) {
		header << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
	}
	else {
		header << "time unavailable";
		std::cerr << "Failed to determine local time for mobile startup log.\n";
	}
	header << "\n-------------------------\n";

	const std::string platform =
		platformName != nullptr && *platformName != '\0'
			? platformName : "Mobile";
	std::cout << header.str() << "STDOUT -> " << stdoutPath << std::endl;
	std::cerr << header.str() << "STDERR -> " << stderrPath << std::endl;
	std::cout << platform << " data directory -> " << directory << std::endl;

#ifdef _WIN32
	if (_chdir(directory.c_str()) != 0) {
#else
	if (chdir(directory.c_str()) != 0) {
#endif
		std::cerr << "Failed to change directory to " << directory << ": ";
		std::perror("");
	}
}
