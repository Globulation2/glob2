// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientDeadlineBatch.h"
#include "OpenCLGradient.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include "online/Sha256.h"

namespace gradient_kernel
{
inline std::uint64_t gradientManifestHash(std::string_view value) noexcept {
    std::uint64_t hash=14695981039346656037ull;
    for(const unsigned char byte:value){hash^=byte;hash*=1099511628211ull;}return hash;
}
inline const std::string& gradientNativeBuildIdentity() noexcept {
    // Linux-first development profile, measured native image rather than a
    // source label. Called on broker startup (tests may call it independently).
    static const auto value=[]() noexcept -> std::string {
#if defined(__linux__)
        try {
            std::ifstream image("/proc/self/exe",std::ios::binary);if(!image)return {};
            std::array<char,65536> buffer;Online::Sha256 hash;std::uint64_t total=0;
            do {
                image.read(buffer.data(),std::streamsize(buffer.size()));const auto count=image.gcount();
                total+=std::uint64_t(count);if(total>512ull*1024*1024)return {};
                hash.update(buffer.data(),std::size_t(count));
            } while(!image.eof() && image);
            return image.eof() && total ? Online::Sha256::toHex(hash.finish()) : std::string{};
        } catch(...) {return {};}
#else
        return {};
#endif
    }();return value;
}
struct GradientBatchManifest
{
    static constexpr std::size_t MaxBytes=65536,MaxProfiles=32;
    std::array<GradientBatchBound,MaxProfiles> bounds{};
    unsigned count=0;
    std::uint64_t hash=0,sourceHash=0;
    // Broker-only. Every entry is actual homogeneous batch-N offline evidence;
    // a caller cannot turn singleton latency into a batch bound.
    static GradientBatchManifest parse(std::string_view input,const OpenCLStatus& backend,bool parityBound,std::string_view nativeBuildIdentity) {
        if(input.empty() || input.size()>MaxBytes)throw std::invalid_argument("batch manifest size");
        unsigned depth=0;bool quoted=false,escaped=false;
        for(const char c:input) {
            if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='\"')quoted=false;continue;}
            if(c=='\"')quoted=true;
            else if(c=='{' || c=='['){if(++depth>8)throw std::invalid_argument("batch manifest depth");}
            else if(c=='}' || c==']'){if(!depth)throw std::invalid_argument("batch manifest structure");--depth;}
        }
        using Json=nlohmann::json;
        const auto root=Json::parse(input.begin(),input.end());
        const auto number=[](const Json& object,const char* key,std::uint64_t maximum) {
            const auto& value=object.at(key);
            if(!value.is_number_unsigned())throw std::invalid_argument("batch manifest unsigned field");
            const auto result=value.get<std::uint64_t>();
            if(result>maximum)throw std::invalid_argument("batch manifest numeric bound");return result;
        };
        if(number(root,"schema",1)!=1)throw std::invalid_argument("batch manifest schema");
        if(!Online::Sha256::isHexDigest(nativeBuildIdentity) || root.at("native_binary_sha256").get<std::string>()!=nativeBuildIdentity)
            throw std::invalid_argument("batch manifest native image");
        const auto source=root.at("source").get<std::string>();
        if(source.empty() || source.size()>256 || root.at("measurement").get<std::string>()!="homogeneous-ready-batch")
            throw std::invalid_argument("batch manifest evidence source");
        const auto& config=root.at("backend");
        const auto matches=[](const Json& object,const char* key,const std::string& actual) {
            return !actual.empty() && object.at(key).get<std::string>()==actual;
        };
        if(!matches(config,"platform",backend.platform) || !matches(config,"platform_vendor",backend.platformVendor) ||
           !matches(config,"platform_version",backend.platformVersion) || !matches(config,"device_vendor",backend.deviceVendor) ||
           !matches(config,"driver_version",backend.driverVersion) || !matches(config,"device_version",backend.deviceVersion) ||
           !matches(config,"opencl_c_version",backend.openCLCVersion))
            throw std::invalid_argument("batch manifest unavailable backend fingerprint");
        if(backend.device.empty() || config.at("device").get<std::string>()!=backend.device ||
           number(config,"check_interval",65536)!=backend.checkInterval ||
           number(config,"poll_micros",1000000)!=backend.pollMicros ||
           config.at("active_epoch").get<bool>()!=backend.activeEpoch ||
           config.at("uniform_metadata").get<bool>()!=backend.uniformMetadata ||
           config.at("device_profiling").get<bool>()!=backend.deviceProfiling ||
           config.at("parity_bound").get<bool>()!=parityBound ||
           config.at("direct_seed_upload").get<bool>()!=backend.directSeedUpload)
            throw std::invalid_argument("batch manifest backend configuration");
        const auto& profiles=root.at("profiles");
        if(!profiles.is_array() || profiles.empty() || profiles.size()>MaxProfiles)
            throw std::invalid_argument("batch manifest profile count");
        GradientBatchManifest out;out.hash=gradientManifestHash(input);out.sourceHash=gradientManifestHash(source);
        for(const auto& entry:profiles) {
            GradientBatchBound bound;auto& key=bound.workload;
            key.width=unsigned(number(entry,"width",65536));key.height=unsigned(number(entry,"height",65536));
            key.cpuBuckets=unsigned(number(entry,"cpu_buckets",256));key.threads=unsigned(number(entry,"threads",65536));
            key.batch=unsigned(number(entry,"batch",8));key.limit=unsigned(number(entry,"limit",65534));
            key.movement=number(entry,"movement",UINT64_MAX);key.movementModifiers=entry.at("movement_modifiers").get<bool>();
            key.seedDensity=std::uint8_t(number(entry,"seed_density",16));key.blockerDensity=std::uint8_t(number(entry,"blocker_density",16));
            const auto family=entry.at("family").get<std::string>();
            if(family=="clear")key.family=Family::Clear;else if(family=="guard")key.family=Family::Guard;
            else throw std::invalid_argument("batch manifest unsupported seed class");
            const auto plan=unsigned(number(entry,"plan",PLANS.size()-1));bound.plan=Plan(plan);
            bound.costRevision=number(entry,"cost_revision",UINT64_MAX);
            bound.costVariant=number(entry,"cost_variant",UINT64_MAX);
            bound.maxElapsedNs=number(entry,"max_measured_elapsed_ns",UINT64_MAX/4);
            bound.completionMarginNs=number(entry,"completion_margin_ns",UINT64_MAX/4);
            const auto measurements=number(entry,"measured_batches",UINT64_MAX);
            if(!key.width || !key.height || key.threads<2 || key.batch<2 || plan==0 ||
               !bound.maxElapsedNs || !bound.completionMarginNs || measurements<8 ||
               (key.cpuBuckets!=64 && key.cpuBuckets!=128 && key.cpuBuckets!=256))
                throw std::invalid_argument("batch manifest incomplete bound");
            for(unsigned i=0;i<out.count;++i)if(out.bounds[i].workload==key && out.bounds[i].plan==bound.plan &&
                out.bounds[i].costRevision==bound.costRevision && out.bounds[i].costVariant==bound.costVariant)
                throw std::invalid_argument("batch manifest duplicate bound");
            out.bounds[out.count++]=bound;
        }
        return out;
    }
};
}
