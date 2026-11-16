#include <Amagami/VitaScripts.hpp>
#include <Amagami/VitaVmBridge.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
  if (argc < 2 || argc > 6) {
    std::cerr << "Usage: AmagamiVitaProbe DECRYPTED_GAME_DIR "
                 "[--boot | --probe CLASS METHOD [--instance]]\n"
                 "Diagnostic only: no Vita graphics, input or audio playback.\n";
    return 2;
  }
  try {
    Amagami::Vita::VitaScripts source(argv[1]);
    Amagami::Vm vm;
    Amagami::Vita::VitaVmBridge bridge;
    source.installAll(vm);
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &method, bool classMethod,
                                   const Amagami::VmValue &self,
                                   unsigned number,
                                   const std::vector<Amagami::VmValue> &args) {
      return bridge.primitive(owner, method, classMethod, self, number, args);
    };
    std::cout << "Vita SCF VM loaded " << source.systemCount()
              << " system classes and " << source.scenarioCount()
              << " scenario classes\n";
    if (argc == 2)
      return 0;

    std::string klass = "Application", method = "run";
    bool instance = false;
    if (std::string(argv[2]) == "--boot") {
      if (argc != 3)
        throw std::invalid_argument("--boot takes no extra arguments");
    } else if (std::string(argv[2]) == "--probe") {
      if (argc != 5 && argc != 6)
        throw std::invalid_argument("--probe expects CLASS METHOD [--instance]");
      klass = argv[3];
      method = argv[4];
      if (argc == 6) {
        if (std::string(argv[5]) != "--instance")
          throw std::invalid_argument("Unknown --probe option");
        instance = true;
      }
    } else {
      throw std::invalid_argument("Expected --boot or --probe");
    }
    const auto receiver = instance ? vm.instance(klass)
                                   : Amagami::VmValue::str(klass,
                                                           Amagami::VmValue::Class);
    if (!vm.hasClass(klass))
      throw std::runtime_error("No Vita SCF class " + klass);
    vm.call(receiver, method);
    // A strict instruction budget prevents a script without input from
    // running forever; the current experiment is NOT a gameplay loop.
    vm.run(250000);
    const auto &view = bridge.view();
    std::cout << "Vita view: scale=" << view.scale
              << " originY=" << view.originY << '\n';
    if (!vm.finished()) {
      std::cout << "Vita VM budget exhausted at " << vm.location() << '\n';
      return 3;
    }
    std::cout << "Vita VM returned from " << klass << "." << method
              << " (diagnostic only)\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Vita VM stopped: " << e.what() << '\n';
    return 1;
  }
}
