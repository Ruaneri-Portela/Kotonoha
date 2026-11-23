#include <SummerRadish/SummerAssets.hpp>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    Kotonoha::Providers::SummerAssets assets(argv[1]);
    const auto actual = assets.moviePath("DISC1/opening.mpg");
    if (!std::filesystem::is_regular_file(actual)) {
      std::cerr << "Opening movie did not resolve to regular file: " << actual
                << '\n';
      return 1;
    }
    try {
      const auto expected = assets.moviePath("SUMMER_OP.MPG");
      if (actual != expected) {
        std::cerr << "Opening alias did not resolve summer_op.mpg: " << actual
                  << " vs " << expected << '\n';
        return 1;
      }
    } catch (const std::exception &) {
      const auto expected = assets.moviePath("OPENING.MPG");
      if (actual != expected) {
        std::cerr << "Opening alias did not resolve opening.mpg: " << actual
                  << " vs " << expected << '\n';
        return 1;
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
