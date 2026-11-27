#include <Kotonoha/routing/SchoolDaysTitleMovieClock.hpp>

#include <cstdlib>
#include <iostream>

namespace {
using Clock = Kotonoha::SchoolDaysTitleMovieClock;
[[noreturn]] void Fail(const char* text) {
    std::cerr << "SchoolDaysTitleMovieClockTest: " << text << '\n';
    std::exit(2);
}
void Check(bool value, const char* text) { if (!value) Fail(text); }
}

int main() {
    Check(Clock::FrameIndex(0) == 0, "0ms frame mismatch");
    Check(Clock::FrameIndex(41) == 0, "41ms frame mismatch");
    Check(Clock::FrameIndex(42) == 1, "42ms frame mismatch");
    Check(Clock::FrameIndex(83) == 1, "83ms frame mismatch");
    Check(Clock::FrameIndex(84) == 2, "84ms frame mismatch");
    Check(Clock::FrameIndex(999) == 23, "999ms frame mismatch");
    Check(Clock::FrameIndex(1000) == 24, "1000ms frame mismatch");
    Check(Clock::Target100ns(1) == 416667u, "frame1 100ns rounding mismatch");
    Check(Clock::Target100ns(2) == 833333u, "frame2 100ns rounding mismatch");
    Check(Clock::TargetMilliseconds(1) == 42u, "frame1 ms projection mismatch");
    Check(Clock::TargetMilliseconds(2) == 83u, "frame2 ms projection mismatch");
    Check(Clock::TargetMilliseconds(24) == 1000u, "frame24 ms projection mismatch");
    std::cout << "SCHOOL DAYS TITLE MOVIE CLOCK GATE PASS\n"
              << "retail_24hz_boundaries=PASS\n"
              << "rounded_100ns_target=PASS\n"
              << "millisecond_projection=PASS\n";
    return 0;
}
