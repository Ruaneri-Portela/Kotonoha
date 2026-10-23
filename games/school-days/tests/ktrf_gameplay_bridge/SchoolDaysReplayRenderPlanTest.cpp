#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/routing/SchoolDaysReplayRenderPlan.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
[[noreturn]] void Fail(const std::string& m) { std::cerr << m << '\n'; std::exit(2); }
void Check(bool v, const char* m) { if (!v) Fail(m); }
void Near(float a, float b, const char* m) { if (std::fabs(a-b) > 0.01f) Fail(m); }

void Put32(std::ofstream& out, uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v),
        static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16),
        static_cast<unsigned char>(v >> 24)};
    out.write(reinterpret_cast<const char*>(b), 4);
}
void WriteCmap(const std::filesystem::path& root, const std::string& rel,
               int controls) {
    const auto p = root / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    Put32(out, static_cast<uint32_t>(controls + 1)); Put32(out, 1);
    for (int i=0;i<controls;++i) out.put(static_cast<char>(i+1));
    out.put(0);
}
}

int main() {
    namespace fs = std::filesystem;
    using Model = Kotonoha::SchoolDaysReplayModel;
    using Plan = Kotonoha::SchoolDaysReplayRenderPlan;
    const fs::path root = fs::temp_directory_path() / "kotonoha-replay-render-gate";
    std::error_code ec; fs::remove_all(root, ec);
    for (auto v : {Model::Variant::Standard, Model::Variant::Wide,
                   Model::Variant::WideFull, Model::Variant::WideNote}) {
        WriteCmap(root, Model::CmapRelativePath(Model::View::HScene,v),19);
        WriteCmap(root, Model::CmapRelativePath(Model::View::PlayData,v),33);
        WriteCmap(root, Model::SceneCheckCmapRelativePath(2,v),2);
        WriteCmap(root, Model::SceneCheckCmapRelativePath(4,v),4);
    }

    const auto stdLayout = Plan::LayoutFor(Model::Variant::Standard);
    const auto wide = Plan::LayoutFor(Model::Variant::Wide);
    const auto full = Plan::LayoutFor(Model::Variant::WideFull);
    const auto note = Plan::LayoutFor(Model::Variant::WideNote);
    Check(stdLayout.logicalWidth==800 && stdLayout.logicalHeight==600 &&
          stdLayout.yOffset==75.0f, "Standard replay layout drifted");
    Check(wide.logicalWidth==800 && wide.logicalHeight==450 && wide.yOffset==0.0f,
          "Wide replay layout drifted");
    Check(full.logicalWidth==1280 && full.logicalHeight==720 && full.scale==1.6f,
          "WideFull replay layout drifted");
    Check(note.logicalWidth==1024 && note.logicalHeight==576 && note.scale==1.28f,
          "WideNote replay layout drifted");

    Model m; std::string error;
    Check(m.Open(root.string(),Model::Variant::Standard,Model::View::HScene,&error),
          error.c_str());
    m.SetHSceneAvailable(0,true);
    m.SetSelectionFromPointer(7,0); // HScene tile0
    auto sprites = Plan::Build(m);
    Check(!sprites.empty() && sprites.front().role==Plan::Role::Base,
          "Replay base draw missing");
    bool placeholder=false, sepia=false, hover=false;
    int placeholderAt=-1, sepiaAt=-1, hoverAt=-1;
    for (size_t i=0;i<sprites.size();++i) {
        if (sprites[i].role==Plan::Role::LockedSlot && sprites[i].index==0) {
            placeholder=true; placeholderAt=static_cast<int>(i);
        }
        if (sprites[i].role==Plan::Role::ThumbnailSepia && sprites[i].index==0) {
            sepia=true; sepiaAt=static_cast<int>(i);
            Near(sprites[i].source.x,1.0f,"sepia src x");
            Near(sprites[i].source.y,286.0f,"sepia src y");
            Near(sprites[i].destination.x,54.5f,"sepia Standard dst x");
            Near(sprites[i].destination.y,181.5f,"sepia Standard dst y");
            Near(sprites[i].destination.w,165.0f,"sepia Standard dst w");
            Near(sprites[i].destination.h,95.0f,"sepia Standard dst h");
        }
        if (sprites[i].role==Plan::Role::ThumbnailHover && sprites[i].index==0) {
            hover=true; hoverAt=static_cast<int>(i);
            Near(sprites[i].source.y,1.0f,"hover src y");
        }
    }
    Check(placeholder && sepia && hover && placeholderAt < sepiaAt && sepiaAt < hoverAt,
          "HScene placeholder/sepia/hover draw order drifted");

    Model fullModel;
    Check(fullModel.Open(root.string(),Model::Variant::WideFull,Model::View::HScene,&error),
          error.c_str());
    fullModel.SetHSceneAvailable(0,true);
    sprites = Plan::Build(fullModel);
    for (const auto& s : sprites) if (s.role==Plan::Role::ThumbnailSepia && s.index==0) {
        Near(s.destination.x,87.2f,"WideFull thumbnail x");
        Near(s.destination.y,170.4f,"WideFull thumbnail y");
        Near(s.destination.w,264.0f,"WideFull thumbnail w");
        Near(s.destination.h,152.0f,"WideFull thumbnail h");
    }

    Check(Plan::ThumbnailRelativePath(0)=="System/Replay/Replay_Thm01.png" &&
          Plan::ThumbnailRelativePath(3)=="System/Replay/Replay_Thm04.png" &&
          Plan::ThumbnailRelativePath(4).empty(),
          "thumbnail resource mapping drifted");

    // Special absolute11 opens the authenticated four-choice SceneCheck.
    m.SetHSceneAvailable(11,true);
    m.SetSelectionFromPointer(18,0);
    Model::Action action;
    Check(m.Activate(&action) && m.SceneCheckOpen() && m.SceneCheckChoiceCount()==4,
          "four-choice SceneCheck setup failed");
    m.SetSceneCheckChoiceAvailable(0,true);
    m.SetSelectionFromPointer(0,0);
    Plan::SceneCheckVisualState pop4;
    pop4.enabled[0]=true;
    sprites=Plan::BuildSceneCheck(m,pop4);
    Check(sprites.size()==3u &&
          sprites[0].role==Plan::Role::Base &&
          sprites[1].role==Plan::Role::SceneCheckChoiceHover &&
          sprites[2].role==Plan::Role::SceneCheckPreview,
          "Pop4 draw order drifted");
    Near(sprites[1].destination.x,67.5f,"Pop4 choice0 dst x");
    Near(sprites[1].destination.y,411.5f,"Pop4 choice0 dst y");

    // Reopen ordinary model for PlayData row hover mapping.
    m.CancelSceneCheck();
    m.SetSelectionFromPointer(1,0);
    Check(m.Activate(&action) && m.CurrentView()==Model::View::PlayData,
          "PlayData tab switch failed");
    m.SetPlayDataAvailable(0,true);
    m.SetSelectionFromPointer(3,0);
    sprites=Plan::Build(m);
    bool rowHover=false;
    for (const auto& s : sprites) if (s.role==Plan::Role::PlayDataRowHover && s.index==0) {
        rowHover=true;
        Near(s.source.x,1.0f,"PlayData row src x");
        Near(s.source.y,122.0f,"PlayData row src y");
    }
    Check(rowHover,"PlayData available row hover missing");

    fs::remove_all(root,ec);
    std::cout << "SCHOOL DAYS REPLAY RENDER PLAN GATE PASS\n"
              << "logical_layouts=PASS\n"
              << "hscene_placeholder_thumbnail_order=PASS\n"
              << "widefull_transform=PASS\n"
              << "scenecheck_draw_order=PASS\n"
              << "playdata_row_hover=PASS\n";
    return 0;
}
