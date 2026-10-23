#include <Kotonoha/routing/SchoolDaysSaveFeelRenderPlan.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Model = Kotonoha::SchoolDaysSaveFeelModel;
using Plan = Kotonoha::SchoolDaysSaveFeelRenderPlan;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysSaveFeelRenderPlanTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

bool Equal(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

const Plan::Sprite* Find(const std::vector<Plan::Sprite>& sprites,
                         Plan::Role role) {
    for (const auto& sprite : sprites) {
        if (sprite.role == role) return &sprite;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: SchoolDaysSaveFeelRenderPlanTest <assets-root>\n";
        return 2;
    }

    const auto standard = Plan::LayoutFor(Model::Variant::Standard);
    const auto wide = Plan::LayoutFor(Model::Variant::Wide);
    const auto full = Plan::LayoutFor(Model::Variant::WideFull);
    const auto note = Plan::LayoutFor(Model::Variant::WideNote);
    Check(Equal(standard.scale, 1.0f) && Equal(standard.yOffset, 75.0f) &&
              standard.logicalWidth == 800 && standard.logicalHeight == 600,
          "standard layout drifted");
    Check(Equal(wide.scale, 1.0f) && Equal(wide.yOffset, 0.0f) &&
              wide.logicalWidth == 800 && wide.logicalHeight == 450,
          "wide layout drifted");
    Check(Equal(full.scale, 1.6f) && full.logicalWidth == 1280 &&
              full.logicalHeight == 720,
          "wide-full layout drifted");
    Check(Equal(note.scale, 1.28f) && note.logicalWidth == 1024 &&
              note.logicalHeight == 576,
          "wide-note layout drifted");

    Model model;
    std::string error;
    if (!model.Open(argv[1], Model::Variant::Standard, &error)) Fail(error);
    model.SetFeelings(100, 50);

    // First native update: base + zero-width bars + seeded 00 digits, with no
    // selection highlight while the 1500 ms entry transition is active.
    model.Tick(1000u);
    auto sprites = Plan::Build(model);
    Check(sprites.size() == 5u, "entry render should contain base/bars/digits only");
    const auto* base = Find(sprites, Plan::Role::Base);
    Check(base != nullptr && !base->hasSource && Equal(base->destination.x, -0.5f) &&
              Equal(base->destination.y, 75.0f) && Equal(base->destination.w, 800.0f) &&
              Equal(base->destination.h, 450.0f),
          "standard base destination mismatch");
    const auto* upper = Find(sprites, Plan::Role::Feeling002Bar);
    const auto* lower = Find(sprites, Plan::Role::Feeling001Bar);
    Check(upper != nullptr && lower != nullptr && Equal(upper->destination.w, 0.0f) &&
              Equal(lower->destination.w, 0.0f),
          "entry did not start with zero-width bars");

    model.Tick(1750u);
    sprites = Plan::Build(model);
    upper = Find(sprites, Plan::Role::Feeling002Bar);
    lower = Find(sprites, Plan::Role::Feeling001Bar);
    Check(upper != nullptr && Equal(upper->source.x, 1.0f) &&
              Equal(upper->source.y, 37.0f) && Equal(upper->source.w, 604.0f) &&
              Equal(upper->destination.x, 149.5f) &&
              Equal(upper->destination.y, 445.5f) &&
              Equal(upper->destination.w, 605.0f * 0.25f) &&
              Equal(upper->destination.h, 19.0f),
          "002 upper-bar source/destination mismatch at 750 ms");
    Check(lower != nullptr && Equal(lower->source.y, 56.0f) &&
              Equal(lower->destination.y, 472.5f) &&
              Equal(lower->destination.w, 605.0f * 0.5f) &&
              Equal(lower->destination.h, 18.0f),
          "001 lower-bar source/destination mismatch at 750 ms");

    model.Tick(2500u);
    model.Tick(2500u); // native state-2 follow-up clears the interaction gate
    sprites = Plan::Build(model);
    const auto* selected = Find(sprites, Plan::Role::SelectedChoice);
    Check(selected != nullptr && Equal(selected->source.x, 1.0f) &&
              Equal(selected->destination.x, 231.5f) &&
              Equal(selected->destination.y, 348.5f) &&
              Equal(selected->destination.w, 165.0f) &&
              Equal(selected->destination.h, 36.0f),
          "selected SIM overlay mismatch");

    const auto* tens = Find(sprites, Plan::Role::CountdownTens);
    const auto* ones = Find(sprites, Plan::Role::CountdownOnes);
    // Countdown started at 1000 ms and is independent of the entry animation;
    // at this interactive witness elapsed is 1500 ms, so the native digits are
    // already 29 rather than 30.
    Check(tens != nullptr && ones != nullptr && Equal(tens->source.x, 121.0f) &&
              Equal(tens->source.y, 141.0f) && Equal(tens->destination.x, 347.5f) &&
              Equal(tens->destination.y, 225.5f) &&
              Equal(ones->source.x, 541.0f) && Equal(ones->source.y, 74.0f) &&
              Equal(ones->destination.x, 393.5f) && Equal(ones->destination.y, 224.5f),
          "countdown 29 atlas records mismatch");

    // Hover selection only changes the overlay; the normal button stays baked
    // into the base texture.
    int noX = -1, noY = -1;
    for (uint32_t y = 0; y < model.Cmap().Height() && noX < 0; ++y) {
        for (uint32_t x = 0; x < model.Cmap().Width(); ++x) {
            if (model.Cmap().Get(static_cast<int>(x), static_cast<int>(y)) == 2) {
                noX = static_cast<int>(x);
                noY = static_cast<int>(y);
                break;
            }
        }
    }
    Check(noX >= 0 && model.SetSelectionFromPointer(noX, noY),
          "could not select authenticated NAO region");
    sprites = Plan::Build(model);
    selected = Find(sprites, Plan::Role::SelectedChoice);
    Check(selected != nullptr && Equal(selected->source.x, 166.0f) &&
              Equal(selected->destination.x, 404.5f) &&
              Equal(selected->destination.w, 164.0f),
          "selected NAO overlay mismatch");

    std::cout << "SCHOOL DAYS SAVEFEEL RENDER PLAN GATE PASS\n"
              << "variant_layouts=PASS\n"
              << "standard_base=PASS\n"
              << "selected_chip_records=PASS\n"
              << "feeling_bar_destination_width=PASS\n"
              << "countdown_digit_atlas=PASS\n"
              << "entry_visibility_gate=PASS\n";
    return 0;
}
