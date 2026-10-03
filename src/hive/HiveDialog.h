// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "HiveClient.h"
namespace Hive
{
class Dialog : public Glob2UI::InGameDialog
{
	std::shared_ptr<Client> client;
	std::string draft;
	bool ongoing = false;

  public:
	explicit Dialog(std::shared_ptr<Client> c) : client(std::move(c)) {}
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;

  protected:
	void onEscape() override { finish(0); }
	double maxWidth() const override { return 560; }
};
} // namespace Hive
