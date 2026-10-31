#include "Ddlc/Python.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    try {
        Ddlc::Interp python;
        if (argc > 1) {
            const auto expected = std::filesystem::canonical(argv[1]);
            for (const char* module : {"random", "math"}) {
                const auto modulePath = std::filesystem::canonical(
                    python.getAttr(python.lookup(module), "__file__").s);
                const auto relative = modulePath.lexically_relative(expected);
                if (relative.empty() || *relative.begin() == "..")
                    throw std::runtime_error(std::string(module) +
                        " was not loaded from the bundle: " + modulePath.string());
            }
        }
        require(python.eval("sum(x * x for x in range(5))").integer() == 30,
                "CPython evaluates comprehensions");
        python.exec("class Counter:\n"
                    "    def __init__(self, value): self.value = value\n"
                    "    def add(self, amount): self.value += amount; return self.value\n"
                    "counter = Counter(3)\n");
        require(python.eval("counter.add(4)").integer() == 7, "Python class method");
        require(python.getAttr(python.lookup("counter"), "value").integer() == 7,
                "Read Python instance attributes");
        python.exec("numbers = range(4)\nnumbers.remove(2)\n"
                    "value = 1\n"
                    "def legacy_global():\n"
                    "    old = value\n"
                    "    global value\n"
                    "    value = old + 1\n"
                    "legacy_global()\n");
        require(python.eval("numbers == [0, 1, 3] and value == 2").truthy(),
                "Python 2 range and late global declarations");
        const std::string bytes("A\n\0\xff", 4);
        require(python.eval(Ddlc::Value::str(bytes).repr()).s == bytes,
                "Persistent string literals preserve arbitrary bytes");

        auto persistent = Ddlc::Interp::nameSpace("persistent");
        python.set("persistent", persistent);
        python.exec("persistent.points = [1, 2]\npersistent.points.append(3)\n");
        auto points = python.getAttr(persistent, "points");
        require(points.items().size() == 3 && points.items()[2].integer() == 3,
                "Python mutations remain visible to native code");
        python.setItem(points, Ddlc::Value::integer(0), Ddlc::Value::integer(9));
        require(python.eval("persistent.points[0]").integer() == 9,
                "Native mutations remain visible to Python");

        python.def("native_add", [](Ddlc::Interp&, Ddlc::Args& args, Ddlc::Kwargs& kwargs) {
            int64_t value = args.at(0).integer();
            for (const auto& [name, argument] : kwargs)
                if (name == "amount") value += argument.integer();
            return Ddlc::Value::integer(value);
        });
        require(python.eval("native_add(4, amount=5)").integer() == 9,
                "Python invokes native functions with keyword arguments");

        bool rejected = false;
        try {
            python.exec("import pathlib\n");
        } catch (const Ddlc::PyError&) {
            rejected = true;
        }
        require(rejected, "Unregistered game modules fail explicitly");

        python.def("yield_to_host", [](Ddlc::Interp& interpreter, Ddlc::Args&, Ddlc::Kwargs&) {
            int64_t result = 0;
            std::thread host([&] { result = interpreter.eval("6 * 7").integer(); });
            {
                Ddlc::PythonThreadRelease release;
                host.join();
            }
            return Ddlc::Value::integer(result);
        });
        require(python.eval("yield_to_host()").integer() == 42,
                "Host can use Python while a native callback is suspended");
        std::cout << "DdlcPythonTest: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DdlcPythonTest: " << error.what() << '\n';
        return 1;
    }
}
