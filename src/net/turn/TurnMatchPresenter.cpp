// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TurnMatchPresenter.h"

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include <algorithm>

#include "ConnectionQuality.h"
#include "Game.h"
#include "GameHeader.h"
#include "Player.h"
#include "Team.h"
#include "TurnSession.h"

using namespace GAGCore;

ConnectionSnapshot TurnMatchPresenter::snapshot(const Turn::TurnSession& s, const Game& game, Uint64 now)
{
    ConnectionSnapshot snapshot;
    const Uint64 period = std::max<Uint64>(1, s.tickPeriodMicros());
    // The quantities of docs/multiplayer/connection-quality.md: Delay (own), and Ping
    // and Behind for every human. Ping is the relay's measurement of each seat
    // (SeatLatency, protocol 2) so every row compares like with like; the own row falls
    // back to this client's Ping/Pong round trip (LAN hosts and protocol-1 relays
    // measure none).
    const int ownDelay = int((std::max<std::int64_t>(0, s.rttMicros()) / 2 + Uint64(s.targetTicks()) * period) / 1000);
    snapshot.inputDelayMs = ownDelay;
    snapshot.jitterMs = int(std::max<std::int64_t>(0, s.jitterMicros()) / 1000);
    snapshot.ownUnstable = s.jitterMicros() > std::int64_t(ConnectionQuality::UNSTABLE_JITTER_MS) * 1000;
    // Without a link there is no delay to state; the card says what is going on.
    if (s.linkStalled() || s.state() == Turn::TurnSession::State::Reconnecting)
        snapshot.inputDelayMs = -1;
    const GameHeader& header = game.gameHeader;
    for (int p = 0; p < header.getNumberOfPlayers() && p < int(Turn::MAX_SEATS); ++p)
    {
        const BasePlayer& player = header.getBasePlayer(p);
        // Closed seats are no players at all. AI `none` (the empty seat of a match
        // from before closed seats) controls nothing. A person who quit becomes AI
        // `none` too, but stays listed as Left so everyone sees who left.
        const bool humanSeat = s.humanSeatMask() & (1u << p);
        if (player.type == BasePlayer::P_NONE || (player.type == BasePlayer::P_AI && !humanSeat))
            continue;
        ConnectionRow row;
        row.seat = p;
        row.name = game.players[p] ? game.players[p]->name : player.name;
        if (player.teamNumber >= 0 && player.teamNumber < game.mapHeader.getNumberOfTeams() && game.teams[player.teamNumber])
            row.color = game.teams[player.teamNumber]->color;
        if (!humanSeat)
        {
            row.state = ConnectionRow::State::AI;
            snapshot.rows.push_back(row);
            continue;
        }
        if (player.type == BasePlayer::P_AI && p != s.localSeat())
        {
            // Their quit order has executed: they left, whatever presence says.
            row.state = ConnectionRow::State::Left;
            snapshot.rows.push_back(row);
            continue;
        }
        if (p == s.localSeat())
        {
            row.local = true;
            const auto own = s.seatPresenceInfo(p);
            row.pingMs = own.relayRttMicros ? int(own.relayRttMicros / 1000)
                         : s.rttMicros() > 0 ? int(s.rttMicros() / 1000)
                                             : -1;
            // A stalled link (nothing from the relay for a while) is our own
            // connection trouble even before the socket notices; its last Ping
            // is stale.
            const bool lost = s.linkStalled() || s.state() == Turn::TurnSession::State::Reconnecting;
            row.state = lost ? ConnectionRow::State::Reconnecting
                        : s.state() == Turn::TurnSession::State::Running ? ConnectionRow::State::Connected
                                                                         : ConnectionRow::State::Waiting;
            if (lost)
                row.pingMs = -1;
            snapshot.rows.push_back(row);
            continue;
        }
        const auto info = s.seatPresenceInfo(p);
        row.pingMs = info.relayRttMicros ? int(info.relayRttMicros / 1000) : -1;
        row.behindMs = int(Uint64(info.lagTicks) * period / 1000);
        switch (info.state)
        {
        case Turn::PresenceState::Connected: row.state = ConnectionRow::State::Connected; break;
        case Turn::PresenceState::Lagging: row.state = ConnectionRow::State::Slow; break;
        case Turn::PresenceState::Reconnecting: row.state = ConnectionRow::State::Reconnecting; break;
        case Turn::PresenceState::Resyncing: row.state = ConnectionRow::State::Resyncing; break;
        case Turn::PresenceState::Left: row.state = ConnectionRow::State::Left; break;
        default: row.state = ConnectionRow::State::Waiting; break;
        }
        if (row.state == ConnectionRow::State::Reconnecting)
        {
            const Uint64 grace = Uint64(info.graceRemainingTicks) * period;
            const Uint64 since = now > info.receivedMicros ? now - info.receivedMicros : 0;
            row.graceSeconds = int((grace > since ? grace - since : 0) / 1000000);
        }
        snapshot.rows.push_back(row);
    }
    // Centre cards: this client waits on the network.
    using State = Turn::TurnSession::State;
    if (s.state() == State::Reconnecting || s.linkStalled())
    {
        snapshot.card = ConnectionSnapshot::Card::Reconnecting;
        snapshot.attempt = std::max(1, s.reconnectAttempts());
        // The loss began when the relay went quiet, which a stall reports before
        // the socket does; a stall turning into a reconnect keeps counting.
        if (!connectionLostMicros)
            connectionLostMicros = now - std::min<Uint64>(now, s.stalledForMicros());
        if (s.graceTicks())
        {
            const Uint64 grace = Uint64(s.graceTicks()) * period;
            const Uint64 lost = now - connectionLostMicros;
            snapshot.graceSeconds = int((grace > lost ? grace - lost : 0) / 1000000);
        }
        return snapshot;
    }
    connectionLostMicros = 0;
    const bool desync = s.rejoiningAfterDesync();
    const std::uint32_t behind = s.maxHorizon() > s.executedTick() ? s.maxHorizon() - s.executedTick() : 0;
    // Small lag closes by itself; the card shows only a real fast-forward.
    const bool farBehind = Uint64(behind) * period > Uint64(ConnectionQuality::catchUpCardMs()) * 1000;
    if (s.state() == State::Running && (s.needsReload() || desync || (s.catchingUp() && farBehind)))
    {
        if (!catchupActive || s.needsReload())
        {
            catchupActive = true;
            catchupFrom = s.needsReload() ? 0 : s.executedTick();
            catchupStartedMicros = now;
        }
        snapshot.card = desync ? ConnectionSnapshot::Card::Desync : ConnectionSnapshot::Card::CatchingUp;
        snapshot.catchupDone = s.executedTick() - std::min(s.executedTick(), catchupFrom);
        snapshot.catchupTotal = std::max(snapshot.catchupDone, s.maxHorizon() - std::min(s.maxHorizon(), catchupFrom));
        snapshot.missedSeconds = int(Uint64(snapshot.catchupTotal) * period / 1000000);
        // The gap closes at (replay rate - match rate): estimate from that, and say
        // when it does not close at all.
        catchupPace.sample(now, s.executedTick(), s.maxHorizon());
        snapshot.secondsLeft = catchupPace.secondsLeft(behind, 1e6 / double(period));
        snapshot.cannotKeepUp = !desync && catchupPace.stuck(now);
        return snapshot;
    }
    catchupPace.reset();
    catchupActive = false;
    catchupFrom = 0;
    catchupStartedMicros = 0;
    return snapshot;
}

// The text form of the connection state (LAN harness logs; the HUD uses the snapshot).
std::vector<std::string> TurnMatchPresenter::notice(const Turn::TurnSession& s, const Game& game)
{
    std::vector<std::string> lines;
    auto& strings = *Toolkit::getStringTable();
    using State = Turn::TurnSession::State;
    // Before the first Welcome the relay is waiting for every player to load.
    const bool welcomed = s.horizon() > 0 || s.executedTick() > 0;
    if (s.state() == State::Reconnecting || s.linkStalled())
        lines.push_back(strings.getString("[turn reconnecting]"));
    else if (s.state() == State::Connecting || s.state() == State::AwaitingWelcome)
        lines.push_back(strings.getString(welcomed ? "[turn reconnecting]" : "[turn waiting for players]"));
    else if (s.state() == State::Running)
    {
        if (s.needsReload())
            lines.push_back(strings.getString("[turn rejoining]"));
        else if (s.catchingUp() &&
                 Uint64(s.bufferedTicks()) * std::max<Uint64>(1, s.tickPeriodMicros()) > Uint64(ConnectionQuality::catchUpLineMs()) * 1000)
            lines.push_back(GAGCore::FormattableString(strings.getString("[turn catching up %0]")).arg(s.bufferedTicks()));
        if (s.tooManyActions())
            lines.push_back(strings.getString("[turn too many actions]"));
    }
    for (int p = 0; p < game.gameHeader.getNumberOfPlayers() && p < Turn::MAX_SEATS; ++p)
    {
        if (p == s.localSeat() || !(s.humanSeatMask() & (1u << p)) || !game.players[p])
            continue;
        const char* key = nullptr;
        switch (s.presence(p))
        {
        case Turn::PresenceState::Reconnecting: key = "[turn player reconnecting %0]"; break;
        case Turn::PresenceState::Lagging: key = "[turn player lagging %0]"; break;
        case Turn::PresenceState::Resyncing: key = "[turn player resyncing %0]"; break;
        case Turn::PresenceState::NotConnected:
            if (welcomed)
                key = "[turn player not connected %0]";
            break;
        default: break;
        }
        if (key)
            lines.push_back(GAGCore::FormattableString(strings.getString(key)).arg(game.players[p]->name));
    }
    return lines;
}
