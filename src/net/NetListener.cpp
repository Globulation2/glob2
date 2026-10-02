// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetListener.h"
void NetListener::startListening(const NetListenConfig &config)
{
	listener = makeNetTransportListener(config);
}
void NetListener::stopListening()
{
	if (listener)
		listener->close();
}
bool NetListener::isListening() const
{
	return listener && listener->listening();
}
bool NetListener::attemptConnection(NetConnection &connection)
{
	if (!listener)
		return false;
	auto transport = listener->accept();
	if (!transport)
		return false;
	connection.closeConnection();
	connection.address = transport->peerAddress();
	connection.transport = std::move(transport);
	return true;
}
