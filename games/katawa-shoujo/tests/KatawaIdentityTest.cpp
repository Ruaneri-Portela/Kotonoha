#include <Ddlc/DdlcAssets.hpp>
#include <DdlcRuntime.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2)
    return 77;
  try {
    auto assets = std::make_shared<Kotonoha::Providers::DdlcAssets>(argv[1]);
    if (assets->name().compare(0, 13, "Katawa Shoujo") != 0)
      throw std::runtime_error("Data was not identified as Katawa Shoujo");
    Ddlc::GameRuntime runtime(assets);
    if (runtime.name != "Katawa Shoujo")
      throw std::runtime_error("Katawa window title is incorrect");
    std::cout << "Katawa runtime identity verified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
