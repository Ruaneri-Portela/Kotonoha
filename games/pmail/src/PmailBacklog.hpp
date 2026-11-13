#pragma once

#include <Kotonoha/components/VNBacklog.hpp>
#include <Pmail/Port.hpp>

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * PmailBacklog is a specialized VNBacklog for PMAIL.
 */
class PmailBacklog final : public Kotonoha::VNBacklog {
public:
    PmailBacklog(Kotonoha::Engine &engine, PmailGame &game);
};

} // namespace Kotonoha::PmailPort
