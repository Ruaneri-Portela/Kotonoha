#include "PmailBacklog.hpp"
#include <Pmail/Port.hpp>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailBacklog::PmailBacklog(Kotonoha::Engine &engine, PmailGame &game)
    : Kotonoha::VNBacklog(engine) {
}

} // namespace Kotonoha::PmailPort
