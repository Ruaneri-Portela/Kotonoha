#pragma once

#include <Kotonoha/script/Timeline.hpp>
#include <string>

namespace Meo {

class TimelineParser {
public:
    static Kotonoha::TimelineScript parse(const std::string &path);
};

} // namespace Meo
