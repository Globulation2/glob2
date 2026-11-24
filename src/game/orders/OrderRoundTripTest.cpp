// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every order an engine creates must decode from its own bytes: the network
// codec, the replay writer and the turn engine all serialize an order and read
// it back with Order::getOrder. A chat message of 255 or more bytes once built
// a MessageOrder whose one-byte text length wrapped to 0, so it did not decode,
// and the replay writer's throw ended the local match that sent it.

#include "Glob2Test.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "BitArray.h"
#include "Brush.h"
#include "Order.h"
#include "Ressource.h"
#include "UnitConsts.h"
#include "Version.h"

namespace
{

std::vector<Uint8> wire(Order &order)
{
	std::vector<Uint8> bytes(1 + order.getDataLength());
	bytes[0] = order.getOrderType();
	if (order.getDataLength() > 0)
		std::copy_n(order.getData(), order.getDataLength(), bytes.data() + 1);
	return bytes;
}

// Decodes the order's bytes and checks that the copy encodes to the same bytes.
std::shared_ptr<Order> roundTrip(Order &order)
{
	const auto bytes = wire(order);
	auto copy = Order::getOrder(bytes.data(), static_cast<int>(bytes.size()), VERSION_MINOR);
	if (!copy)
		return copy;
	CHECK(copy->getOrderType() == order.getOrderType());
	CHECK(wire(*copy) == bytes);
	return copy;
}

Utilities::BitArray fullMask(int w, int h)
{
	Utilities::BitArray mask(w * h);
	for (int i = 0; i < w * h; ++i)
		mask.set(i, true);
	return mask;
}

std::string decodedText(MessageOrder &order)
{
	auto copy = std::dynamic_pointer_cast<MessageOrder>(roundTrip(order));
	REQUIRE(copy);
	return copy->getText();
}

} // namespace

TEST_SUITE("orders")
{
	TEST_CASE("chat messages up to the byte limit round-trip whole")
	{
		static_assert(ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES == 254);
		for (int bytes : {0, 1, 253, 254})
		{
			INFO(bytes << " text bytes");
			const std::string text(bytes, 'a');
			MessageOrder order(~0u, MessageOrder::NORMAL_MESSAGE_TYPE, text.c_str());
			CHECK(order.getDataLength() == 9 + bytes + 1);
			CHECK(decodedText(order) == text);
		}
	}

	TEST_CASE("longer chat messages are trimmed to the limit and still decode")
	{
		// 255 and 256 are the lengths that crashed matches; 1000 is far past
		// the old strmlen cap.
		for (int bytes : {255, 256, 257, 1000})
		{
			INFO(bytes << " text bytes");
			const std::string text(bytes, 'b');
			MessageOrder order(5, MessageOrder::NORMAL_MESSAGE_TYPE, text.c_str());
			CHECK(order.getDataLength() == 9 + ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES + 1);
			CHECK(decodedText(order) == text.substr(0, ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES));
		}
	}

	TEST_CASE("trimming a chat message never splits a UTF-8 character")
	{
		const std::string e = "\xc3\xa9";            // é, two bytes
		const std::string euro = "\xe2\x82\xac";     // €, three bytes
		const std::string smile = "\xf0\x9f\x98\x80"; // 😀, four bytes
		struct Case { std::string text; std::string kept; };
		const std::vector<Case> cases = {
			// A two-byte letter that fits exactly is kept.
			{std::string(252, 'x') + e, std::string(252, 'x') + e},
			// One that would straddle byte 254 is dropped whole.
			{std::string(253, 'x') + e, std::string(253, 'x')},
			{std::string(252, 'x') + euro, std::string(252, 'x')},
			{std::string(251, 'x') + euro + "y", std::string(251, 'x') + euro},
			{std::string(251, 'x') + smile, std::string(251, 'x')},
			{std::string(250, 'x') + smile + smile, std::string(250, 'x') + smile},
		};
		for (const auto &c : cases)
		{
			INFO(c.text.size() << " text bytes");
			MessageOrder order(1, MessageOrder::NORMAL_MESSAGE_TYPE, c.text.c_str());
			CHECK(decodedText(order) == c.kept);
		}
		MessageOrder none(1, MessageOrder::NORMAL_MESSAGE_TYPE, nullptr);
		CHECK(decodedText(none).empty());
	}

	TEST_CASE("a received chat order whose length byte wrapped is rejected")
	{
		// What a 255-byte message from an older build looks like on the wire:
		// still refused (peers drop it), never mistaken for a shorter text.
		std::vector<Uint8> bytes(1 + 9 + 256, 'c');
		bytes[0] = ORDER_TEXT_MESSAGE;
		std::fill_n(bytes.begin() + 1, 9, 0);
		bytes.back() = 0;
		CHECK_FALSE(Order::getOrder(bytes.data(), static_cast<int>(bytes.size()), VERSION_MINOR));
	}

	TEST_CASE("every order type decodes from its own bytes")
	{
		Sint32 ratio[NB_UNIT_TYPE] = {1, 2, 3};
		bool clearing[MaterialCount] = {};
		clearing[0] = true;
		const Uint8 voiceFrames[4] = {1, 2, 3, 4};
		std::vector<std::shared_ptr<Order>> orders = {
			std::make_shared<OrderCreate>(1, 10, 12, 3, 2, 4),
			std::make_shared<OrderCreate>(1, 10, 12, 3, 2, 4, 7),
			std::make_shared<OrderDelete>(513),
			std::make_shared<OrderCancelDelete>(513),
			std::make_shared<OrderConstruction>(513, 3, 5),
			std::make_shared<OrderCancelConstruction>(513, 3),
			std::make_shared<OrderChangePriority>(513, 1),
			std::make_shared<OrderModifyBuilding>(513, 6),
			std::make_shared<OrderModifyExchange>(513, 0x3, 0x5),
			std::make_shared<OrderModifySwarm>(513, ratio),
			std::make_shared<OrderModifyFlag>(513, 9),
			std::make_shared<OrderModifyClearingFlag>(513, clearing),
			std::make_shared<OrderModifyMinLevelToFlag>(513, 2, 1),
			std::make_shared<OrderMoveFlag>(513, 20, 21, true),
			std::make_shared<OrderAlterForbidden>(1, BrushTool::MODE_ADD, 30, 30, 4, 3, fullMask(4, 3)),
			std::make_shared<OrderAlterGuardArea>(1, BrushTool::MODE_DEL, 30, 30, 2, 2, fullMask(2, 2)),
			std::make_shared<OrderAlterClearArea>(1, BrushTool::MODE_ADD, 30, 30, 5, 1, fullMask(5, 1)),
			std::make_shared<OrderAlterFarmArea>(1, BrushTool::MODE_ADD, 30, 30, 1, 5, fullMask(1, 5)),
			std::make_shared<MessageOrder>(~0u, MessageOrder::NORMAL_MESSAGE_TYPE, "gl hf"),
			std::make_shared<MessageOrder>(~0u, MessageOrder::PRIVATE_MESSAGE_TYPE,
										   std::string(ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES + 2, 'z').c_str()),
			std::make_shared<OrderVoiceData>(~0u, sizeof voiceFrames, 2, voiceFrames),
			std::make_shared<SetAllianceOrder>(1, 0x3, 0xc, 0x1, 0x2, 0x4),
			std::make_shared<MapMarkOrder>(1, 40, 41),
			std::make_shared<PauseGameOrder>(true),
			std::make_shared<PlayerQuitsGameOrder>(2),
			std::make_shared<AdjustLatency>(3),
		};
		std::vector<Uint8> covered;
		for (auto &order : orders)
		{
			INFO("order type " << int(order->getOrderType()));
			CHECK(roundTrip(*order));
			covered.push_back(order->getOrderType());
		}
		// A new order type must be added here too.
		for (Uint8 type : {ORDER_CREATE, ORDER_DELETE, ORDER_CANCEL_DELETE, ORDER_CONSTRUCTION,
						   ORDER_CANCEL_CONSTRUCTION, ORDER_MODIFY_BUILDING, ORDER_MODIFY_EXCHANGE,
						   ORDER_MODIFY_SWARM, ORDER_MODIFY_FLAG, ORDER_MODIFY_CLEARING_FLAG,
						   ORDER_MODIFY_MIN_LEVEL_TO_FLAG, ORDER_MOVE_FLAG, ORDER_CHANGE_PRIORITY,
						   ORDER_ALTER_FORBIDDEN, ORDER_ALTER_GUARD_AREA, ORDER_ALTER_CLEAR_AREA,
						   ORDER_ALTER_FARM_AREA, ORDER_TEXT_MESSAGE, ORDER_VOICE_DATA, ORDER_SET_ALLIANCE,
						   ORDER_MAP_MARK, ORDER_PAUSE_GAME, ORDER_PLAYER_QUIT_GAME, ORDER_ADJUST_LATENCY})
		{
			INFO("order type " << int(type));
			CHECK(std::find(covered.begin(), covered.end(), type) != covered.end());
		}
	}
}
