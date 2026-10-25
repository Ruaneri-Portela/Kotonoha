#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class Gameplay;
class SchoolDaysKtrfAppController;
class SchoolDaysKtrfSession;
class KtrfSceneTransaction;
}

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Kotonoha* game,
    Kotonoha::Gameplay* play,
    Kotonoha::SchoolDaysKtrfSession* temporarySession,
    Kotonoha::KtrfSceneTransaction* transaction,
    Kotonoha_Game& context);
