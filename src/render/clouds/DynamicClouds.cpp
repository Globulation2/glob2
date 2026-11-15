// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "CloudField.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"
#include "render/Sunlight.h"
#include <ParallelPresentationWork.h>
#include <PresentationWork.h>
#include <ThreadSupport.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
// Cloud samples are pure functions of an immutable field, so they always use a
// few workers of their own, whatever the global render execution mode. The
// renderers without a cloud shader sample every lattice point on the CPU
// each frame.
GAGCore::PresentationWork &cloudWorkers()
{
    static GAGCore::ParallelPresentationWork pool(
        std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u));
    return pool;
}
}

//! Shadow and cloud opacities on a world-anchored lattice.
struct DynamicClouds::Frame
{
    int startX = 0, startY = 0, cell = 1, w = 0, h = 0, worldWidth = 0, worldHeight = 0;
    bool cloudLayer = false;
    std::vector<unsigned char> alpha, cloud;
};

namespace
{
//! Everything a lattice depends on, copied so a worker never reads the view.
struct CloudRequest
{
    int startX, startY, cell, w, h, time, worldWidth, worldHeight;
    bool cloudLayer;
    float cloudSize, cloudStability, maxCloudSpeed, windStability, cloudHeight;
    int maxAlpha;
};

void sample(const CloudRequest &request, DynamicClouds::Frame &frame, GAGCore::PresentationWork &workers);
}

struct DynamicClouds::Worker
{
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<CloudRequest> request;
    std::shared_ptr<const Frame> done;
    bool stop = false;
    std::thread thread;
    // Its own helpers: the graphics thread's fallback uses cloudWorkers().
    GAGCore::ParallelPresentationWork helpers{std::clamp(std::thread::hardware_concurrency() / 8, 1u, 3u)};
    Worker() : thread([this] { run(); }) {}
    ~Worker()
    {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        wake.notify_all();
        thread.join();
    }
    void run()
    {
        for (;;)
        {
            CloudRequest next;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || request; });
                if (stop)
                    return;
                next = *request;
                request.reset();
            }
            auto frame = std::make_shared<Frame>();
            sample(next, *frame, helpers);
            std::lock_guard lock(mutex);
            done = std::move(frame);
        }
    }
};

namespace
{
void sample(const CloudRequest &request, DynamicClouds::Frame &frame, GAGCore::PresentationWork &workers)
{
    frame.startX = request.startX;
    frame.startY = request.startY;
    frame.cell = request.cell;
    frame.w = request.w;
    frame.h = request.h;
    frame.worldWidth = request.worldWidth;
    frame.worldHeight = request.worldHeight;
    frame.cloudLayer = request.cloudLayer;
    frame.alpha.assign(std::size_t(request.w) * request.h, 0);
    if (request.cloudLayer)
        frame.cloud.assign(frame.alpha.size(), 0);
    CloudField field(request.worldWidth * 32, request.worldHeight * 32, request.time, request.cloudSize,
                     request.cloudStability, request.maxCloudSpeed, request.windStability, request.maxAlpha);
    const float magnification = std::max(.01f, request.cloudHeight);
    // The shadow is the same cloud field, thrown along the sun to the lower
    // right by a distance that grows with the cloud's height.
    const int shadowX = int(std::lround(Sunlight::shadowX * Sunlight::cloudShadowThrow * magnification));
    const int shadowY = int(std::lround(Sunlight::shadowY * Sunlight::cloudShadowThrow * magnification));
    constexpr int RowsPerJob = 4;
    workers.executePresentationJobs(std::size_t((request.h + RowsPerJob - 1) / RowsPerJob), [&](std::size_t job)
    {
        for (int y = int(job) * RowsPerJob; y < std::min(request.h, int(job + 1) * RowsPerJob); ++y)
            for (int x = 0; x < request.w; ++x)
            {
                const int wx = request.startX + x * request.cell, wy = request.startY + y * request.cell;
                frame.alpha[std::size_t(y) * request.w + x] = field.opacity(wx - shadowX, wy - shadowY, magnification);
                if (request.cloudLayer)
                    frame.cloud[std::size_t(y) * request.w + x] = field.opacity(wx, wy, magnification);
            }
    });
}
}

DynamicClouds::~DynamicClouds() = default;

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, const int time, const int worldWidth,
                            const int worldHeight, bool includeCloudLayer, int maxGridSize)
{
    if (!globalContainer->gfx->rendererCapabilities().cloudLayers)
        return;
    // Keep the lattice anchored in world space. An overview can request fewer
    // samples without changing the field or the normal viewport quality.
    int cell = granularity;
    if (maxGridSize > 0)
        while ((std::max(worldWidth, worldHeight) * 32 + cell - 1) / cell > maxGridSize)
            cell *= 2;
    const int pixelX = viewPortX * 32, pixelY = viewPortY * 32;
    const auto floorTo = [cell](int v) { return v >= 0 ? v - v % cell : v - ((v % cell) + cell) % cell; };
    const auto request = [&](int margin)
    {
        CloudRequest r{};
        r.startX = floorTo(pixelX) - margin * cell;
        r.startY = floorTo(pixelY) - margin * cell;
        r.cell = cell;
        // The last lattice point reaches the view's far edge.
        r.w = (pixelX + viewPortWidth - r.startX + cell - 1) / cell + 1 + margin;
        r.h = (pixelY + viewPortHeight - r.startY + cell - 1) / cell + 1 + margin;
        r.time = time;
        r.worldWidth = worldWidth;
        r.worldHeight = worldHeight;
        r.cloudLayer = includeCloudLayer;
        r.cloudSize = cloudSize;
        r.cloudStability = cloudStability;
        r.maxCloudSpeed = maxCloudSpeed;
        r.windStability = windStability;
        r.cloudHeight = cloudHeight;
        r.maxAlpha = maxAlpha;
        return r;
    };
    const auto covers = [&](const Frame *frame)
    {
        return frame && frame->cell == cell && frame->worldWidth == worldWidth &&
               frame->worldHeight == worldHeight && (frame->cloudLayer || !includeCloudLayer) &&
               frame->startX <= pixelX && frame->startY <= pixelY &&
               frame->startX + (frame->w - 1) * cell >= pixelX + viewPortWidth &&
               frame->startY + (frame->h - 1) * cell >= pixelY + viewPortHeight;
    };
    // A few cells of margin let the camera move between samples.
    constexpr int Margin = 3;
    std::shared_ptr<const Frame> latest = current;
    if constexpr (GAGCore::ThreadSupport::available)
    {
        if (!worker)
            worker = std::make_shared<Worker>();
        {
            std::lock_guard lock(worker->mutex);
            if (worker->done)
                latest = std::move(worker->done);
            worker->request = request(Margin);
        }
        worker->wake.notify_one();
    }
    if (!covers(latest.get()))
    {
        // Nothing sampled covers this view yet: the first frame or a jump.
        auto frame = std::make_shared<Frame>();
        sample(request(Margin), *frame, cloudWorkers());
        latest = std::move(frame);
    }
    current = std::move(latest);
    renderCellSize = cell;
    renderOffsetX = current->startX - pixelX;
    renderOffsetY = current->startY - pixelY;
    wGrid = current->w;
    hGrid = current->h;
}

void DynamicClouds::computeWorld(const int worldWidth, const int worldHeight, const int time,
                                 std::vector<unsigned char> &out, int &gridW, int &gridH, int maxGridSize) const
{
    // Sample the same world field at the detail needed by the overview.
    int cell = granularity;
    while (std::max(worldWidth, worldHeight) * 32 / cell > std::max(1, maxGridSize))
        cell *= 2;
    gridW = std::max(1, worldWidth * 32 / cell);
    gridH = std::max(1, worldHeight * 32 / cell);
    if (out.size() != static_cast<size_t>(gridW * gridH))
        out.assign(gridW * gridH, 0);
    CloudField field(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
                     windStability, maxAlpha);
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
            out[y * gridW + x] = field.opacity(x * cell, y * cell, std::max(.01f, cloudHeight));
}

void DynamicClouds::render(DrawableSurface *dest, const int, const int, DynamicClouds::Layer layer)
{
    if (!globalContainer->gfx->rendererCapabilities().cloudLayers)
        return;
    // Magnification is sampled in world space, never around the viewport center.
    if (!current)
        return;
    bool cloud = layer == DynamicClouds::CLOUD;
    if (cloud && !current->cloudLayer)
        return;
    dest->drawAlphaMap(cloud ? current->cloud : current->alpha, wGrid, hGrid, renderOffsetX, renderOffsetY,
                       renderCellSize, renderCellSize, cloud ? Color(255, 255, 255) : Color(0, 0, 0));
}
