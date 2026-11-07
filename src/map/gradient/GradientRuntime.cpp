// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientRuntime.h"
#include "FileFormatVersions.h"

GradientRuntime::GradientRuntime()
    : loadingGradientVersion(FILE_FORMAT_VERSION_ROUTING_RATIO_POLICY)
{
    pipeline.onSuperseded=[this](GradientPipeline::Job& job) {
        if(job.adaptiveKey) {
            const auto found=adaptive.fields.find(*job.adaptiveKey);
            if(found!=adaptive.fields.end())found->second.inflight=false;
        }
    };
    shareBackendSession();
    if (const char* path=std::getenv("GLOB2_RESOURCE_GRADIENT_PROBE"))
        if (*path) qualityProbe=std::make_unique<ResourceGradientProbe>(path);
}
