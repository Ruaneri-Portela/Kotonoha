#pragma once

struct Kotonoha_Game;

namespace Kotonoha {
class SchoolDaysKtrfAppController;
class SchoolDaysKtrfSession;
}

void Kotonoha_SchoolDaysKtrfStateInspectorRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha_Game& context,
    bool* open);

void Kotonoha_SchoolDaysKtrfStateInspectorRunSession(
    Kotonoha::SchoolDaysKtrfSession* session,
    Kotonoha_Game& context,
    bool* open);
