// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Game.h"
#include "Team.h"

// Presentation only: do not change the simulation's recorded condition. An
// allied winner can inherit its partner's result, and the second evaluation
// pass can relabel a score decision as opponents defeated.
inline const char* outcomeReasonKey(const Game& game, int teamNumber)
{
    const auto& team = *game.teams[teamNumber];
    const auto key = [](WinningConditionType condition) {
        switch (condition) {
        case WCDeath: return "[outcome reason eliminated]";
        case WCAllies: return "[outcome reason ally won]";
        case WCPrestige: return "[outcome reason prestige]";
        case WCScript: return "[outcome reason scenario]";
        case WCOpponentsDefeated: return "[outcome reason opponents defeated]";
        case WCSuddenDeath: return "[outcome reason timer]";
        case WCWinProbability: return "[outcome reason probability]";
        default: return "";
        }
    };
    if (!team.hasWon && !team.hasLost)
        return "";
    if (!team.hasWon || (team.winCondition != WCAllies && team.winCondition != WCOpponentsDefeated))
        return key(team.winCondition);

    bool scoringOpponentLost = false;
    bool probabilityDecision = false;
    for (int t = 0; t < game.teamsCount(); ++t) {
        const auto& other = *game.teams[t];
        if (t == teamNumber || ((team.allies & other.me) && (other.allies & team.me))) continue;
        if (other.isAlive && other.hasLost && (other.winCondition == WCPrestige ||
            other.winCondition == WCSuddenDeath || other.winCondition == WCWinProbability)) {
            scoringOpponentLost = true;
            probabilityDecision |= other.winCondition == WCWinProbability;
        }
    }
    for (const auto& condition : game.gameHeader.getWinningConditions()) {
        const auto type = condition->getType();
        if (type == WCAllies) continue;
        // Score/probability losses can make all opponents count as
        // defeated after the decision. That is a consequence, not its reason.
        if (type == WCOpponentsDefeated && scoringOpponentLost) continue;
        // Probability evaluates only on sample ticks. Keep its explanation
        // when the UI receives a later tick with the decided outcomes intact.
        if ((type == WCWinProbability && probabilityDecision) || condition->hasTeamWon(teamNumber, &game))
            return key(type);
    }
    return key(WCAllies);
}
