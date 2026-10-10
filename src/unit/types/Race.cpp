// SPDX-License-Identifier: GPL-3.0-or-later
#include "Race.h"
#include "FileFormatVersions.h"
#include <Stream.h>
#include <cassert>
#include <stdexcept>
Race::Race() { setCatalog(UnitCatalog::availableDefaults()); }
Race::~Race() = default;
void Race::loadDefault() { (void)UnitCatalog::availableDefaults(); }
void Race::load() { setCatalog(UnitCatalog::availableDefaults()); }
void Race::setCatalog(std::shared_ptr<const UnitCatalog> catalog)
{
    if (!catalog)
        throw std::invalid_argument("Missing unit catalog");
    catalog_ = std::move(catalog);
    unitTypes.resize(catalog_->size());
    for (unsigned i = 0; i < unitTypes.size(); ++i)
        unitTypes[i] = catalog_->levels(i);
    hungriness = catalog_->runtime(WORKER).hungerRate;
}
const UnitType *Race::getUnitType(int type, int level) const
{
    assert(type >= 0 && unsigned(type) < unitTypes.size() && level >= 0 && level < NB_UNIT_LEVELS);
    return &unitTypes[type][level];
}
void Race::save(GAGCore::OutputStream *stream)
{
    stream->writeUint32(unitTypes.size(), "unitTypeCount");
    stream->writeEnterSection("unitTypes");
    for (unsigned type = 0; type < unitTypes.size(); ++type)
    {
        stream->writeEnterSection(type);
        for (unsigned level = 0; level < NB_UNIT_LEVELS; ++level)
        {
            stream->writeEnterSection(level);
            unitTypes[type][level].save(stream);
            stream->writeLeaveSection();
        }
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeSint32(hungriness, "hungryness");
}
bool Race::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
    const unsigned count = versionMinor >= FILE_FORMAT_VERSION_UNIT_CATALOG
                               ? stream->readUint32("unitTypeCount")
                               : BuiltinUnitCount;
    if (count != unitTypes.size() || count > UnitCatalog::Capacity)
        throw std::runtime_error("Race unit count does not match catalog");
    auto loaded = unitTypes;
    const bool counted = versionMinor >= FILE_FORMAT_VERSION_UNIT_CATALOG;
    if (counted)
        stream->readEnterSection("unitTypes");
    for (unsigned type = 0; type < loaded.size(); ++type)
    {
        if (counted)
            stream->readEnterSection(type);
        for (unsigned level = 0; level < NB_UNIT_LEVELS; ++level)
        {
            if (counted)
                stream->readEnterSection(level);
            loaded[type][level].load(stream, versionMinor);
            if (counted)
                stream->readLeaveSection();
        }
        if (counted)
            stream->readLeaveSection();
    }
    if (counted)
        stream->readLeaveSection();
    const auto rate = stream->readSint32("hungryness");
    if (rate < 0 || rate > 1000000)
        throw std::runtime_error("Invalid race hunger rate");
    if (counted)
    {
        if (loaded != unitTypes || rate != hungriness)
            throw std::runtime_error("Race tables differ from the authoritative unit catalog");
        return true;
    }
    auto updated = catalog_->withLegacyLevels(loaded, rate);
    catalog_ = std::move(updated);
    unitTypes = std::move(loaded);
    hungriness = rate;
    return true;
}
