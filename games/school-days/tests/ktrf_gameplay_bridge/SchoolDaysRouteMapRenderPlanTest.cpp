#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Model = Kotonoha::SchoolDaysRouteMapModel;
using Plan = Kotonoha::SchoolDaysRouteMapRenderPlan;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysRouteMapRenderPlanTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) { if (!value) Fail(message); }
bool Eq(float a, float b) { return std::fabs(a - b) < 0.001f; }
bool EqRect(const Plan::Rect& a, const Plan::Rect& b) {
    return Eq(a.x,b.x) && Eq(a.y,b.y) && Eq(a.w,b.w) && Eq(a.h,b.h);
}

struct Pos { float x; float y; };
struct Page {
    int major;
    int subpage;
    int n;
    int chipW;
    int chipH;
    const Pos* pos;
};

constexpr Pos p00[]={{21,242},{186,242},{388,242},{741,242}};
constexpr Pos p10[]={{25,240},{122,334},{186,179},{239,383},{326,266},{386,266},{655,346},{741,204},{741,346}};
constexpr Pos p20[]={{48,204},{48,347},{266,204},{495,263},{533,148},{735,148},{735,263}};
constexpr Pos p21[]={{115,238},{459,165},{735,181},{734,297}};
constexpr Pos p30[]={{250,226},{315,226},{371,331},{431,152},{623,134},{623,183},{623,307},{623,392}};
constexpr Pos p31[]={{21,148},{21,263},{129,264},{244,305},{408,263},{478,263},{741,220},{741,333}};
constexpr Pos p32[]={{21,184},{21,300},{75,184},{129,184},{151,282},{309,98},{326,208},{369,242},{412,316},{481,197},{500,139},{519,87},{554,348},{741,246},{741,365}};
constexpr Pos p33[]={{129,155},{421,155},{741,133},{741,268}};
constexpr Pos p40[]={{21,134},{21,344},{147,134},{278,166},{353,196},{478,142},{569,100},{727,142},{727,255},{727,316},{727,374}};
constexpr Pos p41[]={{21,220},{21,333},{87,220},{87,333},{153,219},{155,333},{223,237},{257,141},{329,121},{428,167},{503,189},{510,372},{520,104},{523,333},{540,284},{567,205},{586,148},{602,105}};
constexpr Pos p42[]={{21,101},{21,245},{21,300},{21,366},{86,100},{155,128},{176,270},{202,338},{221,148},{248,382},{262,338},{303,118},{388,198},{409,136},{727,90},{727,231},{727,293},{727,347},{727,401}};
constexpr Pos p43[]={{21,132},{21,268},{79,268},{137,109},{251,321},{327,185},{481,165},{512,85},{521,321}};
constexpr Pos p50[]={{37,141},{37,255},{37,315},{37,374},{142,315},{292,81},{292,201},{292,289},{292,342},{441,227},{689,202}};
constexpr Pos p51[]={{37,177},{37,383},{118,100},{213,123},{377,88},{377,140},{377,189},{377,243},{377,289}};
constexpr Pos p52[]={{37,90},{37,170},{37,230},{37,292},{37,346},{37,400},{184,244},{330,76},{349,220},{349,291},{349,362},{558,220},{718,220}};
constexpr Pos p53[]={{38,121},{37,217},{37,355},{95,121},{94,217},{94,355},{155,121},{210,251},{617,80},{617,135},{617,186},{686,355}};

constexpr Page pages[]={{0,0,4,463,260,p00},{1,0,9,463,260,p10},
    {2,0,7,463,260,p20},{2,1,4,463,260,p21},
    {3,0,8,463,260,p30},{3,1,8,463,260,p31},{3,2,15,585,260,p32},{3,3,4,463,260,p33},
    {4,0,11,463,260,p40},{4,1,18,702,262,p41},{4,2,19,741,262,p42},{4,3,9,463,260,p43},
    {5,0,11,463,260,p50},{5,1,9,463,260,p51},{5,2,13,507,260,p52},{5,3,12,468,260,p53}};

constexpr Plan::Record R(float dx,float dy,float w,float h,float sx,float sy) {
    return {{sx,sy,w,h},{dx,dy,w,h}};
}
constexpr std::array<Plan::Record,33> common{{
    R(562,10,39,25,1,1),R(601,10,38,25,41,1),R(639,10,38,25,80,1),
    R(677,10,38,25,119,1),R(715,10,38,25,158,1),R(753,10,38,25,197,1),
    R(10,5,194,35,236,1),R(7,45,56,24,236,37),R(63,45,56,24,293,37),
    R(132,45,56,24,350,37),R(188,45,56,24,407,37),
    R(562,10,39,25,1,27),R(601,10,38,25,41,27),R(639,10,38,25,80,27),
    R(677,10,38,25,119,27),R(715,10,38,25,158,27),R(753,10,38,25,197,27),
    R(562,10,39,25,1,53),R(601,10,38,25,41,53),R(639,10,38,25,80,53),
    R(677,10,38,25,119,53),R(715,10,38,25,158,53),R(753,10,38,25,197,53),
    R(562,10,39,25,1,79),R(601,10,38,25,41,79),R(639,10,38,25,80,79),
    R(677,10,38,25,119,79),R(715,10,38,25,158,79),R(753,10,38,25,197,79),
    R(7,45,56,24,236,62),R(63,45,56,24,293,62),R(132,45,56,24,350,62),R(188,45,56,24,407,62)
}};

float NodeSize(int major,int subpage,int i) {
    return major==4 && (subpage==1 || subpage==2) && i==4 ? 40.0f : 38.0f;
}

void Put32(std::ofstream& out, uint32_t v) {
    const unsigned char b[4]={static_cast<unsigned char>(v),
        static_cast<unsigned char>(v>>8),static_cast<unsigned char>(v>>16),
        static_cast<unsigned char>(v>>24)};
    out.write(reinterpret_cast<const char*>(b),4);
}
void WriteCmap(const std::filesystem::path& root,const std::string& rel,int controls) {
    const auto path=root/rel;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    Put32(out,static_cast<uint32_t>(controls+1)); Put32(out,1);
    for(int i=0;i<controls;++i) out.put(static_cast<char>(i+1));
    out.put(0);
}

void CheckSpriteIdentity(const Plan::Sprite& s,Plan::Role role,int record) {
    Check(s.texture==Plan::Texture::Chip,"unexpected non-CHIP sprite");
    Check(s.role==role,"draw role/order mismatch");
    Check(s.recordIndex==record,"native record order mismatch");
}
} // namespace

int main() {
    int nodeCount=0;
    int bandCount=0;

    for(int i=0;i<33;++i) {
        const auto got=Plan::CommonRecord(i);
        Check(EqRect(got.source,common[static_cast<size_t>(i)].source) &&
              EqRect(got.destination,common[static_cast<size_t>(i)].destination),
              "common record 0..32 drifted");
    }

    for(const Page& page:pages) {
        const auto chip=Plan::ChipSizeForPage(page.major,page.subpage);
        Check(chip.width==page.chipW && chip.height==page.chipH,
              "per-page CHIP bounds drifted");
        for(int i=0;i<page.n;++i) {
            ++nodeCount;
            const float size=NodeSize(page.major,page.subpage,i);
            for(int band=0;band<4;++band) {
                ++bandCount;
                const auto b=static_cast<Plan::NodeBand>(band);
                const int expectedIndex=33+band*page.n+i;
                Check(Plan::NodeRecordIndex(page.major,page.subpage,i,b)==expectedIndex,
                      "node record index formula drifted");
                const auto record=Plan::NodeRecord(page.major,page.subpage,i,b);
                const Plan::Rect expectedSource{1.0f+39.0f*i,105.0f+39.0f*band,size,size};
                const Plan::Rect expectedDest{page.pos[i].x,page.pos[i].y,size,size};
                Check(EqRect(record.source,expectedSource) &&
                      EqRect(record.destination,expectedDest),
                      "node band record drifted");
                Check(record.source.x>=0 && record.source.y>=0 &&
                      record.source.x+record.source.w<=page.chipW &&
                      record.source.y+record.source.h<=page.chipH,
                      "node source escaped CHIP bounds");
                Check(record.destination.x>=0 && record.destination.y>=0 &&
                      record.destination.x+record.destination.w<=800 &&
                      record.destination.y+record.destination.h<=450,
                      "node destination escaped authored 800x450 canvas");
            }
        }
        for(int i=0;i<33;++i) {
            const auto& r=common[static_cast<size_t>(i)];
            Check(r.source.x+r.source.w<=page.chipW &&
                  r.source.y+r.source.h<=page.chipH,
                  "common source escaped per-page CHIP bounds");
        }
    }
    Check(nodeCount==161,"expected exactly 161 RouteMap nodes");
    Check(bandCount==644,"expected exactly 644 RouteMap node-band records");
    Check(!Plan::NodeHoverPrecedenceProven(),
          "band0 hover precedence was asserted without native proof");

    struct VariantCase { Model::Variant variant; float scale; float yOffset; int w; int h; };
    const VariantCase variants[]={{Model::Variant::Standard,1.0f,75.0f,800,600},
        {Model::Variant::Wide,1.0f,0.0f,800,450},
        {Model::Variant::WideFull,1.6f,0.0f,1280,720},
        {Model::Variant::WideNote,1.28f,0.0f,1024,576}};
    for(const auto& v:variants) {
        const auto l=Plan::LayoutFor(v.variant);
        Check(Eq(l.scale,v.scale)&&Eq(l.yOffset,v.yOffset)&&l.logicalWidth==v.w&&l.logicalHeight==v.h,
              "logical variant layout drifted");
    }

    namespace fs=std::filesystem;
    const fs::path root=fs::temp_directory_path()/"kotonoha-routemap-render-plan-gate";
    std::error_code ec; fs::remove_all(root,ec);
    for(const auto& v:variants) {
        for(const Page& page:pages)
            WriteCmap(root,Model::CmapRelativePath(page.major,page.subpage,v.variant),11+page.n);
    }

    // Exercise the complete authenticated draw-role sequence on all 16 pages.
    for(const Page& page:pages) {
        Model model; std::string error;
        Check(model.Open(root.string(),Model::Variant::Wide,page.major,page.subpage,false,&error),error.c_str());
        for(int i=0;i<page.n;++i) model.SetNodeAvailable(i,(i%2)==0);
        model.SetCurrentMarker(11+page.n-1);
        const auto sprites=Plan::Build(model);
        Check(sprites.size()==static_cast<size_t>(page.n+19),"normal draw count drifted");
        Check(sprites[0].role==Plan::Role::Base && sprites[0].texture==Plan::Texture::Base &&
              !sprites[0].hasSource,"base draw must precede CHIP passes");
        size_t cursor=1;
        for(int i=0;i<page.n;++i,++cursor) {
            const bool available=(i%2)==0;
            const auto band=available?Plan::NodeBand::Available:Plan::NodeBand::Locked;
            CheckSpriteIdentity(sprites[cursor],available?Plan::Role::NodeAvailable:Plan::Role::NodeLocked,
                                33+static_cast<int>(band)*page.n+i);
        }
        for(int record=0;record<=10;++record,++cursor)
            CheckSpriteIdentity(sprites[cursor],Plan::Role::CommonControl,record);
        CheckSpriteIdentity(sprites[cursor++],Plan::Role::NodeCurrent,33+2*page.n+(page.n-1));
        CheckSpriteIdentity(sprites[cursor++],Plan::Role::CurrentMajorPrimary,11+page.major);
        CheckSpriteIdentity(sprites[cursor++],Plan::Role::CurrentMajorSecondary,17+page.major);
        for(int record=29;record<=32;++record,++cursor)
            CheckSpriteIdentity(sprites[cursor],Plan::Role::NavigationOverlay,record);
        Check(cursor==sprites.size(),"draw-order cursor mismatch");

        // Pointer selection alone must not synthesize a band0 overlay.
        const auto before=sprites;
        model.SetSelectionFromPointer(11,0); // control11 in synthetic CMAP
        const auto after=Plan::Build(model);
        Check(after.size()==before.size(),"unproven hover precedence changed draw count");
        for(size_t i=0;i<before.size();++i)
            Check(before[i].role==after[i].role && before[i].recordIndex==after[i].recordIndex,
                  "unproven hover precedence changed authenticated draw sequence");
    }

    // Alternate host adds exactly native common records 23..28 before 29..32.
    Model alt; std::string error;
    Check(alt.Open(root.string(),Model::Variant::Standard,0,0,true,&error),error.c_str());
    const auto altSprites=Plan::Build(alt);
    Check(altSprites.size()==28u,"alternate-host draw count drifted");
    const size_t altMaskStart=1u+4u+11u+2u;
    for(int record=23;record<=28;++record)
        CheckSpriteIdentity(altSprites[altMaskStart+static_cast<size_t>(record-23)],
                            Plan::Role::AlternateHostMajorMask,record);
    for(int record=29;record<=32;++record)
        CheckSpriteIdentity(altSprites[altMaskStart+6u+static_cast<size_t>(record-29)],
                            Plan::Role::NavigationOverlay,record);

    // Exercise the authored logical transform through Build() for all four
    // variants.  No physical output/window size participates in this contract.
    for(const auto& v:variants) {
        Model transformed;
        Check(transformed.Open(root.string(),v.variant,0,0,false,&error),error.c_str());
        const auto transformedSprites=Plan::Build(transformed);
        const float expectedBaseX=-0.5f*v.scale;
        const float expectedBaseY=v.yOffset*v.scale;
        Check(Eq(transformedSprites[0].destination.x,expectedBaseX) &&
              Eq(transformedSprites[0].destination.y,expectedBaseY) &&
              Eq(transformedSprites[0].destination.w,800.0f*v.scale) &&
              Eq(transformedSprites[0].destination.h,450.0f*v.scale),
              "logical base transform drifted");
        const auto& node=transformedSprites[1];
        Check(Eq(node.destination.x,20.5f*v.scale) &&
              Eq(node.destination.y,(241.5f+v.yOffset)*v.scale) &&
              Eq(node.destination.w,39.0f*v.scale) &&
              Eq(node.destination.h,39.0f*v.scale),
              "logical node transform drifted");
    }

    fs::remove_all(root,ec);
    std::cout<<"SCHOOL DAYS ROUTEMAP RENDER PLAN GATE PASS\n"
             <<"common_records_0_32=PASS\n"
             <<"all_16_pages=PASS\n"
             <<"all_161_nodes=PASS\n"
             <<"all_644_band_records=PASS\n"
             <<"chip_and_canvas_bounds=PASS\n"
             <<"native_draw_roles_order=PASS\n"
             <<"hover_precedence_unasserted=PASS\n"
             <<"logical_variants_only=PASS\n";
    return 0;
}
