#include <Kotonoha/routing/SchoolDaysSaveProvenance.hpp>

#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>

namespace Kotonoha {
namespace {

constexpr const char* Magic = "KOTONOHA_SCHOOL_DAYS_LXH_SAVE_V1";
constexpr size_t MaxSidecarSize = 768;

uint32_t Ror(uint32_t value, unsigned n) {
  return (value >> n) | (value << (32u - n));
}

// Portable SHA-256: local to School Days to avoid changes/dependencies in lib/.
std::string Hash(const uint8_t* data, size_t size) {
  static constexpr uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  std::array<uint32_t, 8> state{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}};
  const uint64_t bits = uint64_t(size) * 8u;
  const size_t blocks = size / 64u + ((size % 64u) < 56u ? 1u : 2u);
  for (size_t block = 0; block < blocks; ++block) {
    uint8_t bytes[64]{};
    for (size_t j = 0; j < 64; ++j) {
      const size_t index = block * 64u + j;
      if (index < size) bytes[j] = data[index];
      else if (index == size) bytes[j] = 0x80u;
      else if (index >= blocks * 64u - 8u)
        bytes[j] = uint8_t(bits >> ((blocks * 64u - 1u - index) * 8u));
    }
    uint32_t w[64]{};
    for (size_t j = 0; j < 16u; ++j)
      w[j] = (uint32_t(bytes[j * 4u]) << 24) |
             (uint32_t(bytes[j * 4u + 1u]) << 16) |
             (uint32_t(bytes[j * 4u + 2u]) << 8) |
             uint32_t(bytes[j * 4u + 3u]);
    for (size_t j = 16; j < 64u; ++j) {
      const uint32_t a = w[j-15u], b = w[j-2u];
      w[j] = w[j-16u] + (Ror(a, 7) ^ Ror(a, 18) ^ (a >> 3)) +
             w[j-7u] + (Ror(b, 17) ^ Ror(b, 19) ^ (b >> 10));
    }
    uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
    uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (size_t j=0; j<64u; ++j) {
      const uint32_t t1 = h + (Ror(e,6)^Ror(e,11)^Ror(e,25)) +
                          ((e & f) ^ (~e & g)) + k[j] + w[j];
      const uint32_t t2 = (Ror(a,2)^Ror(a,13)^Ror(a,22)) +
                          ((a & b) ^ (a & c) ^ (b & c));
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
  }
  static constexpr char alphabet[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (auto value : state)
    for (int shift = 28; shift >= 0; shift -= 4)
      hex.push_back(alphabet[(value >> shift) & 15u]);
  return hex;
}

std::string PathBinding(const std::filesystem::path& savePath, int slot) {
  const auto full = std::filesystem::absolute(savePath).lexically_normal();
  const std::string identity = "Kotonoha/SchoolDays/LxH/Save/v1:" +
      std::to_string(slot) + ":" + full.generic_u8string();
  return Hash(reinterpret_cast<const uint8_t*>(identity.data()), identity.size());
}

bool AsciiSceneKey(const std::u16string& input, std::string* out) {
  if (input.empty() || input.size() > 256u || !out) return false;
  out->clear();
  for (char16_t ch : input) {
    if (ch < 33 || ch > 126) return false;
    out->push_back(static_cast<char>(ch));
  }
  return true;
}

bool ReadDisk(const std::filesystem::path& path, std::vector<uint8_t>* bytes) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;
  const auto end = file.tellg();
  if (end < 0 || static_cast<uintmax_t>(end) >
                     static_cast<uintmax_t>(std::numeric_limits<size_t>::max()))
    return false;
  bytes->resize(static_cast<size_t>(end));
  file.seekg(0);
  if (!bytes->empty() &&
      !file.read(reinterpret_cast<char*>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size())))
    return false;
  return true;
}

bool Decimal(const std::string& value, uint64_t* result) {
  if (value.empty() || value.size() > 20) return false;
  uint64_t parsed = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9' ||
        parsed > (UINT64_MAX - uint64_t(ch - '0')) / 10u) return false;
    parsed = parsed * 10u + uint64_t(ch - '0');
  }
  *result = parsed;
  return true;
}

std::string Normalized(std::string value) {
  for (char& ch : value) {
    if (ch == '\\') ch = '/';
    else ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

} // namespace

std::string SchoolDaysSaveProvenance::Sha256(const std::vector<uint8_t>& bytes) {
  return Hash(bytes.data(), bytes.size());
}

std::string SchoolDaysSaveProvenance::SaveSceneKey(
    const std::vector<uint8_t>& bytes) {
  SchoolDaysSLog parsed;
  std::string error, scene;
  if (!SchoolDaysPersistenceCodec::ParseSLog(bytes, &parsed, &error)) return {};
  for (const auto& record : parsed.records) {
    if (record.tag != 1) continue;
    if (!AsciiSceneKey(record.name, &scene)) return {};
  }
  return scene; // The final tag1 is the live SceneKey, never historical tag4.
}

std::filesystem::path SchoolDaysSaveProvenance::SidecarPath(
    const std::filesystem::path& savePath) {
  auto sidecar = savePath;
  sidecar += ".kotonoha-lxh.v1"; // Preserve native Unicode Windows path.
  return sidecar;
}

SchoolDaysSaveProvenance::Origin SchoolDaysSaveProvenance::Capture(
    const std::string& sceneKey, const Kotonoha_orsData& script, uint32_t tick,
    bool temporaryPlayback, bool pendingHandoff,
    bool pendingSceneAdvance, bool movieStarted) {
  Origin result{};
  if (temporaryPlayback || pendingHandoff ||
      pendingSceneAdvance || !movieStarted) return result;
  const auto key = Normalized(sceneKey);
  std::string ending;
  for (const auto& candidate : SchoolDaysLxhContent::Endings)
    if (key == "05/" + Normalized(std::string(candidate)))
      ending = Normalized(std::string(candidate));
  if (ending.empty()) return result;
  const std::string moviePath = "lxh/media/" + ending + "-lxh";
  int credits = 0, movies = 0, nexts = 0;
  uint32_t creditsEnd = 0, movieStart = 0, nextTick = 0;
  bool touched = false;
  for (const auto* event = script.data; event; event = event->next) {
    if (event->command == END_ROLL && event->data.path_end) {
      ++credits;
      creditsEnd = event->endTick;
    } else if (event->command == PLAY_MOVIE && event->data.play_movie &&
               event->data.play_movie->path &&
               Normalized(event->data.play_movie->path).find(moviePath) !=
                   std::string::npos) {
      ++movies;
      movieStart = event->startTick;
      touched = event->eventTouched;
    } else if (event->command == Next) {
      ++nexts;
      nextTick = event->startTick;
    }
  }
  if (credits != 1 || movies != 1 || nexts != 1 || !touched ||
      movieStart < creditsEnd || nextTick <= movieStart ||
      tick < movieStart || tick >= nextTick) return result;
  return {true, tick, movieStart, nextTick};
}

bool SchoolDaysSaveProvenance::Invalidate(
    const std::filesystem::path& savePath, std::string* error) {
  if (error) error->clear();
  std::error_code ec;
  (void)std::filesystem::remove(SidecarPath(savePath), ec);
  if (ec) {
    if (error) *error = "Cannot invalidate School Days LxH save provenance: " +
                        ec.message();
    return false;
  }
  return true;
}

bool SchoolDaysSaveProvenance::Publish(
    const std::filesystem::path& savePath, int slot,
    const std::vector<uint8_t>& savedBytes, Origin origin, std::string* error) {
  if (error) error->clear();
  const auto scene = SaveSceneKey(savedBytes);
  if (scene.empty() || slot < -1 || slot >= 100) {
    if (error) *error = "Cannot attest an invalid School Days SaveFile";
    return false;
  }
  if (origin.lxhEpilogue &&
      (origin.movieStartTick > origin.tick ||
       origin.tick >= origin.nextTick ||
       std::find_if(SchoolDaysLxhContent::Endings.begin(),
                    SchoolDaysLxhContent::Endings.end(), [&](auto ending) {
         return Normalized(scene) == "05/" + Normalized(std::string(ending));
       }) == SchoolDaysLxhContent::Endings.end())) {
    if (error) *error = "Invalid LxH epilogue provenance boundary";
    return false;
  }
  if (!origin.lxhEpilogue) origin = {};
  std::vector<uint8_t> onDisk;
  if (!ReadDisk(savePath, &onDisk) || onDisk != savedBytes) {
    if (error) *error = "School Days saved bytes changed before provenance publish";
    return false;
  }
  std::ostringstream payload;
  payload << Magic << '\n'
          << "binding=" << PathBinding(savePath, slot) << '\n'
          << "length=" << savedBytes.size() << '\n'
          << "sha256=" << Sha256(savedBytes) << '\n'
          << "scene=" << scene << '\n'
          << "lxh=" << (origin.lxhEpilogue ? 1 : 0) << '\n'
          << "tick=" << origin.tick << '\n'
          << "movie_start=" << origin.movieStartTick << '\n'
          << "next=" << origin.nextTick << '\n';
  const auto sidecar = SidecarPath(savePath);
  auto temporary = sidecar;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << payload.str();
    output.close();
    if (!output) {
      std::error_code ignored;
      (void)std::filesystem::remove(temporary, ignored);
      if (error) *error = "Cannot write School Days LxH provenance temporary file";
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(temporary, sidecar, ec);
  if (ec) {
    std::error_code ignored;
    (void)std::filesystem::remove(temporary, ignored);
    if (error) *error = "Cannot publish School Days LxH provenance: " + ec.message();
    return false;
  }
  return true;
}

SchoolDaysSaveProvenance::Verdict SchoolDaysSaveProvenance::Check(
    const std::filesystem::path& savePath, int slot,
    const std::vector<uint8_t>& savedBytes) {
  if (slot < -1 || slot >= 100) return Verdict::Untrusted;
  const auto sidecar = SidecarPath(savePath);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(sidecar, ec) || ec) return Verdict::Untrusted;
  const auto length = std::filesystem::file_size(sidecar, ec);
  if (ec || length == 0 || length > MaxSidecarSize) return Verdict::Untrusted;
  std::ifstream input(sidecar, std::ios::binary);
  if (!input) return Verdict::Untrusted;
  const std::string contents(std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{});
  if (contents.size() != length) return Verdict::Untrusted;
  std::istringstream lines(contents);
  std::array<std::string, 9> fields;
  for (auto& line : fields)
    if (!std::getline(lines, line)) return Verdict::Untrusted;
  // No extra fields or trailing garbage. First line is the schema version.
  if (lines.peek() != std::char_traits<char>::eof() ||
      fields[0] != Magic) return Verdict::Untrusted;
  const auto field = [&](size_t index, const char* prefix) -> std::string {
    const std::string key(prefix);
    return fields[index].compare(0, key.size(), key) == 0
               ? fields[index].substr(key.size()) : std::string{};
  };
  const auto binding = field(1, "binding=");
  const auto byteCount = field(2, "length=");
  const auto hash = field(3, "sha256=");
  const auto scene = field(4, "scene=");
  const auto lxh = field(5, "lxh=");
  const auto tick = field(6, "tick=");
  const auto movieStart = field(7, "movie_start=");
  const auto next = field(8, "next=");
  uint64_t sizeNumber=0, tickNumber=0, movieNumber=0, nextNumber=0;
  if (binding != PathBinding(savePath, slot) ||
      !Decimal(byteCount, &sizeNumber) || sizeNumber != savedBytes.size() ||
      hash != Sha256(savedBytes) || scene.empty() ||
      scene != SaveSceneKey(savedBytes) || (lxh != "0" && lxh != "1") ||
      !Decimal(tick, &tickNumber) || tickNumber > UINT32_MAX ||
      !Decimal(movieStart, &movieNumber) || movieNumber > UINT32_MAX ||
      !Decimal(next, &nextNumber) || nextNumber > UINT32_MAX)
    return Verdict::Untrusted;
  if (lxh == "0") return tickNumber == 0 && movieNumber == 0 && nextNumber == 0
                         ? Verdict::Normal : Verdict::Untrusted;
  const auto key = Normalized(scene);
  const bool knownEnding = std::any_of(SchoolDaysLxhContent::Endings.begin(),
      SchoolDaysLxhContent::Endings.end(), [&](auto ending) {
        return key == "05/" + Normalized(std::string(ending));
      });
  return knownEnding && movieNumber <= tickNumber && tickNumber < nextNumber
             ? Verdict::LxhEpilogue : Verdict::Untrusted;
}

} // namespace Kotonoha
