// SPDX-License-Identifier: GPL-3.0-or-later

#include <GUITextArea.h>
#include <assert.h>

namespace GAGGUI
{
	void TextArea::layout(void)
	{
		int x, y, w, h;
		unsigned line = 0;
		getScreenPos(&x, &y, &w, &h);
		
		unsigned pos = 0;
		int length = w-4-getStringWidth("W")-spriteWidth;
		
		lines.clear();
		lines.push_back(0);
		show_image.clear();
		show_image.push_back(true);
		lines_frames.clear();
		std::string lastWord;
		std::string lastLine;
		int spaceLength = getStringWidth(" ");
		
		auto pushFrame = [&]()
		{
			if (sprite && frames.size() > line)
				lines_frames.push_back(frames[line]);
		};
		// start a wrapped line at start, which never shows an image
		auto wrapAt = [&](size_t start)
		{
			pushFrame();
			lines.push_back(start);
			show_image.push_back(false);
		};
		// a word wider than the area is broken wherever it overflows
		auto breakLongWord = [&]()
		{
			if (!lastLine.empty())
				lastLine += " ";
			for (unsigned c=0; c<lastWord.size();)
			{
				unsigned next = c+1;
				while (next < lastWord.size() && (static_cast<unsigned char>(lastWord[next]) & 0xC0) == 0x80)
					++next;
				const std::string character = lastWord.substr(c, next-c);
				if (!lastLine.empty() && getStringWidth(lastLine + character) > length)
				{
					wrapAt(pos-lastWord.size()+c);
					lastLine.clear();
				}
				lastLine += character;
				c = next;
			}
		};
		
		while (pos<text.length())
		{
			switch (text[pos])
			{
				case ' ':
				case '\t':
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					if (actWordLength+actLineLength+spaceLength < length)
					{
						if (lastLine.length())
							lastLine += " ";
						lastLine += lastWord;
					}
					else if (actWordLength+spaceLength >= length)
					{
						breakLongWord();
					}
					else
					{
						wrapAt(pos-lastWord.size());
						lastLine = lastWord;
					}
					lastWord.clear();
				}
				break;
				
				case '\n':
				case '\r':
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					if (!lastWord.empty() && actWordLength+actLineLength+spaceLength >= length)
					{
						if (actWordLength+spaceLength >= length)
						{
							breakLongWord();
						}
						else
						{
							wrapAt(pos-lastWord.size());
							lastLine.clear();
						}
					}
					if (sprite && frames.size() > line)
					{
						lines_frames.push_back(frames[line]);
						line++;
					}
					lines.push_back(pos+1);
					lastWord.clear();
					lastLine.clear();
					show_image.push_back(true);
				}
				break;
				
				default:
				{
					lastWord += text[pos];
				}
			}
			pos++;
		}
		
		int actLineLength = getStringWidth(lastLine);
		int actWordLength = getStringWidth(lastWord);
		if (actWordLength+actLineLength+spaceLength >= length)
		{
			if (actWordLength+spaceLength >= length)
				breakLongWord();
			else if (!lastWord.empty())
				wrapAt(pos-lastWord.size());
		}
		
		if (cursorPosY >= lines.size())
			cursorPosY = lines.size()-1;
	}
}
