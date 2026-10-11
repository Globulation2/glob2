// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitCatalog.h"
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore
class Race
{
  public:
    Sint32 hungriness = 425;
    Race();
    virtual ~Race();
    void load();
    // Compatibility entry point: warms immutable defaults, mutates no live game.
    static void loadDefault();
    void setCatalog(std::shared_ptr<const UnitCatalog> catalog);
    const std::shared_ptr<const UnitCatalog> &getCatalog() const { return catalog_; }
    const UnitRuntimeTraits &getRuntime(int type) const { return catalog_->runtime(type); }
    std::size_t unitTypeCount() const { return unitTypes.size(); }
    const UnitType *getUnitType(int type, int level) const;
    void save(GAGCore::OutputStream *stream);
    bool load(GAGCore::InputStream *stream, Sint32 versionMinor);

  private:
    std::vector<std::array<UnitType, NB_UNIT_LEVELS>> unitTypes;
    std::shared_ptr<const UnitCatalog> catalog_;
};
