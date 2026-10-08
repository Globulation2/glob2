// SPDX-License-Identifier: GPL-3.0-or-later
// The phone editor's card tray: every brush of the catalogue (BrushCatalog.h)
// as a labelled card with its shared swatch, grouped under header chips in the
// Terrain and Resources modes. Cards are identified by catalogue id, so
// imports, experiment changes and catalogue rebuilds keep the tray in step.
#include "ExperimentalFeatures.h"
#include "GlobalContainer.h"
#include "InGameTouchTheme.h"
#include "MapEdit.h"
#include "PhoneEditor.h"
#include "render/UnitAnimation.h"
#include "render/UnitSkin.h"
#include "resource/ResourceRegistry.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <TouchText.h>
#include <algorithm>
#include <cmath>

using namespace GAGCore;

namespace
{
// Points. A card is a swatch with its name beside it on at most two lines.
constexpr double cardPad = 6, swatchSize = 44, labelGap = 8, labelMin = 52, labelLine = 124,
				 labelMax = 148, cardGap = 4, groupGap = 12;

std::string tr(const std::string &key)
{
	return Toolkit::getStringTable()->getString(key.c_str());
}

std::string experimentLabel(MapEdit &editor, const std::string &key)
{
	const std::string named = "[experiment " + key + "]";
	if (Toolkit::getStringTable()->doesStringExist(named))
		return tr(named);
	for (const auto &definition : registeredExperimentDefinitions())
		if (definition.key == key)
			return definition.label;
	for (const auto &definition : editor.view.scene->map.resourceRegistry().experiments())
		if (definition.key == key)
			return definition.label;
	return key;
}

// `text` in at most two lines no wider than `width` font pixels; the second
// line ends in an ellipsis when the name needs more.
std::vector<std::string> twoLines(Font *font, const std::string &text, double width)
{
	auto lines = wrapTouchText(font, text, width);
	if (lines.size() <= 2)
		return lines;
	std::string rest = lines[1];
	for (size_t i = 2; i < lines.size(); ++i)
		rest += " " + lines[i];
	const std::string ellipsis = "\xe2\x80\xa6";
	while (!rest.empty() && font->getStringWidth(rest + ellipsis) > width)
	{
		rest.pop_back();
		while (!rest.empty() && (static_cast<unsigned char>(rest.back()) & 0xc0) == 0x80)
			rest.pop_back();
	}
	return {lines[0], rest + ellipsis};
}

ViewRect intersect(ViewRect a, ViewRect b)
{
	const double left = std::max(a.x, b.x), top = std::max(a.y, b.y);
	const double right = std::min(a.x + a.w, b.x + b.w), bottom = std::min(a.y + a.h, b.y + b.h);
	return {left, top, std::max(0., right - left), std::max(0., bottom - top)};
}

SDL_Rect pixels(ViewRect r)
{
	return {int(std::floor(r.x)), int(std::floor(r.y)), int(std::ceil(r.w)), int(std::ceil(r.h))};
}

// Draws a sprite frame centred in `box`, scaled to fit it.
void drawSpriteFit(Sprite *sprite, int frame, ViewRect box, ViewRect clip)
{
	if (!sprite || frame < 0 || frame >= sprite->getFrameCount())
		return;
	const int w = std::max(1, sprite->getW(frame)), h = std::max(1, sprite->getH(frame));
	const double scale = std::min(box.w / w, box.h / h);
	auto *gfx = globalContainer->gfx;
	const SDL_Rect bounds = pixels(intersect(box, clip));
	gfx->setUITransform(float(scale), float(box.x + (box.w - w * scale) / 2),
						float(box.y + (box.h - h * scale) / 2), &bounds);
	gfx->drawSprite(0, 0, sprite, frame);
	gfx->finishDrawingSprite(sprite, 255);
	gfx->setUITransform();
}

void thickLine(double x1, double y1, double x2, double y2, double width, const Color &color)
{
	auto *gfx = globalContainer->gfx;
	const int n = std::max(1, int(std::lround(width)));
	for (int i = 0; i < n; ++i)
	{
		const float d = float(i - (n - 1) / 2.);
		gfx->drawLine(float(x1 + d), float(y1), float(x2 + d), float(y2), color);
	}
}
} // namespace

void PhoneEditor::layoutTray(double unit)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double textScale = gfx->textUnitsPerPoint();
	const bool scriptAreas = editor.selectionMode == MapEdit::ChangeAreas;
	const std::string key = std::to_string(editor.catalogRevision()) + '|' + std::to_string(paletteMode) + '|' +
							std::to_string(unit) + '|' + std::to_string(textScale) + '|' + std::to_string(scriptAreas) + std::to_string(editor.fertilityOverlayStale()) +
							'|' + (scriptAreas && editor.areaNumber ? std::to_string(editor.areaNumber->getIndex()) +
																		  (editor.view.scene ? editor.view.scene->map.getAreaName(editor.areaNumber->getIndex()) : std::string{})
																	: std::string());
	if (key == trayLayoutKey && !rows.empty())
		return;
	trayLayoutKey = key;
	rows.clear();
	chips.clear();
	const auto &catalog = editor.brushCatalog();
	double extent = cardGap * unit;
	auto addCard = [&](Row row, const std::string &text)
	{
		const double full = font->getStringWidth(text) * textScale / unit;
		double textWidth = std::max(labelMin, full);
		if (full <= labelLine)
			row.lines = {text};
		else
		{
			textWidth = labelMax;
			for (double width = labelMin + 12; width <= labelMax; width += 6)
				if (wrapTouchText(font, text, width * unit / textScale).size() <= 2)
				{
					textWidth = width;
					break;
				}
			row.lines = twoLines(font, text, textWidth * unit / textScale);
		}
		const double swatch = row.kind == Row::Kind::Widget ? 0 : swatchSize + labelGap;
		row.width = (2 * cardPad + swatch + textWidth) * unit;
		row.x = extent;
		row.chip = int(chips.size()) - 1;
		extent += row.width + cardGap * unit;
		rows.push_back(std::move(row));
	};
	auto beginGroup = [&](const std::string &title)
	{
		if (!rows.empty())
			extent += (groupGap - cardGap) * unit;
		Chip chip;
		chip.title = title;
		chip.first = int(rows.size());
		chips.push_back(std::move(chip));
	};
	auto addSection = [&](BrushSection section, bool grouped)
	{
		for (const auto &group : catalog)
		{
			if (group.section != section || group.entries.empty())
				continue;
			if (grouped)
				beginGroup(!group.title.empty() ? group.title
							   : section == BrushSection::Resources ? tr("[Resources]")
																	: group.key);
			for (const auto &entry : group.entries)
			{
				Row row;
				row.id = entry.id;
				addCard(std::move(row), entry.label);
			}
		}
	};
	if (paletteMode == 0)
	{
		addSection(BrushSection::Terrain, true);
		beginGroup(tr("[brush section tools]"));
		for (const auto &group : catalog)
		{
			if (group.section != BrushSection::Areas && group.section != BrushSection::Tools)
				continue;
			for (const auto &entry : group.entries)
			{
				Row row;
				row.id = entry.id;
				addCard(std::move(row), entry.label);
				if (entry.id == "area/script" && scriptAreas && editor.areaNumber)
				{
					// The area the script brush paints, and its name.
					const int area = editor.areaNumber->getIndex();
					Row number;
					number.kind = Row::Kind::Widget;
					number.id = "area/number";
					number.widget = editor.areaNumber;
					addCard(std::move(number), "# " + std::to_string(area + 1));
					Row name;
					name.kind = Row::Kind::Widget;
					name.id = "area/name";
					name.widget = editor.areaNameLabel;
					const std::string areaName = (editor.view.scene ? editor.view.scene->map.getAreaName(area) : std::string{});
					addCard(std::move(name), areaName.empty() ? tr("[Unnamed Area]") : areaName);
				}
			}
		}
		Row fertility;
		fertility.kind = Row::Kind::Fertility;
		fertility.id = "tool/fertility";
		// A stale overlay (terrain changed since it was computed) offers a refresh.
		addCard(std::move(fertility), editor.fertilityOverlayStale() ? tr("[editor fertility tap refresh]")
																	  : tr("[Fertility Map]"));
	}
	else if (paletteMode == 1)
		addSection(BrushSection::Resources, true);
	else if (paletteMode == 2)
		addSection(BrushSection::Buildings, false);
	else
	{
		addSection(BrushSection::Flags, false);
		addSection(BrushSection::Zones, false);
		addSection(BrushSection::Units, false);
	}
	maximum = std::max(0., extent - safe.w);
	// Chips: a pill per group, their own strip above the cards.
	double chipExtent = cardGap * unit;
	for (auto &chip : chips)
	{
		chip.width = std::max(56 * unit, font->getStringWidth(chip.title) * textScale + 24 * unit);
		chip.x = chipExtent;
		chipExtent += chip.width + cardGap * unit;
	}
	if (chips.size() < 2)
		chips.clear();
}

void PhoneEditor::prepareTray(double unit)
{
	maximum = rows.empty() ? 0. : std::max(0., rows.back().x + rows.back().width + cardGap * unit - cardBar.w);
	syncTray();
	for (auto &row : rows)
		row.rect = {cardBar.x + row.x - offset, cardBar.y + 4 * unit, row.width, cardBar.h - 12 * unit};
	if (chips.empty())
		return;
	// The chips follow the cards: the group in view is centred, so its
	// neighbours stay one tap away.
	const double chipMaximum = std::max(0., chips.back().x + chips.back().width + cardGap * unit - chipBar.w);
	if (const int active = activeChip(); active >= 0)
		chipOffset = chips[active].x + chips[active].width / 2 - chipBar.w / 2;
	chipOffset = std::clamp(chipOffset, 0., chipMaximum);
	for (auto &chip : chips)
		chip.rect = {chipBar.x + chip.x - chipOffset, chipBar.y + 6 * unit, chip.width, chipBar.h - 8 * unit};
}

int PhoneEditor::rowOf(const std::string &id) const
{
	for (size_t i = 0; i < rows.size(); ++i)
		if (rows[i].id == id)
			return int(i);
	return -1;
}

int PhoneEditor::activeChip() const
{
	if (chips.empty())
		return -1;
	// Scrolled to the end, the last group starting in view (a jump to a short
	// final group stops at the end of the strip).
	if (maximum > 0 && offset >= maximum - 1)
		for (int i = int(chips.size()) - 1; i >= 0; --i)
			if (chips[i].first < int(rows.size()) && rows[chips[i].first].x >= offset)
				return i;
	// Otherwise the group of the first card whose larger part is in view.
	for (const auto &row : rows)
		if (row.x + row.width / 2 >= offset)
			return row.chip;
	return int(chips.size()) - 1;
}

void PhoneEditor::jumpToChip(int chip)
{
	if (chip < 0 || chip >= int(chips.size()) || chips[chip].first >= int(rows.size()))
		return;
	stopScrolling();
	const double unit = globalContainer->gfx->logicalUnitsPerPoint();
	offset = std::clamp(rows[chips[chip].first].x - cardGap * unit, 0., maximum);
	syncTray();
}

bool PhoneEditor::dragPlaces(const Row &row)
{
	if (row.kind != Row::Kind::Entry)
		return false;
	const auto *entry = editor.findBrush(row.id);
	return entry && !entry->locked &&
		   (entry->section == BrushSection::Buildings || entry->section == BrushSection::Flags ||
			entry->section == BrushSection::Units);
}

void PhoneEditor::activateRow(const Row &row, ViewPoint)
{
	if (row.kind == Row::Kind::Fertility)
	{
		if (editor.fertilityOverlayStale())
		{
			editor.performAction("refresh fertility");
			return;
		}
		editor.isFertilityOn = !editor.isFertilityOn;
		editor.performAction("compute fertility");
		return;
	}
	if (row.kind == Row::Kind::Widget)
	{
		if (row.id == "area/number" && editor.areaNumber)
			editor.areaNumber->handleClick(0, 0); // Cycles 1..9 and renames the label.
		else if (row.widget)
			editor.performAction(row.widget->action);
		return;
	}
	const auto *entry = editor.findBrush(row.id);
	if (!entry)
		return;
	if (entry->locked)
	{
		// A locked brush is one tap from use: carry its experiment in this map.
		const std::string experiment = entry->experiment;
		if (!editor.enableExperimentForMap(experiment))
		{
			editor.showStatus(entry->tooltip);
			return;
		}
		editor.showStatus(
			FormattableString(tr("[Enabled %0 for this map]")).arg(experimentLabel(editor, experiment)));
		entry = editor.findBrush(row.id);
		if (!entry || entry->locked)
			return;
	}
	editor.performAction(entry->action);
}

void PhoneEditor::drawSwatch(const BrushEntry &entry, ViewRect box)
{
	auto *gfx = globalContainer->gfx;
	const ViewRect clip = intersect(box, cardBar);
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	gfx->drawFilledRect(float(box.x), float(box.y), float(box.w), float(box.h), Color(28, 32, 40));
	const auto &swatch = entry.swatch;
	switch (swatch.kind)
	{
	case BrushSwatch::Kind::Terrain:
	case BrushSwatch::Kind::Resource:
		if (auto *surface = editor.brushSwatches().get(entry, std::max(8, int(std::lround(box.w)))))
			gfx->drawSurface(int(std::lround(box.x)), int(std::lround(box.y)), surface);
		break;
	case BrushSwatch::Kind::Building:
	{
		const int type = editor.displayedBuildingSelectionType(swatch.key);
		if (auto *bt = type >= 0 ? &(*editor.view.scene->buildingTypes)[type] : nullptr)
		{
			Sprite *sprite = bt->miniSpriteImage >= 0 ? bt->miniSpritePtr : bt->gameSpritePtr;
			const int frame = bt->miniSpriteImage >= 0 ? bt->miniSpriteImage : bt->gameSpriteImage;
			if (sprite)
			{
				sprite->setBaseColor(presentationColor(editor.view.scene->entities.teams[editor.team].color));
				drawSpriteFit(sprite, frame, {box.x + 2 * u, box.y + 2 * u, box.w - 4 * u, box.h - 4 * u}, clip);
			}
		}
		break;
	}
	case BrushSwatch::Kind::Unit:
	{
		const int type = swatch.key == "explorer" ? EXPLORER : swatch.key == "warrior" ? WARRIOR : WORKER;
		Sprite *sprite = globalContainer->units;
		sprite->setBaseColor(presentationColor(editor.view.scene->entities.teams[editor.team].color));
		drawSpriteFit(sprite, unitAnimationFrame(g_unitSkins[type].startImage[STOP_WALK], 0, 0),
					  {box.x + 4 * u, box.y + 4 * u, box.w - 8 * u, box.h - 8 * u}, clip);
		break;
	}
	case BrushSwatch::Kind::Zone:
	{
		const int frame = swatch.key == "guard" ? 14 : swatch.key == "clearing" ? 25 : swatch.key == "farm" ? 58 : 13;
		drawSpriteFit(globalContainer->gamegui, frame, {box.x + 4 * u, box.y + 4 * u, box.w - 8 * u, box.h - 8 * u},
					  clip);
		break;
	}
	case BrushSwatch::Kind::Tool:
	{
		const double m = 10 * u;
		if (swatch.key == "delete")
		{
			const Color red(235, 85, 70);
			thickLine(box.x + m, box.y + m, box.x + box.w - m, box.y + box.h - m, 3 * u, red);
			thickLine(box.x + box.w - m, box.y + m, box.x + m, box.y + box.h - m, 3 * u, red);
		}
		else if (swatch.key == "no-growth")
		{
			// A sprout under a bar: resources do not spread here.
			const Color green(120, 200, 90), bar(235, 85, 70);
			thickLine(box.x + box.w / 2, box.y + box.h - m, box.x + box.w / 2, box.y + m + 6 * u, 3 * u, green);
			gfx->drawFilledRect(float(box.x + box.w / 2 - 9 * u), float(box.y + m + 4 * u), float(8 * u),
								float(5 * u), green);
			gfx->drawFilledRect(float(box.x + box.w / 2 + u), float(box.y + m), float(8 * u), float(5 * u), green);
			thickLine(box.x + m, box.y + box.h - m, box.x + box.w - m, box.y + m, 3 * u, bar);
		}
		else
		{
			// Script areas: a dashed outline.
			const Color line(200, 210, 255);
			for (double t = 0; t < box.w - 2 * m; t += 8 * u)
			{
				const double len = std::min(4 * u, box.w - 2 * m - t);
				gfx->drawFilledRect(float(box.x + m + t), float(box.y + m), float(len), float(2 * u), line);
				gfx->drawFilledRect(float(box.x + m + t), float(box.y + box.h - m - 2 * u), float(len), float(2 * u),
									line);
				gfx->drawFilledRect(float(box.x + m), float(box.y + m + t), float(2 * u), float(len), line);
				gfx->drawFilledRect(float(box.x + box.w - m - 2 * u), float(box.y + m + t), float(2 * u),
									float(len), line);
			}
		}
		break;
	}
	default:
		break;
	}
}

void PhoneEditor::drawCard(const Row &row)
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double u = gfx->logicalUnitsPerPoint(), scale = gfx->textUnitsPerPoint();
	const ViewRect r = row.rect;
	const ViewRect visible = intersect(r, cardBar);
	if (visible.w <= 0)
		return;
	const SDL_Rect clip = pixels(visible);
	gfx->setClipRect(clip.x, clip.y, clip.w, clip.h);
	const BrushEntry *entry = row.kind == Row::Kind::Entry ? editor.findBrush(row.id) : nullptr;
	const bool selected = row.kind == Row::Kind::Fertility ? editor.isFertilityOn
						  : row.kind == Row::Kind::Widget  ? false
														   : editor.currentBrushId() == row.id;
	const bool locked = entry && entry->locked;
	gfx->drawFilledRect(float(r.x), float(r.y), float(r.w), float(r.h),
						selected ? InGameTouchTheme::selected() : InGameTouchTheme::field());
	double textX = r.x + cardPad * u;
	if (row.kind != Row::Kind::Widget)
	{
		const double side = swatchSize * u;
		const ViewRect box{r.x + cardPad * u, r.y + (r.h - side) / 2, side, side};
		if (entry)
			drawSwatch(*entry, box);
		else
		{
			// Fertility: bars from barren to fertile.
			const Color shades[] = {{120, 92, 52}, {150, 140, 60}, {120, 170, 70}, {70, 175, 80}};
			gfx->drawFilledRect(float(box.x), float(box.y), float(box.w), float(box.h), Color(28, 32, 40));
			const double bar = (box.w - 10 * u) / 4;
			for (int i = 0; i < 4; ++i)
			{
				const double h = box.h * (0.3 + 0.15 * i);
				gfx->drawFilledRect(float(box.x + 5 * u + i * bar), float(box.y + box.h - 5 * u - h + 5 * u),
									float(bar - 2 * u), float(h - 5 * u), shades[i]);
			}
		}
		gfx->setClipRect(clip.x, clip.y, clip.w, clip.h);
		if (locked)
		{
			// Dimmed with a padlock: a tap enables the experiment for this map.
			gfx->drawFilledRect(float(box.x), float(box.y), float(box.w), float(box.h), Color(10, 12, 18, 150));
			const double w = 16 * u, h = 12 * u, x = box.x + box.w - w - 3 * u, y = box.y + box.h - h - 3 * u;
			const Color gold(240, 205, 110);
			gfx->drawRect(float(x + 3 * u), float(y - 8 * u), float(w - 6 * u), float(10 * u), gold);
			gfx->drawRect(float(x + 4 * u), float(y - 7 * u), float(w - 8 * u), float(8 * u), gold);
			gfx->drawFilledRect(float(x), float(y), float(w), float(h), gold);
			gfx->drawFilledRect(float(x + w / 2 - u), float(y + 3 * u), float(2 * u), float(5 * u), Color(60, 45, 20));
		}
		if (row.kind == Row::Kind::Fertility && editor.isFertilityOn)
		{
			const bool stale = editor.fertilityOverlayStale();
			const Color edge = stale ? Color(245, 180, 60) : InGameTouchTheme::ink();
			for (int i = 0; i < (stale ? 2 : 1); ++i)
				gfx->drawRect(float(box.x + i * u), float(box.y + i * u), float(box.w - 2 * i * u),
							  float(box.h - 2 * i * u), edge);
		}
		textX = box.x + box.w + labelGap * u;
	}
	if (selected)
		for (int i = 0; i < int(std::max(1., std::round(2 * u))); ++i)
			gfx->drawRect(float(r.x + i), float(r.y + i), float(r.w - 2 * i), float(r.h - 2 * i),
						  InGameTouchTheme::ink());
	const double line = font->getStringHeight("Ag") * scale;
	double y = r.y + (r.h - line * row.lines.size()) / 2;
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, locked ? Color(200, 196, 184) : Color(255, 249, 229)));
	for (const auto &text : row.lines)
	{
		gfx->setUITransform(float(scale), float(textX), float(y), &clip);
		gfx->drawString(0, 0, font, text);
		gfx->setUITransform();
		y += line;
	}
	font->popStyle();
	gfx->setClipRect();
}

void PhoneEditor::drawTray()
{
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double u = gfx->logicalUnitsPerPoint(), scale = gfx->textUnitsPerPoint();
	for (const auto &row : rows)
		drawCard(row);
	// Group boundaries: a short rule in the gap between groups.
	for (size_t i = 1; i < chips.size(); ++i)
		if (chips[i].first < int(rows.size()))
		{
			const auto &r = rows[chips[i].first].rect;
			const double x = r.x - (groupGap / 2 + 1) * u;
			if (x > cardBar.x && x < cardBar.x + cardBar.w)
				gfx->drawFilledRect(float(x), float(r.y + 8 * u), float(2 * u), float(r.h - 16 * u),
									InGameTouchTheme::border());
		}
	if (!chips.empty())
	{
		const int active = activeChip();
		const SDL_Rect bar = pixels(chipBar);
		for (size_t i = 0; i < chips.size(); ++i)
		{
			const auto &chip = chips[i];
			const ViewRect visible = intersect(chip.rect, chipBar);
			if (visible.w <= 0)
				continue;
			gfx->setClipRect(bar.x, bar.y, bar.w, bar.h);
			gfx->drawFilledRect(float(chip.rect.x), float(chip.rect.y), float(chip.rect.w), float(chip.rect.h),
								int(i) == active ? InGameTouchTheme::selected() : InGameTouchTheme::field());
			if (int(i) == active)
				gfx->drawFilledRect(float(chip.rect.x + 6 * u), float(chip.rect.y + chip.rect.h - 3 * u),
									float(chip.rect.w - 12 * u), float(2 * u), InGameTouchTheme::ink());
			const SDL_Rect clip = pixels(visible);
			const double width = font->getStringWidth(chip.title) * scale;
			const double height = font->getStringHeight(chip.title) * scale;
			font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255, 249, 229)));
			gfx->setUITransform(float(scale), float(chip.rect.x + (chip.rect.w - width) / 2),
								float(chip.rect.y + (chip.rect.h - height) / 2), &clip);
			gfx->drawString(0, 0, font, chip.title);
			gfx->setUITransform();
			font->popStyle();
		}
		gfx->setClipRect();
	}
	if (maximum > 0)
		gfx->drawFilledRect(int(cardBar.x + offset / (maximum + cardBar.w) * cardBar.w),
							int(cardBar.y + cardBar.h - 4 * u), int(cardBar.w * cardBar.w / (maximum + cardBar.w)),
							int(2 * u), InGameTouchTheme::border());
}

void PhoneEditor::drawStatusToast()
{
	// MapEdit::showStatus messages (placement hints, enabled experiments) sit
	// above the tray, where the desktop's bottom-left status would be covered.
	if (editor.statusText.empty() || SDL_GetTicks() >= editor.statusUntil)
		return;
	auto *gfx = globalContainer->gfx;
	auto *font = globalContainer->standardFont;
	const double u = gfx->logicalUnitsPerPoint(), scale = gfx->textUnitsPerPoint();
	const double maxWidth = std::min(content.w - 24 * u, 460 * u);
	const auto lines = wrapTouchText(font, editor.statusText, (maxWidth - 24 * u) / scale);
	double width = 0;
	for (const auto &line : lines)
		width = std::max(width, font->getStringWidth(line) * scale);
	const double line = font->getStringHeight("Ag") * scale;
	const double w = width + 24 * u, h = line * lines.size() + 16 * u;
	const double x = content.x + (content.w - w) / 2, y = content.y + content.h - h - 12 * u;
	gfx->drawFilledRect(float(x), float(y), float(w), float(h), Color(16, 18, 26, 225));
	gfx->drawRect(float(x), float(y), float(w), float(h), InGameTouchTheme::border());
	const SDL_Rect clip = pixels({x, y, w, h});
	font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(255, 249, 229)));
	double ty = y + 8 * u;
	for (const auto &text : lines)
	{
		gfx->setUITransform(float(scale), float(x + 12 * u), float(ty), &clip);
		gfx->drawString(0, 0, font, text);
		gfx->setUITransform();
		ty += line;
	}
	font->popStyle();
}

void PhoneEditor::publishControls(bool shown)
{
	if (!ApplicationHost::controlsObserved())
		return;
	if (!shown)
	{
		if (!publishedControls.empty())
			ApplicationHost::controlsChanged(this, nullptr);
		publishedControls.clear();
		return;
	}
	auto *gfx = globalContainer->gfx;
	std::string json = "{\"surface\":{\"w\":" + std::to_string(gfx->getW()) + ",\"h\":" + std::to_string(gfx->getH()) +
					   "},\"root\":{\"x\":" + std::to_string(int(modeBar.x)) + ",\"y\":" + std::to_string(int(modeBar.y)) +
					   ",\"w\":" + std::to_string(int(modeBar.w)) + ",\"h\":" + std::to_string(int(modeBar.h + tray.h)) +
					   "},\"controls\":{";
	bool first = true;
	auto add = [&](const std::string &key, ViewRect r, ViewRect area, const std::string &label)
	{
		const ViewRect v = intersect(r, area);
		std::string quoted;
		for (char c : label)
			if (c == '"' || c == '\\')
				quoted += std::string("\\") + c;
			else if (static_cast<unsigned char>(c) >= 0x20)
				quoted += c;
		json += std::string(first ? "" : ",") + "\"" + key + "\":{\"x\":" + std::to_string(int(r.x)) + ",\"y\":" +
				std::to_string(int(r.y)) + ",\"w\":" + std::to_string(int(r.w)) + ",\"h\":" + std::to_string(int(r.h)) +
				",\"visible\":{\"x\":" + std::to_string(int(v.x)) + ",\"y\":" + std::to_string(int(v.y)) + ",\"w\":" +
				std::to_string(int(v.w)) + ",\"h\":" + std::to_string(int(v.h)) + "},\"enabled\":true,\"label\":\"" + quoted +
				"\"}";
		first = false;
	};
	for (int i = 0; i < modeCount; ++i)
	{
		const ViewRect r{modeBar.x + i * modeBar.w / modeCount, modeBar.y, modeBar.w / modeCount, modeBar.h};
		add("tray/mode/" + std::to_string(i), r, r, "");
	}
	for (size_t i = 0; i < chips.size(); ++i)
		add("tray/chip/" + std::to_string(i), chips[i].rect, chipBar, chips[i].title);
	for (const auto &row : rows)
		add("tray/" + row.id, row.rect, cardBar, row.lines.empty() ? std::string() : row.lines.front());
	json += "}}";
	if (json == publishedControls)
		return;
	publishedControls = std::move(json);
	ApplicationHost::controlsChanged(this, publishedControls.c_str());
}
