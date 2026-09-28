// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <ApplicationHost.h>
#include "EndGameScreen.h"
#include "gui/PhoneForm.h"
#include "gui/InGameTouchTheme.h"
#include "gui/MobileSafeArea.h"
#include <InterfacePresentation.h>
#include <FormatableString.h>
#include <GUIStyle.h>
#include <GUIText.h>
#include <GUIButton.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <typeinfo>
#include "GlobalContainer.h"
#include "Team.h"
#include "TeamDisplay.h"
#include "GameGUILoadSave.h"
#include "ReplayWriter.h"
#include "SDLCompat.h"
#include "Utilities.h"

EndGameStat::EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game)
{
	this->x = x;
	this->y = y;
	this->w = w;
	this->h = h;

	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;

	this->game = game;

	isTeamEnabled = new bool[Team::MAX_COUNT];
	for (int x = 0; x < Team::MAX_COUNT; ++x)
		isTeamEnabled[x] = true;

	this->type = EndOfGameStat::TYPE_UNITS;
	mouse_x = -1;
	mouse_y = -1;
}

EndGameStat::~EndGameStat()
{
	delete[] isTeamEnabled;
}

void EndGameStat::setStatType(int type)
{
	this->type = type;
}

void EndGameStat::setEnabledState(int teamNum, bool isEnabled)
{
	isTeamEnabled[teamNum] = isEnabled;
}

void EndGameStat::paintPhone(int width, int height)
{
	const auto oldX = x, oldY = y, oldW = w, oldH = h;
	const auto horizontal = hAlignFlag, vertical = vAlignFlag;
	setScreenRectangle(0, 0, width, height);
	paint();
	x = oldX;
	y = oldY;
	w = oldW;
	h = oldH;
	hAlignFlag = horizontal;
	vAlignFlag = vertical;
}

void EndGameStat::paint(void)
{
	if (type >= 6)
	{
		paintMeasurements();
		return;
	}
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	assert(parent);
	assert(parent->getSurface());

	if (game->teams[0]->stats.endOfGameStats.size() < 2)
	{
		parent->getSurface()->drawString(x + 8, y + 16, globalContainer->standardFont,
										 "Not enough recorded history yet.");
		return;
	}
	// find maximum
	int team, maxValue = 0;
	unsigned int pos = 0;
	for (team = 0; team < game->mapHeader.getNumberOfTeams(); team++)
		if (isTeamEnabled[team])
			for (pos = 0; pos < game->teams[team]->stats.endOfGameStats.size(); pos++)
				maxValue =
					std::max(maxValue, game->teams[team]->stats.endOfGameStats[pos].value[type]);

	///You can't draw anything if the game ended so quickly that there weren't two recorded values to draw a line between
	if (game->teams[0]->stats.endOfGameStats.size() >= 2)
	{
		//Calculate the number of digits used by the max value when rounded up to the nearest 10
		int num = 10;
		maxValue += num - (maxValue % num);
		std::stringstream maxstr;
		maxstr << maxValue << std::endl;
		int max_digit_count = maxstr.str().size();

		//Compute the maximum width used by the right-scale
		int max_width = -1;
		for (int n = 0; n < num; ++n)
		{
			int value = maxValue - (maxValue * n) / num;
			std::string valueText = getRightScaleText(value, max_digit_count - 1);
			int width = globalContainer->littleFont->getStringWidth(valueText.c_str());
			max_width = std::max(width, max_width);
		}

		//Compute the maximum height used by the time-scale
		int time_period = (game->teams[0]->stats.endOfGameStats.size() * 512) / 25;
		int max_height = 0;
		for (int n = 1; n < 16; ++n)
		{
			int time = (time_period * n) / 15;
			std::string timeText = getTimeText(time);
			int height = globalContainer->littleFont->getStringHeight(timeText.c_str());
			max_height = std::max(height, max_height);
		}

		///Effective width and height
		int e_width = w - max_width - 8;
		int e_height = h - max_height - 8;

		//Draw horizontal lines to given the scale of the graphs values.
		double line_separate = double(e_height) / double(num);
		// Short landscape charts need fewer labels, not overlapping text.
		const int valueStride =
			std::max(1, int(std::ceil((max_height + 4) / std::max(1.0, line_separate))));
		for (int n = 0; n < num; n += valueStride)
		{
			int pos = int(double(n) * line_separate + 0.5);
			int value = maxValue - (maxValue * n) / num;
			if (n != 0)
			{
				parent->getSurface()->drawHorzLine(x, y + pos, e_width, Color(68, 51, 82));
				parent->getSurface()->drawHorzLine(x + e_width - 5, y + pos, 10,
												   InGameTouchTheme::ink);
			}
			std::string valueText = getRightScaleText(value, max_digit_count - 1);
			int height = globalContainer->littleFont->getStringHeight(valueText.c_str());
			parent->getSurface()->drawString(x + e_width + 8, y + pos - height / 2,
											 globalContainer->littleFont, valueText.c_str());
		}

		///Draw vertical lines to give the timescale
		double time_line_separate = double(e_width) / double(15);
		for (int n = 1; n < 16; n += std::max(1, 1200 / std::max(1, e_width)))
		{
			int pos = int(double(x) + time_line_separate * double(n) + 0.5);
			int time = (time_period * n) / 15;
			if (n != 15)
				parent->getSurface()->drawVertLine(pos, y + e_height - 5, 10,
												   InGameTouchTheme::ink);
			std::string timeText = getTimeText(time);
			int width = globalContainer->littleFont->getStringWidth(timeText.c_str());
			parent->getSurface()->drawString(pos - width / 2, y + e_height + 8,
											 globalContainer->littleFont, timeText);
		}

		// draw background
		parent->getSurface()->drawRect(x, y, e_width, e_height, InGameTouchTheme::border);

		int closest_position = std::numeric_limits<int>::max();
		int circle_position_value = -1;
		int circle_position_x = -1;
		int circle_position_y = -1;

		// draw curve
		if (maxValue)
		{
			for (team = 0; team < game->mapHeader.getNumberOfTeams(); team++)
			{
				if (!isTeamEnabled[team])
				{
					continue;
				}
				const Color &color = game->teams[team]->color;

				int previous_y =
					e_height - int(double(e_height) * getValue(0, team, type) / double(maxValue));

				for (int px = 0; px < (e_width - 2); ++px)
				{
					double value = getValue(double(px) / double(e_width - 2), team, type);
					int ny = e_height - int(double(e_height) * value / double(maxValue));
					parent->getSurface()->drawLine(x + px, y + previous_y, x + px + 1, y + ny,
												   color);
					previous_y = ny;
					const int dist = std::abs(mouse_x - px - 1) * 4096 + std::abs(mouse_y - ny);
					if (mouse_x >= 0 && mouse_x < e_width && mouse_y >= 0 && mouse_y < e_height &&
						dist < closest_position)
					{
						circle_position_value = int(std::floor(value + 0.5));
						circle_position_x = x + px;
						circle_position_y = y + ny;
						closest_position = dist;
					}
				}
			}
		}
		if (circle_position_x != -1)
		{
			parent->getSurface()->drawVertLine(circle_position_x, y, e_height,
											   InGameTouchTheme::border);
			parent->getSurface()->drawCircle(circle_position_x, circle_position_y, 10,
											 Color::white);
			std::stringstream str;
			str << circle_position_value;
			parent->getSurface()->drawString(circle_position_x + 10, circle_position_y + 10,
											 globalContainer->littleFont, str.str());
		}

		// Metric and elapsed-time labels belong to the enclosing chart view;
		// putting them inside the plot obscures the curves on short viewports.
	}
	else
	{
		// draw background
		parent->getSurface()->drawRect(x, y, w, h, InGameTouchTheme::border);
	}
}

double EndGameStat::getValue(double position, int team, int type)
{
	int s = game->teams[team]->stats.endOfGameStats.size() - 1;
	int lower = int(position * float(s));
	int upper = lower + 1;
	double mu = (position * float(s)) - lower;

	int y1 = game->teams[team]->stats.endOfGameStats[lower].value[type];
	int y2 = game->teams[team]->stats.endOfGameStats[upper].value[type];

	//Linear interpolation
	return (1 - mu) * y1 + mu * y2;
}

std::string EndGameStat::getTimeText(int seconds)
{
	int min = int(seconds) / 60;
	int sec = int(seconds) % 60;
	std::stringstream str;
	str << min << ":" << std::setw(2) << std::setfill('0') << sec << std::endl;
	return str.str();
}

std::string EndGameStat::getRightScaleText(int value, int digits)
{
	std::stringstream str;
	str << std::setw(digits) << std::setfill('0') << value << std::endl;
	return str.str();
}

std::string EndGameStat::getStatLabel()
{
	switch (type)
	{
	case EndOfGameStat::TYPE_UNITS:
		return Toolkit::getStringTable()->getString("[Number Of Units]");
	case EndOfGameStat::TYPE_BUILDINGS:
		return Toolkit::getStringTable()->getString("[Number Of Buildings]");
	case EndOfGameStat::TYPE_PRESTIGE:
		return Toolkit::getStringTable()->getString("[Prestige Score]");
	case EndOfGameStat::TYPE_HP:
		return Toolkit::getStringTable()->getString("[Total Hitpoints]");
	case EndOfGameStat::TYPE_ATTACK:
		return Toolkit::getStringTable()->getString("[Total Attack Power]");
	case EndOfGameStat::TYPE_DEFENSE:
		return Toolkit::getStringTable()->getString("[Total Defence Power]");
	default:
		assert(false);
		return "No clue how we got here.";
	}
}

void EndGameStat::onSDLMouseMotion(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	if (event->motion.x > x && event->motion.x < x + w && event->motion.y > y &&
		event->motion.y < y + h)
	{
		mouse_x = event->motion.x - x;
		mouse_y = event->motion.y - y;
	}
	else
	{
		mouse_x = -1;
		mouse_y = -1;
	}
}

//! This function is used to sort the player array
struct MoreScore
{
	int type;
	bool operator()(const TeamEntry &t1, const TeamEntry &t2)
	{
		if (t1.endVal[type] == t2.endVal[type])
		{
			if (t1.teamNum == t2.teamNum)
				return t1.name > t2.name;
			return t1.teamNum > t2.teamNum;
		}
		return t1.endVal[type] > t2.endVal[type];
	}
};

EndGameScreen::EndGameScreen(GameGUI *gui)
{
	// We're no longer replaying a game
	globalContainer->replaying = false;

	statWidget = new EndGameStat(20, 80, 180, 120, ALIGN_FILL, ALIGN_FILL, &(gui->game));
	addWidget(statWidget);

	// set teams entries for later sort
	for (int i = 0; i < gui->game.gameHeader.getNumberOfPlayers(); i++)
	{
		struct TeamEntry entry;
		entry.name = gui->game.gameHeader.getBasePlayer(i).name;
		entry.teamNum = gui->game.gameHeader.getBasePlayer(i).teamNumber;
		entry.color = gui->game.teams[entry.teamNum]->color;
		int endIndex = gui->game.teams[entry.teamNum]->stats.endOfGameStats.size() - 1;
		for (int j = 0; j < EndOfGameStat::TYPE_NB_STATS; j++)
		{
			entry.endVal[j] =
				endIndex >= 0
					? gui->game.teams[entry.teamNum]->stats.endOfGameStats[endIndex].value[j]
					: 0;
		}
		for (int j = 0; j < 30; ++j)
			entry.endVal[j + 6] =
				TeamStats::graphValue(gui->game.teams[entry.teamNum]->stats.measurements, j);
		auto existing = std::find_if(teams.begin(), teams.end(), [&](const auto &team)
									 { return team.teamNum == entry.teamNum; });
		if (existing == teams.end())
			teams.push_back(entry);
		else
			existing->name += ", " + entry.name;
	}

	// Save the step and order count
	game = &(gui->game);

	sortAndSet(EndOfGameStat::TYPE_UNITS);
}

void EndGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (par1 == QUIT)
		{
			endExecute(par1);
		}
		else if (par1 >= STAT_BUTTON_FIRST &&
				 par1 < STAT_BUTTON_FIRST + EndOfGameStat::TYPE_NB_STATS)
			selectMetric(par1 - STAT_BUTTON_FIRST);
		///One of the buttons beside the team names where selected
		else if (par1 >= TEAM_TOGGLE_FIRST &&
				 par1 < static_cast<int>(TEAM_TOGGLE_FIRST + teams.size()))
		{
			const int n = par1 - TEAM_TOGGLE_FIRST;
			teams[n].enabled = !teams[n].enabled;
			statWidget->setEnabledState(teams[n].teamNum, teams[n].enabled);
		}
		/// The "Save Replay" button was pressed
		else if (par1 == SAVE_REPLAY)
		{
			saveReplay("replays", "replay");
		}
		else
			assert(false);
	}
}

std::string EndGameScreen::statTypeName(int type)
{
	if (type >= 6)
		return Toolkit::getStringTable()->getString(TeamStats::measurementLabel(type - 6));
	switch (type)
	{
	case EndOfGameStat::TYPE_UNITS:
		return Toolkit::getStringTable()->getString("[Units]");
	case EndOfGameStat::TYPE_BUILDINGS:
		return Toolkit::getStringTable()->getString("[Buildings]");
	case EndOfGameStat::TYPE_PRESTIGE:
		return Toolkit::getStringTable()->getString("[Prestige]");
	case EndOfGameStat::TYPE_HP:
		return Toolkit::getStringTable()->getString("[hp]");
	case EndOfGameStat::TYPE_ATTACK:
		return Toolkit::getStringTable()->getString("[Attack]");
	case EndOfGameStat::TYPE_DEFENSE:
		return Toolkit::getStringTable()->getString("[Defense]");
	default:
		assert(false);
		return "";
	}
}

void EndGameScreen::sortAndSet(int type)
{
	// Selection belongs to team identity, not the visible rank or widget order.
	MoreScore moreScore;
	moreScore.type = type;
	std::stable_sort(teams.begin(), teams.end(), moreScore);
}

//! LoadSaveScreen name-extractor callback for the save-replay dialog: turn a
//! full virtual path like "replays/My_Game.replay" into the display name
//! "My Game". Directory prefix and extension are only removed when actually
//! present, so a stray file in replays/ degrades to showing its raw name
//! instead of throwing (erase(npos) used to crash the dialog).
std::string replayFilenameToName(const std::string &fullfilename)
{
	std::string filename =
		Utilities::stripSuffix(Utilities::stripPrefix(fullfilename, "replays/"), ".replay");
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

EndGameScreen::~EndGameScreen() = default;

void EndGameScreen::saveReplay(const char *dir, const char *ext)
{
	replaySave = std::make_unique<LoadSaveScreen>(
		dir, ext, false, Toolkit::getStringTable()->getString("[save replay]"), "",
		replayFilenameToName, glob2NameToFilename);
	replayForm = std::make_unique<PhoneForm>(
		*replaySave, [](auto *) { return std::string{}; }, [](auto *) { return true; });
	GAGCore::ApplicationHost::screenChanged(typeid(*replaySave).name());
}

void EndGameScreen::updateExecution(Uint32 tick)
{
	if (!replaySave)
	{
		Glob2Screen::updateExecution(tick);
		return;
	}
	replaySave->dispatchTimer(tick);
	if (replaySave->pollPersistence() || replaySave->endValue == LoadSaveScreen::CANCEL)
	{
		replayForm.reset();
		replaySave.reset();
		GAGCore::ApplicationHost::screenChanged(typeid(*this).name());
	}
	else if (replaySave->endValue == LoadSaveScreen::OK)
	{
		if (!globalContainer->replayWriter ||
			!globalContainer->replayWriter->write(replaySave->getFileName()))
		{
			replaySave->showSaveFailure();
		}
		else
			replaySave->beginPersistence(GAGCore::ApplicationHost::persistStorage());
	}
}

void EndGameScreen::handleExecutionEvent(SDL_Event event)
{
	if (!replaySave)
	{
		GraphicContext::translateMouseEvent(&event);
		if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
			event.type == SDL_FINGERUP)
		{
			const ViewPoint point{event.tfinger.x * gfx->getW(), event.tfinger.y * gfx->getH()};
			const auto actions =
				event.type == SDL_FINGERDOWN
					? resultGesture.down(event.tfinger.touchId, event.tfinger.fingerId, point)
				: event.type == SDL_FINGERMOTION
					? resultGesture.move(event.tfinger.touchId, event.tfinger.fingerId, point)
					: resultGesture.up(event.tfinger.touchId, event.tfinger.fingerId, point);
			for (const auto &action : actions)
			{
				if (action.kind == TouchActionKind::Select)
				{
					SDL_Event click{};
					click.type = SDL_MOUSEBUTTONDOWN;
					click.button.button = SDL_BUTTON_LEFT;
					click.button.x = int(point.x);
					click.button.y = int(point.y);
					handleExecutionEvent(click);
				}
				else if (action.kind == TouchActionKind::Pan && metricPicker.isOpen())
				{
					SDL_Event wheel{};
					wheel.type = SDL_MOUSEWHEEL;
					wheel.wheel.y = action.point.y > 0 ? 1 : -1;
					metricPicker.handleEvent(wheel);
				}
				else if (action.kind == TouchActionKind::Pan && teamFiltersOpen)
					teamFilterScroll = std::max(0.0, teamFilterScroll - action.point.y);
			}
			statWidget->inspectScreenPoint(int(point.x), int(point.y));
			return;
		}
		if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
			event.button.which == SDL_TOUCH_MOUSEID)
			return;
		if (metricPicker.isOpen())
		{
			const int choice = metricPicker.handleEvent(event);
			if (choice >= 0)
				selectMetric(choice);
			return;
		}
		if (event.type == SDL_MOUSEWHEEL && teamFiltersOpen)
		{
			teamFilterScroll = std::max(0.0, teamFilterScroll - event.wheel.y * 48.0);
			return;
		}
		if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
		{
			const ViewPoint point{double(event.button.x), double(event.button.y)};
			for (const auto &control : resultControls)
				if (control.rect.contains(point))
				{
					activateResultControl(control.action);
					return;
				}
			statWidget->inspectScreenPoint(event.button.x, event.button.y);
		}
		else if (event.type == SDL_MOUSEMOTION)
			statWidget->inspectScreenPoint(event.motion.x, event.motion.y);
		else if (event.type == SDL_KEYDOWN &&
				 (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN ||
				  event.key.keysym.sym == SDLK_KP_ENTER))
			endExecute(QUIT);
		return;
	}
	GAGCore::GraphicContext::translateMouseEvent(&event);
	if (GAGCore::phonePresentationRequested() && replayForm && replayForm->event(event))
		return;
	replaySave->translateAndProcessEvent(&event);
}

void EndGameScreen::drawExecution()
{
	if (!isExecutionRunning())
		return;
	drawResults();
	if (replaySave)
	{
		if (GAGCore::phonePresentationRequested())
			replayForm->draw();
		else
		{
			replaySave->dispatchPaint(false);
			gfx->drawSurface(replaySave->decX, replaySave->decY, replaySave->getSurface());
		}
	}
	gfx->nextFrame();
}
void EndGameScreen::selectMetric(int metric)
{
	if (metric < 0 || metric >= 36)
		return;
	selectedMetric = metric;
	statWidget->setStatType(metric);
	sortAndSet(metric);
}
void EndGameScreen::activateResultControl(int action)
{
	if (action == 100)
	{
		const auto r = resultControls.front().rect;
		std::vector<std::string> options;
		for (int i = 0; i < 36; ++i)
			options.push_back(statTypeName(i));
		const auto safe = mobileDialogSafe(globalContainer->gfx);
		metricPicker.open({int(r.x), int(r.y), int(r.w), int(r.h)},
						  {int(safe.x) + 8, int(safe.y) + 8, int(safe.w) - 16, int(safe.h) - 16},
						  options, selectedMetric,
						  globalContainer->standardFont);
	}
	else if (action == 101)
	{
		expandedChart = !expandedChart;
		teamFiltersOpen = false;
	}
	else if (action == 102)
		teamFiltersOpen = !teamFiltersOpen;
	else if (action >= 200)
	{
		const int index = action - 200;
		if (index >= int(teams.size()))
			return;
		onAction(nullptr, BUTTON_RELEASED, TEAM_TOGGLE_FIRST + index, 0);
	}
	else
		onAction(nullptr, BUTTON_RELEASED, action, 0);
}
void EndGameScreen::drawResults()
{
	using namespace InGameTouchTheme;
	auto *context = globalContainer->gfx;
	const double unit = context->logicalUnitsPerPoint();
	const auto safe = mobileDialogSafe(context);
	resultControls.clear();
	gfx->setClipRect();
	gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), Color(34, 24, 49));
	auto label = [&](ViewRect r, const std::string &text)
	{
		SDL_Rect clip{int(r.x), int(r.y), int(r.w), int(r.h)};
		TextStyle style(globalContainer->standardFont);
		context->setUITransform(unit, r.x + 8 * unit, r.y + (r.h - 16 * unit) / 2, &clip);
		gfx->drawString(0, 0, globalContainer->standardFont, text);
		context->setUITransform();
		gfx->setClipRect();
	};
	auto button = [&](ViewRect r, const std::string &text, int action, bool selected = false)
	{
		gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
							selected ? InGameTouchTheme::selected : field);
		gfx->drawRect(int(r.x), int(r.y), int(r.w), int(r.h), border);
		label(r, text);
		resultControls.push_back({r, action});
	};
	const double target = 48 * unit, gap = 8 * unit;
	const bool compact = safe.w < 700 * unit || safe.h < 450 * unit;
	double top = safe.y + gap;
	if (!expandedChart || !compact)
	{
		const double filtersWidth = compact ? 128 * unit : 0;
		button({safe.x + gap, top, safe.w - 2 * gap - filtersWidth, target},
			   statTypeName(selectedMetric) + "  ▾", 100);
		if (compact)
		{
			const int enabled =
				std::count_if(teams.begin(), teams.end(), [](const auto &t) { return t.enabled; });
			button({safe.x + safe.w - gap - filtersWidth, top, filtersWidth, target},
				   "Teams " + std::to_string(enabled) + "/" + std::to_string(teams.size()), 102,
				   teamFiltersOpen);
		}
		top += target + gap;
	}
	auto drawTeamFilters = [&](double y)
	{
		const int columns = std::max(1, int((safe.w - gap) / (160 * unit)));
		const double extent = std::ceil(teams.size() / double(columns)) * (target + gap);
		const double available = safe.y + safe.h - target - 2 * gap - y;
		if (compact)
		{
			teamFilterScroll =
				std::clamp(teamFilterScroll, 0.0, std::max(0.0, (extent - available) / unit));
			gfx->drawFilledRect(int(safe.x), int(y), int(safe.w), int(std::min(available, extent)),
								Color(34, 24, 49));
		}
		int index = 0;
		for (size_t i = 0; i < teams.size(); ++i)
		{
			const double width = (safe.w - (columns + 1) * gap) / columns;
			ViewRect r{safe.x + gap + (index % columns) * (width + gap),
					   y + (index / columns) * (target + gap) -
						   (compact ? teamFilterScroll * unit : 0),
					   width, target};
			if (compact && (r.y < y || r.y + r.h > y + available))
			{
				++index;
				continue;
			}
			button(r, (teams[i].enabled ? "[x] " : "[ ] ") + teams[i].name, 200 + i,
				   teams[i].enabled);
			gfx->drawFilledRect(int(r.x), int(r.y), int(4 * unit), int(r.h), teams[i].color);
			++index;
		}
		return std::ceil(index / double(columns)) * (target + gap);
	};
	if (!compact && !expandedChart)
		top += drawTeamFilters(top);
	const double bottom = safe.y + safe.h - target - gap;
	const double chartHeight = std::max(48 * unit, bottom - top - 24 * unit);
	statWidget->setScreenRectangle(int(safe.x + 16 * unit), int(top + 8 * unit),
								   int(safe.w - 80 * unit), int(chartHeight - 16 * unit));
	{
		TextStyle chartText(globalContainer->littleFont);
		TextStyle chartLabels(globalContainer->standardFont);
		if (std::none_of(teams.begin(), teams.end(), [](const auto &team) { return team.enabled; }))
			label({safe.x + gap, top + 48 * unit, safe.w - 2 * gap, 48 * unit},
				  "Select a team to show its history.");
		else
			statWidget->paint();
	}
	label({safe.x + gap, bottom - 28 * unit, safe.w - 2 * gap, 24 * unit},
		  "Elapsed time · " + statTypeName(selectedMetric));
	const bool save = globalContainer->replayWriter && globalContainer->replayWriter->isValid();
	const int count = save ? 3 : 2;
	const double width = (safe.w - (count + 1) * gap) / count;
	button({safe.x + gap, bottom, width, target}, expandedChart ? "Back to chart" : "Expand chart",
		   101);
	if (save)
		button({safe.x + 2 * gap + width, bottom, width, target},
			   Toolkit::getStringTable()->getString("[save replay]"), SAVE_REPLAY);
	button({safe.x + safe.w - gap - width, bottom, width, target},
		   Toolkit::getStringTable()->getString("[quit]"), QUIT);
	if (compact && teamFiltersOpen && !expandedChart)
		drawTeamFilters(top);
	metricPicker.paint(gfx, ink, field, InGameTouchTheme::selected, border);
}

void EndGameScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
	resultGesture.cancel();
	metricPicker.close();
	Glob2Screen::viewportResized(oldWidth, oldHeight, width, height);
	if (replayForm)
		replayForm->cancel();
	if (replaySave)
		replaySave->viewportResized(oldWidth, oldHeight, width, height);
}

void EndGameStat::paintMeasurements()
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	auto *surface = parent->getSurface();
	auto *font = globalContainer->littleFont;
	Uint64 maximum = 1;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
		if (isTeamEnabled[t])
			for (const auto &m : game->teams[t]->stats.measurementHistory)
				if (type - 6 < 16 || m.tick > game->teams[t]->stats.extendedCoverageStartTick ||
					(game->teams[t]->stats.extendedCoverageStartTick == 0 && m.tick == 0))
					maximum = std::max(maximum, TeamStats::graphValue(m, type - 6));
	const std::string scale = std::to_string(maximum);
	const int ew = std::max(1, w - font->getStringWidth(scale) - 12), eh = std::max(1, h - 24);
	surface->drawRect(x, y, ew, eh, InGameTouchTheme::border);
	surface->drawString(x + ew + 4, y, font, scale);
	surface->drawString(x + ew + 4, y + eh - 12, font, "0");
	const Uint32 end = std::max(Uint32(1), game->stepCounter);
	for (int i = 0; i <= 4; ++i)
		surface->drawString(x + ew * i / 4, y + eh + 6, font,
							getTimeText(static_cast<Uint64>(end) * i / 100));
	int closest = std::numeric_limits<int>::max();
	Uint64 hover = 0;
	int hoverX = -1, hoverY = -1;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		if (!isTeamEnabled[t])
			continue;
		const auto &stats = game->teams[t]->stats;
		int prevX = -1, prevY = -1;
		for (const auto &m : stats.measurementHistory)
		{
			if (type - 6 >= 16 && m.tick <= stats.extendedCoverageStartTick &&
				!(stats.extendedCoverageStartTick == 0 && m.tick == 0))
				continue;
			const Uint64 value = TeamStats::graphValue(m, type - 6);
			const int px = static_cast<Uint64>(m.tick) * ew / end;
			const int py = eh - static_cast<long double>(value) * eh / maximum;
			if (prevX >= 0)
				surface->drawLine(x + prevX, y + prevY, x + px, y + py, game->teams[t]->color);
			else
				surface->drawCircle(x + px, y + py, 2, game->teams[t]->color);
			const int dist = std::abs(mouse_x - px) * 4096 + std::abs(mouse_y - py);
			if (mouse_x >= 0 && mouse_x < ew && mouse_y >= 0 && mouse_y < eh && dist < closest)
			{
				closest = dist;
				hover = value;
				hoverX = x + px;
				hoverY = y + py;
			}
			prevX = px;
			prevY = py;
		}
	}
	Uint32 earliest = end, latest = 0;
	bool missing = false, anySamples = false;
	for (int t = 0; t < game->mapHeader.getNumberOfTeams(); ++t)
	{
		if (!isTeamEnabled[t])
			continue;
		const auto &stats = game->teams[t]->stats;
		earliest = std::min(earliest, stats.coverageStartTick);
		latest = std::max(latest, stats.coverageStartTick);
		missing |= stats.coverageStartTick > 0;
		anySamples |= !stats.measurementHistory.empty();
	}
	if (missing || !anySamples)
	{
		std::string label = Toolkit::getStringTable()->getString(
			anySamples ? "[Stats since tick]" : "[Stats unavailable]");
		if (anySamples)
			label += " " + std::to_string(earliest) +
					 (latest != earliest ? " - " + std::to_string(latest) : "");
		surface->drawString(x + 5, y + 25, font, label);
	}
	if (hoverX >= 0)
	{
		surface->drawVertLine(hoverX, y, eh, InGameTouchTheme::border);
		surface->drawCircle(hoverX, hoverY, 5, Color::white);
		surface->drawString(hoverX + 5, hoverY + 5, font, std::to_string(hover));
	}
}
