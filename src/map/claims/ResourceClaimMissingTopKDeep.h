// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceClaimMissingDeep.h"
#include "ResourceClaimTopKDeep.h"
namespace ResourceClaims::Experiment {
inline constexpr unsigned MissingTopKPendingLimit=4;
inline constexpr const char* MissingTopKDesign="independent-bottom-k-missing-destination-v2";
inline Uint64 missingTopKFinalLimit(unsigned k) {if(!k || k>64)throw std::invalid_argument("missing top-k capacity");return Uint64(MissingStrata)*k*DeepRecordLimit;}
inline std::string missingTopKFilename(unsigned stratum,unsigned slot,unsigned k) {if(stratum>=MissingStrata)throw std::invalid_argument("missing stratum");return topKFilename(stratum,slot,k);}
inline bool missingTopKOutputName(std::string_view name,unsigned k) {return topKOutputName(name,k) && (name=="manifest.json" || unsigned(name[6]-'0')*10+unsigned(name[7]-'0')<MissingStrata);}
inline void replaceMissingTopKFile(const std::filesystem::path& path,std::span<const Uint8> bytes,unsigned k) {
 const auto finalLimit=missingTopKFinalLimit(k);constexpr Uint64 manifestLimit=1024*1024;
 const auto name=path.filename().string();const bool manifest=name=="manifest.json";
 if(!missingTopKOutputName(name,k))throw std::invalid_argument("missing top-k output target");
 if(bytes.size()>(manifest?manifestLimit:DeepRecordLimit))throw std::runtime_error("missing top-k file size");
 Uint64 existing=0;
 for(const auto& e:std::filesystem::directory_iterator(path.parent_path())) {
  const auto n=e.path().filename().string();
  if(!missingTopKOutputName(n,k) || e.is_symlink() || !e.is_regular_file() || e.file_size()>(n=="manifest.json"?manifestLimit:DeepRecordLimit))throw std::runtime_error("unexpected missing top-k file");
  existing+=e.file_size();
 }
 const auto previous=std::filesystem::exists(path)?std::filesystem::file_size(path):0;
 if(existing-previous+bytes.size()>finalLimit+manifestLimit || existing+bytes.size()>finalLimit+MissingTransientLimit || std::filesystem::space(path.parent_path()).available<DeepFreeReserve+bytes.size())throw std::runtime_error("missing top-k storage bound");
 const auto partial=path.string()+".partial";if(std::filesystem::exists(partial))throw std::runtime_error("stale missing top-k partial");
 std::ofstream f(partial,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();if(!f)throw std::runtime_error("missing top-k write failed");std::filesystem::rename(partial,path);
}
}
