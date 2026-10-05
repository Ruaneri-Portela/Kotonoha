#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class Gameplay;
class SchoolDaysKtrfAppController;
}

bool Kotonoha_SchoolDaysKtrfDebugGuiRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha::Gameplay* play,
    Kotonoha_Game& context);
