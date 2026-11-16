// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2ArchiveAssets.hpp>
#include <set>
#include <stdexcept>
#include <utility>
namespace N2System {
void N2ArchiveAssets::mount(std::vector<std::uint8_t> data) {
  Archive parsed(std::move(data)); // Reject corrupt input before mounting.
  archives_.push_back(std::move(parsed));
}
bool N2ArchiveAssets::containsAsset(const std::string& path) const {
  for (auto it=archives_.rbegin();it!=archives_.rend();++it)
    if (it->contains(path)) return true;
  return false;
}
std::vector<Uint8> N2ArchiveAssets::readAsset(const std::string& path) const {
  for (auto it=archives_.rbegin();it!=archives_.rend();++it)
    if (it->contains(path)) return it->read(path);
  throw std::out_of_range("N2System: missing asset "+path);
}
std::vector<std::string> N2ArchiveAssets::listAsset(
    const std::string& prefix) const {
  std::set<std::string> names;
  const auto lookup=Archive::normalize(prefix);
  for (const auto& archive:archives_)
    for (const auto& entry:archive.entries())
      if (entry.name.compare(0,lookup.size(),lookup)==0)
        names.insert(entry.name);
  return {names.begin(),names.end()};
}
} // namespace N2System
