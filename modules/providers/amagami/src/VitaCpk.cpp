// Adapted from hokagocafe/open-amagami (MIT, copyright 2026 hokagocafe).
// Vita CPK/CRI @UTF archive reader: supports TOC and ITOC directories.
// See third_party/open-amagami-LICENSE.txt and original project documentation.
#include <algorithm>
#include <cstring>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <Amagami/VitaCpk.hpp>
#include <limits>

namespace Amagami::Vita {

namespace {

using Kotonoha::Filesystem::FileHandle;
using Kotonoha::Filesystem::SDLFileSource;
using Kotonoha::Filesystem::SeekOrigin;

std::string unmask(const std::string &data) {
  if (data.size() >= 4 && std::memcmp(data.data(), "@UTF", 4) == 0)
    return data;
  std::string out = data;
  uint32_t m = 0x655F, t = 0x4115;
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = char(uint8_t(out[i]) ^ (m & 0xFF));
    m = (m * t) & 0xFFFFFFFFu;
  }
  return out;
}

void require_bytes(const std::string &data, size_t offset, size_t length) {
  if (offset > data.size() || length > data.size() - offset)
    throw std::runtime_error("cpk: truncated archive/table");
}
uint32_t be32(const std::string &d, size_t at) {
  require_bytes(d, at, 4);
  return (uint32_t(uint8_t(d[at])) << 24) |
         (uint32_t(uint8_t(d[at + 1])) << 16) |
         (uint32_t(uint8_t(d[at + 2])) << 8) | uint32_t(uint8_t(d[at + 3]));
}
uint16_t be16(const std::string &d, size_t at) {
  require_bytes(d, at, 2);
  return uint16_t((uint8_t(d[at]) << 8) | uint8_t(d[at + 1]));
}
uint64_t be64(const std::string &d, size_t at) {
  return (uint64_t(be32(d, at)) << 32) | uint64_t(be32(d, at + 4));
}

std::string cstr(const std::string &d, size_t at) {
  require_bytes(d, at, 1);
  size_t end = d.find('\0', at);
  if (end == std::string::npos)
    throw std::runtime_error("cpk: unterminated UTF string");
  return d.substr(at, end - at);
}

// A column's flag byte is a bitfield over its low-nibble type: a name (0x10), a
// default value inline in the schema (0x20), a value per row (0x40). A column
// with a default stores nothing per row even with 0x40 also set (tools/cpk.py,
// docs/technical/vita-cpk-format.md).
enum ColumnFlag : uint8_t {
  FLAG_NAME = 0x10,
  FLAG_DEFAULT = 0x20,
  FLAG_ROW = 0x40
};

// Reads one scalar/string value; floats and "data" (0x08, 0x09, 0x0B) are
// skipped over, unused here.
UtfValue read_value(const std::string &table, size_t &pos, uint8_t typ,
                    size_t strings_offset, size_t data_offset) {
  UtfValue v;
  v.present = true;
  switch (typ) {
  case 0x0A: { // string: u32 offset into the strings area
    uint32_t off = be32(table, pos);
    pos += 4;
    v.is_string = true;
    v.s = cstr(table, strings_offset + off);
    return v;
  }
  case 0x00:
  case 0x01:
    require_bytes(table, pos, 1);
    v.i = uint8_t(table[pos]);
    pos += 1;
    return v;
  case 0x02:
  case 0x03:
    v.i = be16(table, pos);
    pos += 2;
    return v;
  case 0x04:
  case 0x05:
    v.i = int64_t(be32(table, pos));
    pos += 4;
    return v;
  case 0x06:
  case 0x07:
    v.i = int64_t(be64(table, pos));
    pos += 8;
    return v;
  case 0x08:
    require_bytes(table, pos, 4);
    pos += 4;
    v.present = false;
    return v;
  case 0x09:
    require_bytes(table, pos, 8);
    pos += 8;
    v.present = false;
    return v;
  case 0x0B: { // data: u32 offset into the data area, u32 size
    uint32_t off = be32(table, pos), len = be32(table, pos + 4);
    pos += 8;
    v.is_blob = true;
    if (data_offset > table.size() || off > table.size() - data_offset)
      throw std::runtime_error("cpk: invalid blob offset");
    require_bytes(table, data_offset + off, len);
    v.s = table.substr(data_offset + off, len);
    return v;
  }
  default:
    throw std::runtime_error("cpk: unsupported UTF column type " +
                             std::to_string(typ));
  }
}

struct Column {
  std::string name;
  uint8_t type = 0;
  bool per_row = false;
  UtfValue const_value;
};

UtfTable parse_utf(const std::string &raw) {
  std::string data = unmask(raw);
  if (data.size() < 8 || std::memcmp(data.data(), "@UTF", 4) != 0)
    throw std::runtime_error("cpk: not a UTF table");
  uint32_t table_size = be32(data, 4);
  require_bytes(data, 8, table_size);
  std::string table = data.substr(8, table_size);
  require_bytes(table, 0, 24);

  uint32_t rows_offset = be32(table, 0), strings_offset = be32(table, 4),
           data_offset = be32(table, 8);
  uint16_t num_columns = be16(table, 16), row_length = be16(table, 18);
  uint32_t num_rows = be32(table, 20);
  // A malformed @UTF header must not allocate unbounded rows or point
  // into unrelated sections of the packet.
  if (num_columns > 1024 || num_rows > 1000000 ||
      rows_offset > strings_offset || strings_offset > data_offset ||
      data_offset > table.size() ||
      uint64_t(num_rows) * row_length > strings_offset - rows_offset)
    throw std::runtime_error("cpk: invalid UTF table dimensions");

  size_t pos = 24;
  std::vector<Column> columns;
  for (int c = 0; c < num_columns; ++c) {
    require_bytes(table, pos, 1);
    uint8_t flag = uint8_t(table[pos]);
    pos += 1;
    Column col;
    col.type = flag & 0x0F;
    if (flag & FLAG_NAME) {
      uint32_t name_off = be32(table, pos);
      pos += 4;
      col.name = cstr(table, strings_offset + name_off);
    }
    if (flag & FLAG_DEFAULT)
      col.const_value =
          read_value(table, pos, col.type, strings_offset, data_offset);
    col.per_row = (flag & FLAG_ROW) && !(flag & FLAG_DEFAULT);
    columns.push_back(std::move(col));
  }
  if (pos != rows_offset)
    throw std::runtime_error(
        "cpk: UTF schema does not end where the rows start");

  UtfTable out;
  for (uint32_t r = 0; r < num_rows; ++r) {
    const size_t row_offset = size_t(rows_offset) + size_t(r) * row_length;
    require_bytes(table, row_offset, row_length);
    pos = row_offset;
    std::unordered_map<std::string, UtfValue> row;
    for (const Column &col : columns) {
      UtfValue v;
      v = col.per_row
              ? read_value(table, pos, col.type, strings_offset, data_offset)
              : col.const_value;
      if (!col.name.empty())
        row[col.name] = v;
    }
    if (pos != row_offset + row_length)
      throw std::runtime_error("cpk: UTF row has an invalid length");
    out.rows.push_back(std::move(row));
  }
  return out;
}

int64_t get_int(const std::unordered_map<std::string, UtfValue> &row,
                const char *key, int64_t fallback = 0) {
  auto it = row.find(key);
  if (it == row.end() || !it->second.present)
    return fallback;
  return it->second.i;
}
std::string get_str(const std::unordered_map<std::string, UtfValue> &row,
                    const char *key) {
  auto it = row.find(key);
  if (it == row.end() || !it->second.present)
    return "";
  return it->second.s;
}

// A length-prefixed @UTF packet: 4-byte magic (ignored), 4 bytes padding, u64
// LE size, then the masked table (tools/cpk.py's read_packet).
uint64_t stream_size(FileHandle &f) {
  const auto n = f.size();
  if (n < 0)
    throw std::runtime_error("cpk: unable to determine file size");
  return uint64_t(n);
}
void check_entry(uint64_t offset, uint64_t length, uint64_t file_size) {
  if (offset > file_size || length > file_size - offset ||
      length > 512ull * 1024 * 1024)
    throw std::runtime_error("cpk: entry outside archive payload");
}
bool read_exact(FileHandle &file, void *destination, size_t count) {
  auto *bytes = static_cast<char *>(destination);
  size_t done = 0;
  while (done < count) {
    const auto n = file.read(bytes + done, count - done);
    if (n <= 0 || uint64_t(n) > count - done)
      return false;
    done += size_t(n);
  }
  return true;
}

bool seek_absolute(FileHandle &file, uint64_t offset) {
  return offset <= uint64_t(std::numeric_limits<int64_t>::max()) &&
         file.seek(int64_t(offset), SeekOrigin::Begin) == int64_t(offset);
}

UtfTable read_packet(FileHandle &f, uint64_t offset) {
  if (!seek_absolute(f, offset))
    throw std::runtime_error("cpk: invalid packet offset");
  char header[16];
  if (!read_exact(f, header, sizeof(header)))
    throw std::runtime_error("cpk: truncated packet header");
  uint64_t size = 0;
  for (unsigned i = 0; i < 8; ++i)
    size |= uint64_t(uint8_t(header[8 + i])) << (i * 8);
  if (size > 64ull * 1024 * 1024)
    throw std::runtime_error("cpk: UTF packet exceeds size limit");
  std::string body(size_t(size), '\0');
  if (!read_exact(f, body.data(), body.size()))
    throw std::runtime_error("cpk: truncated UTF packet");
  return parse_utf(body);
}

} // namespace

UtfTable cpk_parse_utf(const std::string &bytes) { return parse_utf(bytes); }

CpkIndex cpk_read_index(const std::string &path) {
  SDLFileSource f{std::filesystem::path(path)};
  char magic[4];
  if (!read_exact(f, magic, sizeof(magic)) || std::memcmp(magic, "CPK ", 4) != 0)
    throw std::runtime_error(path + ": not a CPK archive");
  const uint64_t file_size = stream_size(f);

  UtfTable header_table = read_packet(f, 0);
  const auto &header = header_table.rows.at(0);
  int64_t toc_offset = get_int(header, "TocOffset", -1);
  if (toc_offset < 0)
    throw std::runtime_error(
        path + ": no TocOffset (ITOC-mode CPK, not supported here)");
  int64_t content_offset = get_int(header, "ContentOffset", toc_offset);
  if (content_offset < 0)
    throw std::runtime_error("cpk: invalid content offset");
  const uint64_t base_offset = uint64_t(std::min(toc_offset, content_offset));

  UtfTable toc = read_packet(f, uint64_t(toc_offset));
  CpkIndex index;
  for (const auto &row : toc.rows) {
    std::string dir = get_str(row, "DirName");
    std::string name = get_str(row, "FileName");
    std::string key = dir.empty() ? name : dir + "/" + name;
    const auto relative = get_int(row, "FileOffset", -1);
    const auto length = get_int(row, "FileSize", -1);
    const auto extract = get_int(row, "ExtractSize", -1);
    if (name.empty() || relative < 0 || length < 0 ||
        uint64_t(relative) > file_size ||
        base_offset > file_size - uint64_t(relative))
      throw std::runtime_error("cpk: invalid TOC resource");
    CpkEntry e;
    e.offset = base_offset + uint64_t(relative);
    e.size = uint64_t(length);
    e.extract_size = extract >= 0 ? uint64_t(extract) : e.size;
    check_entry(e.offset, e.size, file_size);
    if (e.extract_size > 512ull * 1024 * 1024 || !index.emplace(key, e).second)
      throw std::runtime_error("cpk: invalid or duplicate resource " + key);
  }
  return index;
}

namespace {
const int VLE_LENS[4] = {2, 3, 5, 8};
}

std::vector<CpkEntry> cpk_read_entries(const std::string &path,
                                       uint64_t start) {
  SDLFileSource f{std::filesystem::path(path)};
  char magic[4];
  const uint64_t file_size = stream_size(f);
  if (start > file_size || file_size - start < 4 || !seek_absolute(f, start))
    throw std::runtime_error("cpk: invalid nested archive offset");
  if (!read_exact(f, magic, sizeof(magic)) || std::memcmp(magic, "CPK ", 4) != 0)
    throw std::runtime_error(path + ": not a CPK archive");
  UtfTable header_table = read_packet(f, start);
  const auto &header = header_table.rows.at(0);
  std::vector<CpkEntry> out;
  int64_t toc_offset = get_int(header, "TocOffset", -1);
  if (toc_offset >= 0) { // named entries, in the table's order
    int64_t content_offset = get_int(header, "ContentOffset", toc_offset);
    if (content_offset < 0 || uint64_t(toc_offset) > file_size - start)
      throw std::runtime_error("cpk: invalid TOC offsets");
    const uint64_t base_offset = uint64_t(std::min(toc_offset, content_offset));
    UtfTable toc = read_packet(f, start + uint64_t(toc_offset));
    for (const auto &row : toc.rows) {
      const auto relative = get_int(row, "FileOffset", -1);
      const auto length = get_int(row, "FileSize", -1);
      const auto extract = get_int(row, "ExtractSize", -1);
      if (relative < 0 || length < 0 || base_offset > file_size - start ||
          uint64_t(relative) > file_size - start - base_offset)
        throw std::runtime_error("cpk: invalid TOC member");
      CpkEntry e;
      e.offset = start + base_offset + uint64_t(relative);
      e.size = uint64_t(length);
      e.extract_size = extract >= 0 ? uint64_t(extract) : e.size;
      check_entry(e.offset, e.size, file_size);
      if (e.extract_size > 512ull * 1024 * 1024)
        throw std::runtime_error("cpk: expanded TOC member exceeds limit");
      out.push_back(e);
    }
    return out;
  }
  // The other layout (ItocOffset: MUSIC, SOUND, VOICE and the per-speaker
  // archives): no names, ids in order, files stored back to back from
  // ContentOffset, each padded up to Align (tools/cpk.py, read_itoc_entries).
  int64_t itoc_offset = get_int(header, "ItocOffset", -1);
  if (itoc_offset < 0)
    throw std::runtime_error(path + ": neither TocOffset nor ItocOffset");
  if (uint64_t(itoc_offset) > file_size - start)
    throw std::runtime_error("cpk: invalid ITOC offset");
  UtfTable itoc = read_packet(f, start + uint64_t(itoc_offset));
  std::map<int64_t, std::pair<int64_t, int64_t>>
      by_id; // id -> size, extract size
  for (const char *column : {"DataL", "DataH"}) {
    auto it = itoc.rows.at(0).find(column);
    if (it == itoc.rows.at(0).end() || !it->second.is_blob ||
        it->second.s.empty())
      continue;
    UtfTable rows = parse_utf(it->second.s);
    for (const auto &row : rows.rows) {
      int64_t size = get_int(row, "FileSize");
      int64_t extract = get_int(row, "ExtractSize", -1);
      const auto id = get_int(row, "ID", -1);
      if (id < 0 || size < 0 ||
          !by_id
               .emplace(id, std::make_pair(size, extract >= 0 ? extract : size))
               .second)
        throw std::runtime_error("cpk: invalid ITOC member ID/size");
    }
  }
  int64_t align = get_int(header, "Align", 2048);
  if (align <= 0 || align > 65536)
    throw std::runtime_error("cpk: invalid ITOC alignment");
  const auto content_offset = get_int(header, "ContentOffset", -1);
  if (content_offset < 0 || uint64_t(content_offset) > file_size - start)
    throw std::runtime_error("cpk: invalid ITOC content offset");
  uint64_t offset = start + uint64_t(content_offset);
  for (const auto &[id, sizes] : by_id) {
    CpkEntry e;
    e.offset = offset;
    e.size = uint64_t(sizes.first);
    e.extract_size = uint64_t(sizes.second);
    check_entry(e.offset, e.size, file_size);
    if (e.extract_size > 512ull * 1024 * 1024)
      throw std::runtime_error("cpk: expanded ITOC member exceeds limit");
    out.push_back(e);
    const uint64_t aligned =
        (e.size + uint64_t(align) - 1) / uint64_t(align) * uint64_t(align);
    if (aligned > file_size - offset)
      offset = file_size; // Last entry need not retain alignment padding.
    else
      offset += aligned;
  }
  return out;
}

std::string cpk_decompress_crilayla(const std::string &data) {
  if (data.size() < 8 || std::memcmp(data.data(), "CRILAYLA", 8) != 0)
    return data;
  if (data.size() < 16)
    throw std::runtime_error("cpk: truncated CRILAYLA header");
  uint32_t uncompressed_size = 0, header_offset = 0;
  for (unsigned i = 0; i < 4; ++i) {
    uncompressed_size |= uint32_t(uint8_t(data[8 + i])) << (i * 8);
    header_offset |= uint32_t(uint8_t(data[12 + i])) << (i * 8);
  }
  if (uncompressed_size > 512u * 1024 * 1024 ||
      data.size() < 16 + size_t(0x100) ||
      header_offset > data.size() - 16 - size_t(0x100))
    throw std::runtime_error("cpk: invalid CRILAYLA sizes");

  std::string result(size_t(0x100) + uncompressed_size, '\0');
  std::memcpy(result.data(), data.data() + 16 + header_offset, 0x100);
  const uint8_t *compressed =
      reinterpret_cast<const uint8_t *>(data.data()) + 16;

  int64_t input_offset = int64_t(header_offset) - 1;
  // The bits come from the end of the input backwards, each byte most
  // significant bit first: an accumulator that takes a byte at a time and hands
  // out the top `n` of the bits it holds.
  uint64_t pool = 0;
  int bits_left = 0;
  uint32_t output_end = 0x100 + uncompressed_size - 1;
  uint32_t bytes_output = 0;

  auto get_bits = [&](int n) -> uint32_t {
    while (bits_left < n) {
      if (input_offset < 0)
        throw std::runtime_error("cpk: truncated CRILAYLA bitstream");
      pool = (pool << 8) | compressed[input_offset];
      input_offset -= 1;
      bits_left += 8;
    }
    bits_left -= n;
    return uint32_t((pool >> bits_left) & ((uint64_t(1) << n) - 1));
  };

  while (bytes_output < uncompressed_size) {
    if (get_bits(1)) {
      uint32_t back_offset = output_end - bytes_output + get_bits(13) + 3;
      uint32_t length = 3;
      int level = 0;
      while (true) {
        int bits = VLE_LENS[std::min(level, 3)];
        uint32_t chunk = get_bits(bits);
        length += chunk;
        if (chunk != (1u << bits) - 1)
          break;
        level += 1;
      }
      if (length > uncompressed_size - bytes_output)
        throw std::runtime_error("cpk: CRILAYLA match exceeds output");
      for (uint32_t i = 0; i < length; ++i) {
        if (back_offset >= result.size())
          throw std::runtime_error("cpk: invalid CRILAYLA match offset");
        result[output_end - bytes_output] = result[back_offset];
        back_offset -= 1;
        bytes_output += 1;
      }
    } else {
      result[output_end - bytes_output] = char(get_bits(8));
      bytes_output += 1;
    }
  }
  return result;
}

bool cpk_read(const std::string &cpk_path, const CpkIndex &index,
              const std::string &key, std::string &out) {
  auto it = index.find(key);
  if (it == index.end())
    return false;
  // One open file per archive, kept: opening costs tens of milliseconds over a
  // network or WSL path, once per picture otherwise. Reads come from the
  // preload threads too, hence the lock (decompression runs outside it).
  static std::mutex lock;
  static std::map<std::string, std::unique_ptr<SDLFileSource>> files;
  if (it->second.size > 512ull * 1024 * 1024)
    throw std::runtime_error("cpk: entry exceeds size limit");
  std::string raw(size_t(it->second.size), '\0');
  {
    std::lock_guard<std::mutex> guard(lock);
    if (files.find(cpk_path) == files.end() && files.size() >= 16)
      files.clear();
    auto &f = files[cpk_path];
    if (!f)
      f = std::make_unique<SDLFileSource>(std::filesystem::path(cpk_path));
    check_entry(it->second.offset, it->second.size, stream_size(*f));
    if (!seek_absolute(*f, it->second.offset) ||
        !read_exact(*f, raw.data(), raw.size()))
      throw std::runtime_error("cpk: truncated file entry");
  }
  out = raw.size() >= 8 && std::memcmp(raw.data(), "CRILAYLA", 8) == 0
            ? cpk_decompress_crilayla(raw)
            : raw;
  if (it->second.extract_size && out.size() != it->second.extract_size)
    throw std::runtime_error("cpk: decompressed member size mismatch");
  return true;
}

} // namespace Amagami::Vita
