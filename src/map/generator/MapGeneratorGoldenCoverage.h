// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

// Golden revisions belong to a platform: a fresh foreign row cannot certify
// stale or absent local output. Mixed local revisions also need regeneration.
enum class MapGoldenCoverage { Missing, Stale, Current };

template<class Rows>
MapGoldenCoverage mapGoldenCoverage(const Rows &rows, const std::string &platform,
                                    int generator, unsigned revision)
{
    bool found = false;
    for (const auto &row : rows)
        if (row.platform == platform && row.id == generator)
        {
            found = true;
            if (row.revision != revision)
                return MapGoldenCoverage::Stale;
        }
    return found ? MapGoldenCoverage::Current : MapGoldenCoverage::Missing;
}
