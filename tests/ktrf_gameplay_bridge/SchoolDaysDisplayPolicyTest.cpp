#include <Kotonoha/routing/SchoolDaysDisplayPolicy.hpp>

#include <cstdlib>
#include <iostream>

namespace {
[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysDisplayPolicyTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const char* message) {
    if (!value) Fail(message);
}
} // namespace

int main() {
    using Policy = Kotonoha::SchoolDaysDisplayPolicy;
    using Request = Policy::RetailConfigRequest;
    using Variant = Policy::Variant;
    Policy::Decision d;

    struct RetailCase {
        Policy::RetailState state;
        Variant expected;
        const char* message;
    };
    const RetailCase retailTruthTable[] = {
        {{0, 0, 0}, Variant::Standard, "retail 000 was not Standard"},
        {{0, 0, 1}, Variant::Standard, "retail 001 was not Standard"},
        {{0, 1, 0}, Variant::Standard, "retail 010 was not Standard"},
        {{0, 1, 1}, Variant::Standard, "retail 011 was not Standard"},
        {{1, 0, 0}, Variant::Wide, "retail 100 was not Wide"},
        {{1, 0, 1}, Variant::Wide, "retail 101 was not Wide"},
        {{1, 1, 0}, Variant::WideFull, "retail 110 was not WideFull"},
        {{1, 1, 1}, Variant::WideNote, "retail 111 was not WideNote"},
    };
    for (const auto& test : retailTruthTable) {
        Check(Policy::SelectRetailVariant(test.state) == test.expected,
              test.message);
    }

    // The shipped selector uses exact integer comparisons rather than boolean
    // normalization.  Lock those branch predicates so later adapters cannot
    // accidentally reinterpret arbitrary backend values as true.
    Check(Policy::SelectRetailVariant({2, 1, 0}) == Variant::Standard,
          "non-1 DisplayType did not remain Standard");
    Check(Policy::SelectRetailVariant({1, 2, 0}) == Variant::Wide,
          "non-1 WindowMode did not remain Wide");
    Check(Policy::SelectRetailVariant({1, 1, -1}) == Variant::WideNote,
          "nonzero TypeMiniNote did not remain WideNote");

    for (Variant variant : {Variant::Standard, Variant::Wide,
                            Variant::WideFull, Variant::WideNote}) {
        const auto canonical = Policy::CanonicalRetailStateForVariant(variant);
        Check(Policy::SelectRetailVariant(canonical) == variant,
              "canonical retail state did not round-trip to authored variant");
    }

    // The +0xB8 effective getter projects B in WindowMode 0 and D otherwise.
    // Keep B!=D cases explicit because Full changes the projection without
    // rewriting either DisplayType source.
    Check(Policy::ProjectRetailState({0, 1, 0, 0}).effectiveDisplayType == 0,
          "windowed projection did not use base DisplayType B");
    Check(Policy::ProjectRetailState({0, 1, 1, 0}).effectiveDisplayType == 1,
          "nonzero WindowMode projection did not use device DisplayType D");

    const Policy::RetailConfigInputs allCapabilities{0, 1, 1};

    {
        Policy::RetailBackendState state{0, 0, 0, 0};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 1,
                                               allCapabilities, &state),
              "control 4 Wide-on request was not accepted from e=0");
        Check(state.baseDisplayType == 1 && state.deviceDisplayType == 1 &&
                  state.windowMode == 0 && state.typeMiniNote == 0 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::Wide,
              "control 4 did not reproduce retail Wide-on state transition");
        const auto after = state;
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 1,
                                                allCapabilities, &state) &&
                  state.baseDisplayType == after.baseDisplayType &&
                  state.deviceDisplayType == after.deviceDisplayType &&
                  state.windowMode == after.windowMode &&
                  state.typeMiniNote == after.typeMiniNote,
              "control 4 was not inert when e was already 1");
    }

    {
        Policy::RetailBackendState state{1, 1, 0, 1};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 0,
                                               allCapabilities, &state),
              "control 5 Wide-off request was not accepted from e=1");
        Check(state.baseDisplayType == 0 && state.deviceDisplayType == 0 &&
                  state.windowMode == 0 && state.typeMiniNote == 1 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::Standard,
              "control 5 did not reproduce retail Wide-off state transition");
        const auto after = state;
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 0,
                                                allCapabilities, &state) &&
                  state.baseDisplayType == after.baseDisplayType &&
                  state.deviceDisplayType == after.deviceDisplayType &&
                  state.windowMode == after.windowMode &&
                  state.typeMiniNote == after.typeMiniNote,
              "control 5 was not inert when e was already 0");
    }

    // +CC nonzero bypasses +D0 only in WindowMode 0 and leaves D untouched.
    {
        Policy::RetailBackendState state{0, 7, 0, 0};
        const Policy::RetailConfigInputs guarded{1, 0, 1};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 1, guarded,
                                               &state) &&
                  state.baseDisplayType == 1 && state.deviceDisplayType == 7,
              "guarded control 4 did not update B while preserving D");
        state = {1, -3, 0, 0};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 0, guarded,
                                               &state) &&
                  state.baseDisplayType == 0 && state.deviceDisplayType == -3,
              "guarded control 5 did not update B while preserving D");
        state = {0, 0, 1, 0};
        const auto before = state;
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 1, guarded,
                                                &state) &&
                  state.baseDisplayType == before.baseDisplayType &&
                  state.deviceDisplayType == before.deviceDisplayType &&
                  state.windowMode == before.windowMode,
              "+CC guard accepted Wide while WindowMode was nonzero");
    }

    // With +CC zero, a fullscreen Wide request uses effective D for the
    // control gate but the consumer synchronizes both B and D to the target.
    {
        Policy::RetailBackendState state{1, 0, 1, 1};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 1,
                                               allCapabilities, &state) &&
                  state.baseDisplayType == 1 && state.deviceDisplayType == 1 &&
                  state.windowMode == 1 && state.typeMiniNote == 1 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::WideNote,
              "fullscreen control 4 did not gate on D and synchronize B/D");
        state = {0, 1, 1, 0};
        Check(Policy::ApplyRetailConfigRequest(Request::Wide, 0,
                                               allCapabilities, &state) &&
                  state.baseDisplayType == 0 && state.deviceDisplayType == 0 &&
                  state.windowMode == 1 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::Standard,
              "fullscreen control 5 did not gate on D and synchronize B/D");
    }

    // With +CC zero, +D0 is required for both Wide controls.
    {
        Policy::RetailBackendState state{0, 0, 0, 0};
        const Policy::RetailConfigInputs noWideCapability{0, 0, 1};
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 1,
                                                noWideCapability, &state) &&
                  state.baseDisplayType == 0 && state.deviceDisplayType == 0,
              "control 4 ignored the +D0 capability gate");
        state = {1, 1, 0, 0};
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 0,
                                                noWideCapability, &state) &&
                  state.baseDisplayType == 1 && state.deviceDisplayType == 1,
              "control 5 ignored the +D0 capability gate");
    }

    // Full toggles W only.  B!=D demonstrates the authenticated effective
    // DisplayType source swap: control 7 (action.value==0) enters W=1, while
    // control 6 (action.value==1) returns to W=0.
    {
        Policy::RetailBackendState state{0, 1, 0, 0};
        Check(Policy::ApplyRetailConfigRequest(Request::Full, 0,
                                               allCapabilities, &state),
              "control 7 Full request was not accepted from WindowMode 0");
        Check(state.baseDisplayType == 0 && state.deviceDisplayType == 1 &&
                  state.windowMode == 1 && state.typeMiniNote == 0 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::WideFull,
              "control 7 did not swap effective projection from B to D");
        const auto after = state;
        Check(!Policy::ApplyRetailConfigRequest(Request::Full, 0,
                                                allCapabilities, &state) &&
                  state.windowMode == after.windowMode,
              "control 7 was not inert when WindowMode was already 1");

        Check(Policy::ApplyRetailConfigRequest(Request::Full, 1,
                                               allCapabilities, &state),
              "control 6 Full request was not accepted from WindowMode 1");
        Check(state.baseDisplayType == 0 && state.deviceDisplayType == 1 &&
                  state.windowMode == 0 && state.typeMiniNote == 0 &&
                  Policy::SelectRetailVariant(Policy::ProjectRetailState(state)) ==
                      Variant::Standard,
              "control 6 did not swap effective projection from D back to B");
        const auto windowed = state;
        Check(!Policy::ApplyRetailConfigRequest(Request::Full, 1,
                                                allCapabilities, &state) &&
                  state.windowMode == windowed.windowMode,
              "control 6 was not inert when WindowMode was already 0");
    }

    {
        const Policy::RetailConfigInputs noFullCapability{0, 1, 0};
        Policy::RetailBackendState state{1, 0, 0, 1};
        Check(!Policy::ApplyRetailConfigRequest(Request::Full, 0,
                                                noFullCapability, &state) &&
                  state.windowMode == 0 && state.typeMiniNote == 1,
              "control 7 ignored the +D4 capability gate");
        state = {0, 1, 1, 1};
        Check(!Policy::ApplyRetailConfigRequest(Request::Full, 1,
                                                noFullCapability, &state) &&
                  state.windowMode == 1 && state.typeMiniNote == 1,
              "control 6 ignored the +D4 capability gate");
    }

    {
        Policy::RetailBackendState state{0, 0, 0, 0};
        Check(!Policy::ApplyRetailConfigRequest(Request::Wide, 2,
                                                allCapabilities, &state) &&
                  !Policy::ApplyRetailConfigRequest(Request::Full, -1,
                                                    allCapabilities, &state) &&
                  !Policy::ApplyRetailConfigRequest(Request::Wide, 1,
                                                    allCapabilities, nullptr),
              "non-control action values or null backend state were accepted");
    }

    Check(Policy::SelectAuto(800, 600, &d) && d.variant == Variant::Standard &&
              d.viewportWidth == 800 && d.viewportHeight == 600,
          "800x600 did not select Standard logical canvas");
    Check(Policy::SelectAuto(1280, 1024, &d) && d.variant == Variant::Standard,
          "5:4 display did not prefer Standard aspect family");
    Check(Policy::SelectAuto(800, 450, &d) && d.variant == Variant::Wide,
          "800x450 did not select low-density Wide profile");
    Check(Policy::SelectAuto(1024, 576, &d) && d.variant == Variant::WideNote,
          "1024x576 did not select WideNote density");
    Check(Policy::SelectAuto(1280, 720, &d) && d.variant == Variant::WideFull,
          "1280x720 did not select WideFull density");
    Check(Policy::SelectAuto(1366, 768, &d) && d.variant == Variant::WideFull,
          "1366x768 did not remain physical-resolution independent");
    Check(Policy::SelectAuto(1920, 1080, &d) && d.variant == Variant::WideFull,
          "1080p did not select WideFull logical profile");
    Check(Policy::SelectAuto(2560, 1440, &d) && d.variant == Variant::WideFull,
          "1440p did not select WideFull logical profile");
    Check(Policy::SelectAuto(3840, 2160, &d) && d.variant == Variant::WideFull,
          "4K did not select WideFull logical profile");
    Check(Policy::SelectAuto(3440, 1440, &d) && d.variant == Variant::WideFull &&
              d.viewportWidth == 2560 && d.viewportHeight == 1440,
          "ultrawide did not preserve 16:9 content with centered pillarbox");
    Check(Policy::SelectAuto(1920, 1200, &d) && d.variant == Variant::WideFull,
          "16:10 display did not choose higher-coverage wide family");
    Check(!Policy::SelectAuto(0, 1080, &d) &&
              !Policy::SelectAuto(1920, 0, &d),
          "invalid output dimensions were accepted");

    std::cout << "SCHOOL DAYS DISPLAY POLICY GATE PASS\n"
              << "retail_truth_table=PASS\n"
              << "retail_exact_integer_predicates=PASS\n"
              << "canonical_retail_state_roundtrip=PASS\n"
              << "retail_backend_projection_B_D=PASS\n"
              << "config_ctrl4_5_acceptance_transition=PASS\n"
              << "config_ctrl6_7_acceptance_transition=PASS\n"
              << "config_CC_D0_D4_gates=PASS\n"
              << "config_full_B_ne_D_projection_swap=PASS\n"
              << "standard_4_3_5_4=PASS\n"
              << "wide_density_800_1024_1280=PASS\n"
              << "1080p_1440p_4k=PASS\n"
              << "ultrawide_pillarbox=PASS\n"
              << "physical_resolution_unchanged=PASS\n"
              << "logical_transition_has_no_physical_size_state=PASS\n";
    return 0;
}
