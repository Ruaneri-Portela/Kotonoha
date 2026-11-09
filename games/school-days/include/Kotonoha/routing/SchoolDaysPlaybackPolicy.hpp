#pragma once

namespace Kotonoha {

// Retail School Days HQ speed selector policy. The five visible button labels
// are x1/x2/x4/x16/x32 (retail gameplay capture, 2026-11-05), but the EXE
// selects internal float rates 1/2/4/12/24 from 0x004F99F0 (0x00424F90).
// Do not derive elapsed scene time from the artwork labels.
struct SchoolDaysPlaybackPolicy final {
  struct Selection {
    int displayedMultiplier = 1;
    double internalRate = 1.0;
    // EXE 0x004433D0 compares the internal rate to double 4.0 at 0x004D5080.
    // Above that threshold, 0x00443650(1) forwards message 0x8010 to the
    // retail media subsystem; the low-rate path sends 0x00443650(0).
    bool suppressNarrativeAudio = false;
  };

  static bool SelectionForIndex(int index, Selection *selection) {
    static constexpr Selection kSelections[] = {
        {1, 1.0, false},  {2, 2.0, false},  {4, 4.0, false},
        {16, 12.0, true}, {32, 24.0, true},
    };
    if (selection == nullptr || index < 0 || index >= 5)
      return false;
    *selection = kSelections[index];
    return true;
  }

  // Direct threshold for callers that have the already-selected internal rate.
  // Muting affects output gain only: do not pause media/decoder or the clock.
  static bool SuppressesNarrativeAudioAtRate(double rate) { return rate > 4.0; }

  static bool RateForIndex(int index, double *rate) {
    Selection selection;
    if (rate == nullptr || !SelectionForIndex(index, &selection))
      return false;
    *rate = selection.internalRate;
    return true;
  }
};

} // namespace Kotonoha
