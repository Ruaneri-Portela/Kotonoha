#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class SchoolDaysKtrfAppController;
}

void Kotonoha_SchoolDaysKtrfStateInspectorRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha_Game& context,
    bool* open);
