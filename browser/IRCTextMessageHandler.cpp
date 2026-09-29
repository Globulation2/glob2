// SPDX-License-Identifier: GPL-3.0-or-later
// The browser lobby uses YOG chat. The optional native TCP IRC bridge is not
// available through the fixed-backend WebSocket gateway.
#include "IRCTextMessageHandler.h"

IRCTextMessageHandler::IRCTextMessageHandler() : irc(incoming, incomingMutex), userListModified(false) {}
IRCTextMessageHandler::~IRCTextMessageHandler() = default;
void IRCTextMessageHandler::startIRC(const std::string&) {}
void IRCTextMessageHandler::stopIRC() {}
void IRCTextMessageHandler::update() {}
void IRCTextMessageHandler::sendCommand(const std::string&) {}
bool IRCTextMessageHandler::hasUserListBeenModified() { return false; }
std::vector<std::string>& IRCTextMessageHandler::getUsers() { return users; }
void IRCTextMessageHandler::addTextMessageListener(IRCTextMessageListener* listener) { listeners.add(listener); }
void IRCTextMessageHandler::removeTextMessageListener(IRCTextMessageListener* listener) { listeners.remove(listener); }
void IRCTextMessageHandler::sendToAllListeners(const std::string& message) {
    listeners.notify(&IRCTextMessageListener::handleIRCTextMessage, message);
}
