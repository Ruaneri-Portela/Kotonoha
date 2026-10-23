#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class Gameplay;
class SchoolDaysKtrfAppController;
class SchoolDaysSceneTransaction;
}

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Gameplay* play,
    Kotonoha::SchoolDaysSceneTransaction* transaction,
    Kotonoha_Game& context);
