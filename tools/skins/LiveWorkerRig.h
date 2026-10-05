// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SkinMesh.h>
#include <memory>
#include <string>

// Offline prototype only; never installed or requested by gameplay.
class LiveWorkerRig {
  public:
    struct Statistics {
        unsigned long hits = 0, misses = 0;
        double deformationSeconds = 0;
    };
    LiveWorkerRig();
    ~LiveWorkerRig();
    bool load(const std::string &path, std::string &error);
    bool loadJson(const std::string &bytes, std::string &error);
    // Phase is a cycle fraction, heading is radians; both wrap periodically.
    std::shared_ptr<const GAGCore::SkinMesh> evaluate(double phase, double heading);
    Statistics statistics() const;
    std::size_t geometryBytes() const;
    void clearCache();

  private:
    struct State;
    std::unique_ptr<State> state;
};
