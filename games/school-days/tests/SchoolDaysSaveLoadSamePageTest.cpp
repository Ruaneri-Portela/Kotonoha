#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Model = Kotonoha::SchoolDaysSaveLoadModel;
namespace fs = std::filesystem;

void Check(bool success, const char* message) {
    if (!success) throw std::runtime_error(message);
}

struct ScratchCmap {
    fs::path directory;
    fs::path file;

    ScratchCmap() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = fs::temp_directory_path() /
                    ("school-days-saveload-samepage-" + std::to_string(stamp));
        fs::create_directories(directory);
        file = directory / "isolated-test.cmap";
        // Real CMAP format: 32-bit little-endian width/height, then one
        // region byte per pixel. Four points encode page0/page1/slot0/slot1.
        const std::array<uint8_t, 12> bytes{{
            4, 0, 0, 0, 1, 0, 0, 0, 11, 12, 1, 2
        }};
        std::ofstream stream(file, std::ios::binary);
        Check(stream.is_open(), "Cannot create private synthetic CMAP");
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        Check(bool(stream), "Cannot write private synthetic CMAP");
    }

    ~ScratchCmap() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }
};

void Activate(Model& model, int x, Model::Output& output) {
    Check(model.SetSelectionFromPointer(x, 0), "Synthetic pointer selection failed");
    Check(model.Activate(&output), "Native activation rejected");
}
} // namespace

int main() {
    try {
        ScratchCmap cmap;
        Model model;
        std::string error;
        Check(model.Open(cmap.file.string(), Model::Variant::WideFull,
                         Model::Mode::Load, false, &error),
              "Opening synthetic LOAD model failed");

        const std::array<bool, 10> occupied{{true}};
        model.SetRowOccupancy(occupied);
        Model::Output output;

        // Regression: the runtime only refreshes occupancy after a genuine
        // page-number change. Redundant page0 click must retain current rows.
        Activate(model, 0, output); // logical control10 = current page 0
        Check(model.Page() == 0, "Redundant page click changed page");
        Activate(model, 2, output); // logical control0 = occupied slot0
        Check(output.action == Model::Action::LoadSlotRequested && output.slot == 0,
              "Redundant page0 tab invalidated occupied slot0");

        // Switching pages DOES invalidate stale row occupancy until the host
        // publishes the newly selected page's ten slot flags.
        Activate(model, 1, output); // logical control11 = page1
        Check(model.Page() == 1, "Page1 click did not change page");
        Check(model.SetSelectionFromPointer(2, 0), "Cannot select slot on page1");
        Check(!model.Activate(&output), "Page1 accepted stale page0 occupancy");
        model.SetRowOccupancy(occupied);
        Activate(model, 1, output); // redundant current page1 after refresh
        Activate(model, 2, output);
        Check(output.action == Model::Action::LoadSlotRequested && output.slot == 10,
              "Redundant page1 tab invalidated occupied slot10");

        Activate(model, 0, output); // actual page1 -> page0 transition
        Check(model.Page() == 0, "Page0 selection was not restored");
        Check(model.SetSelectionFromPointer(2, 0), "Cannot select slot after page flip");
        Check(!model.Activate(&output), "Page0 accepted stale page1 occupancy");
        model.SetRowOccupancy(occupied);
        // Again click current tab; load must continue to work.
        Activate(model, 0, output);
        Activate(model, 2, output);
        Check(output.action == Model::Action::LoadSlotRequested && output.slot == 0,
              "Revisited current tab invalidated the freshly populated page");

        std::cout << "SchoolDaysSaveLoadSamePageTest PASS\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "SchoolDaysSaveLoadSamePageTest FAIL: " << exception.what() << '\n';
        return 1;
    }
}
