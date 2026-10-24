#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
[[noreturn]] void Fail(const std::string& m) { std::cerr << m << '\n'; std::exit(2); }
void Check(bool v, const char* m) { if (!v) Fail(m); }
void Put32(std::ofstream& out, uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v),
        static_cast<unsigned char>(v>>8), static_cast<unsigned char>(v>>16),
        static_cast<unsigned char>(v>>24)};
    out.write(reinterpret_cast<const char*>(b),4);
}
void WriteCmap(const std::filesystem::path& root, const std::string& rel, int controls) {
    const auto p=root/rel; std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p,std::ios::binary|std::ios::trunc); Put32(out,controls+1); Put32(out,1);
    for(int i=0;i<controls;++i) out.put(static_cast<char>(i+1)); out.put(0);
}
}

int main(int argc, char** argv) {
    namespace fs=std::filesystem;
    using Model=Kotonoha::SchoolDaysRouteMapModel;
    const fs::path root=fs::temp_directory_path()/"kotonoha-routemap-model-gate";
    std::error_code ec; fs::remove_all(root,ec);
    for(auto v:{Model::Variant::Standard,Model::Variant::Wide,
                Model::Variant::WideFull,Model::Variant::WideNote}) {
        for(int major=0;major<6;++major) for(int sub=0;sub<Model::SubpageCountForMajor(major);++sub) {
            const int controls=11+Model::Page(major,sub).nodeCount;
            WriteCmap(root,Model::CmapRelativePath(major,sub,v),controls);
        }
    }

    Check(Model::SubpageCountForMajor(0)==1 && Model::SubpageCountForMajor(2)==2 &&
          Model::SubpageCountForMajor(5)==4,"RouteMap subpage counts drifted");
    const auto m4p2=Model::Page(4,2);
    Check(m4p2.nodeOffset==29 && m4p2.nodeCount==19,"RouteMap M4/P2 table drifted");
    Check(Model::NodeSelector(4,2,18)==547 &&
          Model::NodeSpKey(4,2,18)=="SP547","RouteMap selector/SP formula drifted");
    Check(Model::CmapRelativePath(0,0,Model::Variant::WideFull)==
              "System/RouteMap/01/RouteMap01_Wide_Full.cmap" &&
          Model::CmapRelativePath(3,2,Model::Variant::WideNote)==
              "System/RouteMap/04/RouteMap04-3_Wide_Note.cmap",
          "RouteMap logical resource mapping drifted");

    Model m; std::string error;
    Check(m.Open(root.string(),Model::Variant::Standard,2,1,false,&error),error.c_str());
    m.SetCompletionPercent(49);
    Check(m.Major()==2 && m.Subpage()==1 && m.CurrentPageInfo().nodeOffset==7,
          "RouteMap initial state failed");

    // Crossing low->high family maps subpage1 to 2.
    m.SetSelectionFromPointer(3,0); // raw4 -> control3 major3
    Model::Action action;
    Check(m.Activate(&action,&error) && m.Major()==3 && m.Subpage()==2,
          "RouteMap low->high family subpage mapping failed");
    Check(m.CompletionPercent()==49,
          "RouteMap completion changed across major/page navigation");
    // Crossing back high->low maps subpage2 to1.
    m.SetSelectionFromPointer(2,0);
    Check(m.Activate(&action,&error) && m.Major()==2 && m.Subpage()==1,
          "RouteMap high->low family subpage mapping failed");

    // Previous/next subpage reload synchronously and clear stale projection.
    m.SetNodeAvailable(0,true);
    m.SetSelectionFromPointer(9,0); // control9 previous subpage
    Check(m.Activate(&action,&error) && m.Subpage()==0 && !m.NodeAvailable(0),
          "RouteMap previous-subpage reload/projection reset failed");
    m.SetSelectionFromPointer(10,0);
    Check(m.Activate(&action,&error) && m.Subpage()==1,
          "RouteMap next-subpage failed");
    Check(m.CompletionPercent()==49,
          "RouteMap completion changed across subpage navigation");

    // Node activation is SP-gated and emits native selector only when available.
    m.SetSelectionFromPointer(11,0);
    Check(!m.Activate(&action,&error),"locked RouteMap node activated");
    m.SetNodeAvailable(0,true);
    Check(m.Activate(&action,&error) && action.kind==Model::ActionKind::NodeRequested &&
              action.selector==307,
          "RouteMap node request selector failed");

    m.SetSelectionFromPointer(6,0);
    Check(m.Activate(&action,&error) && action.kind==Model::ActionKind::ReturnToLoad,
          "RouteMap back->LOAD contract failed");

    Model alt;
    Check(alt.Open(root.string(),Model::Variant::Standard,0,0,true,&error),error.c_str());
    Check(alt.IsControlAvailable(0) && !alt.IsControlAvailable(1) &&
          !alt.IsControlAvailable(8),"RouteMap alternate-host major gate failed");

    if (argc == 2) {
        const std::string corpus = argv[1];
        struct Probe { Model::Variant variant; int major; int subpage; int w; int h; };
        const Probe probes[] = {
            {Model::Variant::Standard, 0, 0, 800, 600},
            {Model::Variant::Wide, 2, 1, 800, 450},
            {Model::Variant::WideFull, 4, 2, 1280, 720},
            {Model::Variant::WideNote, 5, 3, 1024, 576},
        };
        for (const Probe& p : probes) {
            Model real;
            Check(real.Open(corpus,p.variant,p.major,p.subpage,false,&error),error.c_str());
            Check(static_cast<int>(real.Cmap().Width())==p.w &&
                      static_cast<int>(real.Cmap().Height())==p.h,
                  "RouteMap corpus logical CMAP dimensions drifted");
            const int lastControl = 10 + real.CurrentPageInfo().nodeCount;
            const uint16_t rawLast = static_cast<uint16_t>(lastControl + 1);
            Check(std::find(real.Cmap().Regions().begin(),
                            real.Cmap().Regions().end(), rawLast) !=
                      real.Cmap().Regions().end(),
                  "RouteMap corpus control-id range drifted");
        }
    }

    fs::remove_all(root,ec);
    std::cout << "SCHOOL DAYS ROUTEMAP MODEL GATE PASS\n"
              << "major_subpage_tables=PASS\n"
              << "cross_family_subpage_mapping=PASS\n"
              << "synchronous_reload_projection_reset=PASS\n"
              << "sp_node_selector=PASS\n"
              << "back_to_load=PASS\n"
              << "logical_variant_resources=PASS\n";
    std::cout << "completion_page_stable=PASS\n";
    if (argc == 2) std::cout << "real_corpus_four_variants=PASS\n";
    return 0;
}
