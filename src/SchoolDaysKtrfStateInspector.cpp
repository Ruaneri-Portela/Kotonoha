#include <Kotonoha/routing/SchoolDaysKtrfStateInspector.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>

extern "C" {
#include <Kotonoha/parsers/KtrfTyped.h>
#include <Kotonoha/routing/KtrfRuntime.h>
}

#include <imgui.h>
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct StateRow {
    uint32_t index = 0;
    std::string id;
    std::string type;
    std::string scope;
    std::string value;
};

char stateFilter[128] = {0};
bool changedOnly = false;
bool logChanges = false;
std::vector<std::string> baselineValues;
std::vector<std::string> observedValues;

bool ReadString(const Kotonoha_KtrfDocument* document,
                uint32_t stringIndex,
                std::string& out,
                Kotonoha_KtrfError* error) {
    const char* data = nullptr;
    size_t size = 0;
    if (!Kotonoha_KtrfGetString(document, stringIndex, &data, &size, error)) {
        return false;
    }
    out.assign(data != nullptr ? data : "", size);
    return true;
}

std::string FormatValue(const Kotonoha_KtrfRuntimeValue& value) {
    switch (value.kind) {
    case KOTONOHA_KTRF_RUNTIME_NULL:
        return "null";
    case KOTONOHA_KTRF_RUNTIME_BOOL:
        return value.as.boolean_value != 0 ? "true" : "false";
    case KOTONOHA_KTRF_RUNTIME_INT64:
        return std::to_string(static_cast<long long>(value.as.int64_value));
    case KOTONOHA_KTRF_RUNTIME_UINT64:
        return std::to_string(static_cast<unsigned long long>(value.as.uint64_value));
    case KOTONOHA_KTRF_RUNTIME_FLOAT64: {
        std::ostringstream stream;
        stream << std::setprecision(12) << value.as.float64_value;
        return stream.str();
    }
    case KOTONOHA_KTRF_RUNTIME_STRING:
        if (value.as.string_value.data == nullptr) return "\"\"";
        return std::string("\"") +
               std::string(value.as.string_value.data, value.as.string_value.size) +
               "\"";
    case KOTONOHA_KTRF_RUNTIME_BYTES:
        return "<bytes:" + std::to_string(value.as.bytes_value.size) + ">";
    case KOTONOHA_KTRF_RUNTIME_ENTITY:
        return "<entity:" + std::to_string(static_cast<unsigned>(value.entity_kind)) +
               ":" + std::to_string(static_cast<unsigned>(value.as.entity_index)) + ">";
    default:
        return "<unknown>";
    }
}

bool CollectRows(Kotonoha::SchoolDaysKtrfAppController* controller,
                 std::vector<StateRow>& rows,
                 Kotonoha_KtrfError* error) {
    rows.clear();
    if (controller == nullptr || !controller->IsOpen()) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "KTRF state inspector controller is not open");
        }
        return false;
    }

    const auto* adapter = controller->DebugAdapter();
    if (adapter == nullptr || adapter->document == nullptr ||
        adapter->router.runtime.document == nullptr) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_ARGUMENT;
            SDL_snprintf(error->message, sizeof(error->message),
                         "KTRF state inspector has no bound runtime");
        }
        return false;
    }

    const uint32_t count = adapter->router.runtime.variable_count;
    rows.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        Kotonoha_KtrfVariable metadata{};
        Kotonoha_KtrfRuntimeValue value{};
        if (!Kotonoha_KtrfGetVariable(adapter->document, i, &metadata, error) ||
            !Kotonoha_KtrfRuntimeGetVariable(&adapter->router.runtime, i, &value, error)) {
            rows.clear();
            return false;
        }

        StateRow row;
        row.index = i;
        if (!ReadString(adapter->document, metadata.id_str, row.id, error) ||
            !ReadString(adapter->document, metadata.type_str, row.type, error) ||
            !ReadString(adapter->document, metadata.scope_str, row.scope, error)) {
            rows.clear();
            return false;
        }
        row.value = FormatValue(value);
        rows.push_back(std::move(row));
    }

    return true;
}

bool MatchesFilter(const StateRow& row) {
    if (stateFilter[0] == '\0') return true;
    const std::string filter = stateFilter;
    return row.id.find(filter) != std::string::npos ||
           row.type.find(filter) != std::string::npos ||
           row.scope.find(filter) != std::string::npos ||
           row.value.find(filter) != std::string::npos;
}

void CaptureBaseline(const std::vector<StateRow>& rows) {
    baselineValues.clear();
    baselineValues.reserve(rows.size());
    for (const auto& row : rows) baselineValues.push_back(row.value);
}

void InitializeObserved(const std::vector<StateRow>& rows) {
    observedValues.clear();
    observedValues.reserve(rows.size());
    for (const auto& row : rows) observedValues.push_back(row.value);
}

} // namespace

void Kotonoha_SchoolDaysKtrfStateInspectorRun(
    Kotonoha::SchoolDaysKtrfAppController* controller,
    Kotonoha_Game&,
    bool* open) {
    if (open == nullptr || !*open) return;

    std::vector<StateRow> rows;
    Kotonoha_KtrfError error{};
    const bool ok = CollectRows(controller, rows, &error);

    ImGui::Begin("KTRF State Inspector", open);

    if (!ok) {
        ImGui::TextWrapped("Failed to read KTRF state: %s",
                           error.message[0] != '\0' ? error.message : "unknown error");
        ImGui::End();
        return;
    }

    if (baselineValues.size() != rows.size()) CaptureBaseline(rows);
    if (observedValues.size() != rows.size()) InitializeObserved(rows);

    if (logChanges) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (observedValues[i] == rows[i].value) continue;
            SDL_Log("[KTRF-STATE] var[%u] %s: %s -> %s",
                    static_cast<unsigned>(rows[i].index),
                    rows[i].id.c_str(),
                    observedValues[i].c_str(),
                    rows[i].value.c_str());
        }
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        observedValues[i] = rows[i].value;
    }

    if (ImGui::Button("Capture Baseline")) {
        CaptureBaseline(rows);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Changed only", &changedOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Log changes", &logChanges);

    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputTextWithHint("##ktrf_state_filter",
                             "filter id/type/scope/value",
                             stateFilter,
                             IM_ARRAYSIZE(stateFilter));

    std::size_t changedCount = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i < baselineValues.size() && baselineValues[i] != rows[i].value) {
            ++changedCount;
        }
    }

    ImGui::Text("Variables: %d   Changed since baseline: %d",
                static_cast<int>(rows.size()),
                static_cast<int>(changedCount));
    ImGui::TextDisabled("Workflow: Capture Baseline -> make a choice/transition -> inspect Changed only.");

    ImVec2 tableSize = ImGui::GetContentRegionAvail();
    if (tableSize.y < 220.0f) tableSize.y = 220.0f;

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("##ktrf_state_table", 6, flags, tableSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Δ", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const bool changed = i < baselineValues.size() &&
                                 baselineValues[i] != rows[i].value;
            if (changedOnly && !changed) continue;
            if (!MatchesFilter(rows[i])) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (changed) {
                ImGui::TextUnformatted("*");
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", static_cast<unsigned>(rows[i].index));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(rows[i].id.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(rows[i].type.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(rows[i].scope.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(rows[i].value.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
