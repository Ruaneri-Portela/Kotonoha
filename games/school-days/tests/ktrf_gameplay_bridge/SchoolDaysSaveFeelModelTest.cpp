#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysSaveFeelModelTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

} // namespace

int main(int argc, char** argv) {
    using Model = Kotonoha::SchoolDaysSaveFeelModel;

    Check(Model::SelectVariant(0, 0, 0) == Model::Variant::Standard,
          "non-wide DisplayType did not select standard CMAP");
    Check(Model::SelectVariant(1, 0, 0) == Model::Variant::Wide,
          "wide non-window-mode-1 did not select wide CMAP");
    Check(Model::SelectVariant(1, 1, 0) == Model::Variant::WideFull,
          "wide/full predicate did not select Wide_Full CMAP");
    Check(Model::SelectVariant(1, 1, 1) == Model::Variant::WideNote,
          "wide/note predicate did not select Wide_Note CMAP");

    if (argc != 2) {
        std::cerr << "usage: SchoolDaysSaveFeelModelTest <assets-root>\n";
        return 2;
    }

    Model model;
    std::string error;
    if (!model.Open(argv[1], Model::Variant::Standard, &error)) Fail(error);
    Check(model.Selection() == 0, "SaveFeel did not initialize selection to 0");
    model.SetFeelings(100, 50);
    Check(!model.Interactive(), "SaveFeel became interactive before entry transition");
    model.Tick(1000u);
    Check(model.Feeling001Visible() == 0.0f &&
              model.Feeling002Visible() == 0.0f && !model.Interactive(),
          "SaveFeel entry state0 did not start from zero-width bars");
    model.Tick(1750u);
    Check(model.Feeling001Visible() == 0.5f &&
              model.Feeling002Visible() == 0.25f && !model.Interactive(),
          "SaveFeel entry interpolation at 750 ms is not native");
    model.Tick(2500u);
    Check(model.Feeling001Visible() == 1.0f &&
              model.Feeling002Visible() == 0.5f && !model.Interactive(),
          "SaveFeel entry did not reach target bars at 1500 ms");
    model.Tick(2500u);
    Check(model.Interactive(),
          "SaveFeel did not clear entry gate on the state-2 follow-up tick");

    // Do not hardcode button rectangles. Locate one real pixel for each
    // authenticated CMAP region and feed it back through RegionAt/model input.
    int oneX = -1, oneY = -1, twoX = -1, twoY = -1;
    const auto& regions = model.Cmap().Regions();
    for (uint32_t y = 0; y < model.Cmap().Height(); ++y) {
        for (uint32_t x = 0; x < model.Cmap().Width(); ++x) {
            const uint16_t id = regions[static_cast<size_t>(y) *
                                        model.Cmap().Width() + x];
            if (id == 1u && oneX < 0) {
                oneX = static_cast<int>(x);
                oneY = static_cast<int>(y);
            }
            if (id == 2u && twoX < 0) {
                twoX = static_cast<int>(x);
                twoY = static_cast<int>(y);
            }
        }
    }
    Check(oneX >= 0 && twoX >= 0, "SaveFeel CMAP lacks both choices");
    Check(!model.SetSelectionFromPointer(oneX, oneY) && model.Selection() == 0 &&
              model.HitChoice(oneX, oneY) == 0,
          "CMAP region 1 did not preserve current choice 0/SIM");
    Model::Result result = Model::Result::Skip;
    Check(model.Activate(&result) && result == Model::Result::OpenSave,
          "SaveFeel choice 0 did not produce authenticated result 4");

    Check(model.SetSelectionFromPointer(twoX, twoY) && model.Selection() == 1,
          "CMAP region 2 did not select choice 1/NAO");
    Check(model.Activate(&result) && result == Model::Result::Skip,
          "SaveFeel choice 1 did not produce authenticated result 0");

    model.Next();
    Check(model.Selection() == 0, "SaveFeel Next did not wrap 1->0");
    model.Previous();
    Check(model.Selection() == 1, "SaveFeel Previous did not wrap 0->1");
    Check(model.SetSelectionFromPointer(-1, -1) && model.Selection() == -1,
          "background/outside hover did not clear selection to native -1");
    Check(!model.Activate(&result),
          "SaveFeel activated while pointer hover selection was -1");

    // Re-open so the countdown witness has an isolated native state0.  The
    // full Tick tests above already prove entry and countdown run concurrently.
    model.Close();
    if (!model.Open(argv[1], Model::Variant::Standard, &error)) Fail(error);
    model.SetFeelings(0, 0);

    // Native countdown state 0 only samples the initial time. The next update
    // computes the visible number. Use a near-wrap start to prove DWORD-style
    // unsigned subtraction rather than a monotonic 64-bit shortcut.
    model.TickCountdown(0xfffffff0u);
    Check(model.CountdownEnabled() && model.CountdownTens() == 0 &&
              model.CountdownOnes() == 0 && !model.CountdownCloseRequested(),
          "countdown state 0 did not preserve the initial 00 sprites");
    model.TickCountdown(0xfffffff1u);
    Check(model.CountdownTens() == 3 && model.CountdownOnes() == 0,
          "countdown did not display 30 before the first second elapsed");
    model.TickCountdown(0x000003d8u); // start + 1000 modulo uint32
    Check(model.CountdownTens() == 2 && model.CountdownOnes() == 9,
          "countdown did not display 29 at elapsed 1000 ms across wrap");
    model.TickCountdown(0x0000751fu); // start + 29999 modulo uint32
    Check(model.CountdownTens() == 0 && model.CountdownOnes() == 1 &&
              !model.CountdownCloseRequested(),
          "countdown did not preserve 01 through elapsed 29999 ms");
    model.TickCountdown(0x00007520u); // start + 30000 modulo uint32
    Check(model.CountdownTens() == 0 && model.CountdownOnes() == 0 &&
              !model.CountdownCloseRequested(),
          "countdown did not display 00 before its terminal state-2 tick");
    model.TickCountdown(0x00007520u);
    Check(!model.CountdownEnabled() && model.CountdownCloseRequested(),
          "countdown state 2 did not request SaveFeel close");
    Check(model.CountdownResult(&result) && result == Model::Result::Skip,
          "countdown timeout did not preserve authenticated result 0/skip");

    std::cout << "SCHOOL DAYS SAVEFEEL MODEL GATE PASS\n"
              << "host_variant_predicates=PASS\n"
              << "cmap_region_to_choice=PASS\n"
              << "choice0_result4=PASS\n"
              << "choice1_result0=PASS\n"
              << "two_item_wrap=PASS\n"
              << "background_hover_selection_minus1=PASS\n"
              << "entry1500ms_exact=PASS\n"
              << "entry_interaction_gate=PASS\n"
              << "countdown_30s_exact=PASS\n"
              << "countdown_uint32_wrap=PASS\n"
              << "countdown_zero_then_close=PASS\n"
              << "countdown_result0_skip=PASS\n";
    return 0;
}
