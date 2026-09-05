// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AINull.h"
#include "Order.h"

std::shared_ptr<Order> AINull::getOrder(void)
{
	return std::shared_ptr<Order>(new NullOrder());
}
