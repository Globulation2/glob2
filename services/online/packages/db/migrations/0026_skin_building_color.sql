ALTER TABLE colony_skin_equipment ADD COLUMN building_color integer CHECK (building_color BETWEEN 0 AND 16777215);
ALTER TABLE match_colony_skins ADD COLUMN building_color integer CHECK (building_color BETWEEN 0 AND 16777215);
UPDATE match_colony_skins AS m SET building_color = v.building_color FROM colony_skin_versions AS v WHERE v.id = m.version_id;
ALTER TABLE match_colony_skins ALTER COLUMN building_color SET NOT NULL;
