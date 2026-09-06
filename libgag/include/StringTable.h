// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <vector>
#include <bitset>
#include <map>
#include <string>

namespace GAGCore
{
	class OneStringToken
	{
	public:
		std::vector<std::string> data;
	};
	
	class StringTable
	{
	public:
		StringTable();
		~StringTable();
		void setLang(int l) { actLang = l; }
		void setDefaultLang(int l) { defaultLang = l; }
		int getLang(void) { return actLang; }
		int getLangCode(const std::string & lang) { return languageCodes[lang]; }
		bool isLangComplete(int l) { return !incomplete[l]; }
		int getNumberOfLanguage(void) { return languageCount; }
		bool loadIncompleteList(const std::string filename);
		bool load(const std::string filename);
		const std::string getString(const std::string stringname) const;
		bool doesStringExist(const std::string stringname) const;
		const std::string getStringInLang(const std::string stringname, int lang) const;
		void print();
	
	private:
		std::vector<OneStringToken *> strings;
		std::map<std::string, size_t> stringAccess;
		std::map<std::string, int> languageCodes;
		int actLang;
		int defaultLang;
		int languageCount;
		std::vector<bool> incomplete;
		
	public:
		enum {AI_NAME_SIZE=4};
	};

}

