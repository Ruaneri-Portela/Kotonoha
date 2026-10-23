#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>
#include <Kotonoha/routing/SchoolDaysTitleRenderPlan.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysTitleModelTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

} // namespace

int main(int argc, char** argv) {
    using Model = Kotonoha::SchoolDaysTitleModel;
    using Plan = Kotonoha::SchoolDaysTitleRenderPlan;
    if (argc != 2) Fail("usage: SchoolDaysTitleModelTest <assets-root>");

    Model::HostState host{};
    Check(Model::SelectFamily(host) == Model::Family::Title,
          "default resource family mismatch");
    host.endClearVisual = true;
    Check(Model::SelectFamily(host) == Model::Family::TitleClear,
          "EndClear did not select Title_Clear");
    host.alternate = true;
    Check(Model::SelectFamily(host) == Model::Family::Title,
          "alternate gate did not override Title_Clear");
    host.ac = true;
    Check(Model::SelectFamily(host) == Model::Family::TitleAc,
          "AC gate did not win resource precedence");

    host = {};
    Check(!Model::ItemAvailable(2, host), "REVER unexpectedly enabled");
    host.endClearReplay = true;
    Check(Model::ItemAvailable(2, host), "EndClear did not enable REVER");
    host.alternate = true;
    Check(!Model::ItemAvailable(2, host), "alternate mode did not disable REVER");
    host.ac = true;
    Check(Model::ItemAvailable(5, host), "AC item5 unavailable");

    host = {};
    Model model;
    std::string error;
    Check(model.Open(argv[1], Model::Variant::Standard, host, &error), error.c_str());
    Check(model.Cmap().Width() == 800 && model.Cmap().Height() == 600,
          "standard Title CMAP dimensions mismatch");

    int hitX = -1;
    int hitY = -1;
    for (uint32_t y = 0; y < model.Cmap().Height() && hitX < 0; ++y) {
        for (uint32_t x = 0; x < model.Cmap().Width(); ++x) {
            if (model.Cmap().Get(static_cast<int>(x), static_cast<int>(y)) == 1) {
                hitX = static_cast<int>(x);
                hitY = static_cast<int>(y);
                break;
            }
        }
    }
    Check(hitX >= 0 && model.SetSelectionFromPointer(hitX, hitY) &&
              model.Selection() == 0 && model.SelectionAvailable(),
          "Title CMAP region1 did not select available item0");
    Model::Activation activation;
    Check(model.ActivateSelection(&activation) &&
              activation.result == Model::Result::Start &&
              activation.startSelector == 0,
          "Title item0 action mapping mismatch");
    Check(static_cast<int>(Model::Result::Exit) == -1 &&
              static_cast<int>(Model::Result::Start) == 1 &&
              static_cast<int>(Model::Result::Load) == 3 &&
              static_cast<int>(Model::Result::Config) == 4 &&
              static_cast<int>(Model::Result::Replay) == 5,
          "Title native result-code mapping drifted");

    // AC is a shipped-but-dormant Title family.  Its pure model contract stays
    // authenticated, while production Main deliberately keeps host.ac=false
    // because v1.02 exposes no persisted/startup source that enables it.
    Model::HostState acHost{};
    acHost.ac = true;
    Model acModel;
    Check(acModel.Open(argv[1], Model::Variant::Standard, acHost, &error),
          error.c_str());
    int acX = -1;
    int acY = -1;
    for (uint32_t y = 0; y < acModel.Cmap().Height() && acX < 0; ++y) {
        for (uint32_t x = 0; x < acModel.Cmap().Width(); ++x) {
            if (acModel.Cmap().Get(static_cast<int>(x), static_cast<int>(y)) == 6) {
                acX = static_cast<int>(x);
                acY = static_cast<int>(y);
                break;
            }
        }
    }
    Check(acX >= 0 && acModel.SetSelectionFromPointer(acX, acY) &&
              acModel.Selection() == 5 && acModel.SelectionAvailable(),
          "Title_AC CMAP did not expose authenticated dormant item5");
    Check(acModel.ActivateSelection(&activation) &&
              activation.result == Model::Result::Start &&
              activation.startSelector == 1,
          "Title_AC item5 selector1 contract drifted");

    const auto plan = Plan::Build(model);
    Check(plan.size() == 3,
          "Title plan should contain overlay, selected highlight and disabled REVER");
    Check(plan[1].source.x == 1.0f && plan[1].source.y == 1.0f &&
              plan[1].destination.x == 112.0f &&
              plan[1].destination.y == 428.0f,
          "Title Standard item0 record/yOffset mismatch");
    Check(plan[2].source.x == 235.0f && plan[2].source.y == 40.0f &&
              plan[2].destination.x == 348.0f &&
              plan[2].destination.y == 428.0f,
          "Title disabled REVER record/yOffset mismatch");

    std::cout << "SCHOOL DAYS TITLE MODEL GATE PASS\n"
              << "resource_precedence=PASS\n"
              << "item_availability=PASS\n"
              << "title_cmap=PASS\n"
              << "native_action_codes=PASS\n"
              << "start_selector0=PASS\n"
              << "dormant_ac_selector1_contract=PASS\n"
              << "standard_offset=PASS\n"
              << "disabled_rever=PASS\n";
    return 0;
}
