#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/routing/KtrfRouter.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TraceContext {
    std::vector<uint32_t> endings;
    std::vector<uint32_t> hooks;
};

[[noreturn]] void Fail(int step, const std::string& message) {
    std::cerr << "KtrfNativeStateMatrixTrace step " << step << ": " << message << '\n';
    std::exit(2);
}

uint32_t Count(const Kotonoha_KtrfDocument& doc, const char type[4]) {
    const auto* section = Kotonoha_KtrfFindSection(&doc, type);
    return section ? section->item_count : 0u;
}

bool Integral(const Kotonoha_KtrfRuntimeValue& value, int64_t& out) {
    switch (value.kind) {
    case KOTONOHA_KTRF_RUNTIME_BOOL:
        out = value.as.boolean_value ? 1 : 0;
        return true;
    case KOTONOHA_KTRF_RUNTIME_INT64:
        out = value.as.int64_value;
        return true;
    case KOTONOHA_KTRF_RUNTIME_UINT64:
        if (value.as.uint64_value > static_cast<uint64_t>(INT64_MAX)) return false;
        out = static_cast<int64_t>(value.as.uint64_value);
        return true;
    default:
        return false;
    }
}

int OnEnding(Kotonoha_KtrfRuntime*, uint32_t index, void* userdata,
             Kotonoha_KtrfError*) {
    static_cast<TraceContext*>(userdata)->endings.push_back(index);
    return 1;
}

int OnHook(Kotonoha_KtrfRuntime*, uint32_t index,
           const Kotonoha_KtrfRuntimeValue*, uint32_t, void* userdata,
           Kotonoha_KtrfError*) {
    static_cast<TraceContext*>(userdata)->hooks.push_back(index);
    return 1;
}

void WriteU32Array(const std::vector<uint32_t>& values) {
    std::cout << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}

void WriteVariables(Kotonoha_KtrfRouter& router) {
    const uint32_t count = Count(*router.document, "VARS");
    Kotonoha_KtrfError error{};
    std::cout << '[';
    for (uint32_t i = 0; i < count; ++i) {
        Kotonoha_KtrfRuntimeValue value{};
        if (!Kotonoha_KtrfRuntimeGetVariable(&router.runtime, i, &value, &error))
            throw std::runtime_error(error.message);
        int64_t numeric = 0;
        if (!Integral(value, numeric))
            throw std::runtime_error("School Days state-matrix gate encountered non-integral VARS value");
        if (i) std::cout << ',';
        std::cout << numeric;
    }
    std::cout << ']';
}

void MarkCurrentChoicesCommitted(Kotonoha_KtrfRouter& router) {
    Kotonoha_KtrfError error{};
    for (uint32_t i = 0; i < router.choice_count; ++i) {
        Kotonoha_KtrfChoice choice{};
        if (!Kotonoha_KtrfGetChoice(router.document, i, &choice, &error))
            throw std::runtime_error(error.message);
        if (choice.node_index != router.current_node_index) continue;

        Kotonoha_KtrfRuntimeValue value{};
        if (!Kotonoha_KtrfRuntimeGetVariable(
                &router.runtime, choice.result_variable_index, &value, &error))
            throw std::runtime_error(error.message);
        router.choice_values[i] = value;
        router.choice_committed[i] = 1u;
    }
}

void Emit(Kotonoha_KtrfRouter& router, TraceContext& ctx,
          const Kotonoha_KtrfRouteResult& result, int step) {
    std::cout << '{'
              << "\"step\":" << step << ','
              << "\"status\":" << static_cast<unsigned>(result.status) << ','
              << "\"transition_index\":" << result.transition_index << ','
              << "\"source_node_index\":" << result.source_node_index << ','
              << "\"destination_node_index\":" << result.destination_node_index << ','
              << "\"current_node_index\":" << router.current_node_index << ','
              << "\"endings\":";
    WriteU32Array(ctx.endings);
    std::cout << ",\"hooks\":";
    WriteU32Array(ctx.hooks);
    std::cout << ",\"variables\":";
    WriteVariables(router);
    std::cout << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: KtrfNativeStateMatrixTrace <school-days-hq.ktnroute>\n";
        return 2;
    }

    Kotonoha_KtrfDocument doc{};
    Kotonoha_KtrfError error{};
    Kotonoha_KtrfInit(&doc);
    if (!Kotonoha_KtrfLoadFile(argv[1], &doc, &error)) {
        std::cerr << "KTRF load failed: " << error.message << '\n';
        return 2;
    }

    TraceContext ctx;
    Kotonoha_KtrfRuntimeCallbacks callbacks{};
    callbacks.register_ending = OnEnding;
    callbacks.call_hook = OnHook;
    callbacks.userdata = &ctx;

    Kotonoha_KtrfRouter router{};
    Kotonoha_KtrfRouterInit(&router);
    if (!Kotonoha_KtrfRouterBind(&router, &doc, &callbacks, &error)) {
        std::cerr << "KTRF router bind failed: " << error.message << '\n';
        Kotonoha_KtrfClean(&doc);
        return 2;
    }

    try {
        std::string line;
        int step = 0;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::istringstream input(line);
            std::string command;
            input >> command;

            if (command == "RESET") {
                ctx.endings.clear();
                ctx.hooks.clear();
                if (!Kotonoha_KtrfRouterResetEntry(&router, 0u, &error))
                    Fail(step, std::string("reset failed: ") + error.message);
                continue;
            }

            if (command == "ACTIVATE") {
                uint32_t node = KOTONOHA_KTRF_NULL_INDEX;
                if (!(input >> node)) Fail(step, "malformed ACTIVATE line: " + line);
                if (!Kotonoha_KtrfRouterActivateNode(&router, node, &error))
                    Fail(step, std::string("activate failed: ") + error.message);
                continue;
            }

            if (command == "SET") {
                uint32_t variable = KOTONOHA_KTRF_NULL_INDEX;
                int64_t numeric = 0;
                if (!(input >> variable >> numeric)) Fail(step, "malformed SET line: " + line);
                Kotonoha_KtrfRuntimeValue value{};
                value.kind = KOTONOHA_KTRF_RUNTIME_INT64;
                value.as.int64_value = numeric;
                if (!Kotonoha_KtrfRuntimeSetVariable(
                        &router.runtime, variable, &value, &error))
                    Fail(step, std::string("set failed: ") + error.message);
                continue;
            }

            if (command == "MARK_CHOICES_COMMITTED") {
                MarkCurrentChoicesCommitted(router);
                continue;
            }

            if (command == "RESOLVE") {
                ctx.endings.clear();
                ctx.hooks.clear();
                Kotonoha_KtrfRouteResult result{};
                if (!Kotonoha_KtrfRouterTrigger(
                        &router, "ktrf:next", &result, &error))
                    Fail(step, std::string("resolve failed: ") + error.message);
                Emit(router, ctx, result, step++);
                continue;
            }

            Fail(step, "unsupported command: " + command);
        }
    } catch (const std::exception& exc) {
        std::cerr << "KtrfNativeStateMatrixTrace: " << exc.what() << '\n';
        Kotonoha_KtrfRouterClean(&router);
        Kotonoha_KtrfClean(&doc);
        return 2;
    }

    Kotonoha_KtrfRouterClean(&router);
    Kotonoha_KtrfClean(&doc);
    return 0;
}
