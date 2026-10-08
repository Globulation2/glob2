void apply(Map &map, const Batch &batch, Metrics &metrics)
{
    const auto start = now();
    metrics.publishedProposals += batch.proposals.size();
    if (map.game->gameHeader.isResourceGrowthDisabled())
    {
        metrics.rejected += batch.proposals.size();
        metrics.publicationNs += now() - start;
        return;
    }
    map.rebuildGrowthCoverage();
    const auto v = map.cellView();
    for (const auto &op : batch.proposals)
    {
        const auto oldType = v.resources[op.tile].resource.type;
        if (oldType != op.type && (oldType != NO_RES_TYPE || op.delta < 0))
        {
            ++metrics.rejected;
            continue;
        }
        const auto &properties = map.resourcePropertiesByIndex(op.type);
        if (oldType == NO_RES_TYPE)
        {
            // Any positive operation creating a deposit, including a delayed
            // replenishment after removal, starts every configured material at one.
            std::array<Uint16, MaterialCount> stocks{};
            for (unsigned mask = properties.materialMask; mask; mask &= mask - 1)
                stocks[std::countr_zero(mask)] = 1;
            map.replaceResource(op.tile, Resource{op.type, 0, 1, 0}, &stocks);
            bool first = true;
            for (unsigned mask = properties.materialMask; mask; mask &= mask - 1)
            {
                Proposal unit{op.tile, op.type, Uint8(std::countr_zero(mask)), 1};
                recordDelta(map, unit, first);
                first = false;
                ++metrics.stockAdded;
            }
            ++metrics.tilesAdded;
            ++metrics.accepted;
            continue;
        }
        // 255 is an experimental seed marker, still in the existing eight bytes.
        // A seed that encounters a matching deposit adds one per material, with
        // independent caps. Other operations replenish only their named material.
        unsigned mask = op.material == 255 ? properties.materialMask : (1u << op.material);
        bool changed = false;
        for (; mask; mask &= mask - 1)
        {
            const auto material = std::countr_zero(mask);
            const auto capacity = v.resourceRegistry->yields(static_cast<ResourceId>(op.type))[material].capacity;
            const auto amount = map.materialAmountAtSlot(op.tile, material);
            if ((op.delta > 0 && amount >= capacity) || (op.delta < 0 && amount == 0)) continue;
            map.setMaterialAmountSlot(op.tile, material, amount + op.delta);
            Proposal unit{op.tile, op.type, Uint8(material), op.delta};
            recordDelta(map, unit, false);
            metrics.stockAdded += op.delta > 0;
            changed = true;
        }
        if (changed) ++metrics.accepted;
        else { ++metrics.rejected; metrics.clamped += op.delta > 0; }
    }
    metrics.publicationNs += now() - start;
}
