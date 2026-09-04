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
						lastWord.clear();
					}
					else
					{
						if(actWordLength+spaceLength >= length)
						{
							for(unsigned c=0; c<lastWord.size(); ++c)
							{
								lastLine += lastWord[c];
								int actLineLength = getStringWidth(lastLine);
								if (actLineLength >= length)
								{
									if (sprite && frames.size() > line)
									{	
										lines_frames.push_back(frames[line]);
									}	
									lines.push_back(pos-lastWord.size()+c);
									lastLine.clear();
									show_image.push_back(false);
								}
							}
							lastWord.clear();
						}
						else
						{
							if (sprite && frames.size() > line)
							{
								lines_frames.push_back(frames[line]);
							}
							lines.push_back(pos-lastWord.size());
							show_image.push_back(false);
							lastLine = lastWord;
							lastWord.clear();
						}
					}
				}
				break;
				
				case '\n':
				case '\r':
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					if (actWordLength+actLineLength+spaceLength >= length)
					{
						if(actWordLength+spaceLength >= length)
						{
							for(unsigned c=0; c<lastWord.size(); ++c)
							{
								lastLine += lastWord[c];
								int actLineLength = getStringWidth(lastLine);
								if (actLineLength >= length)
								{
									if (sprite && frames.size() > line)
									{	
										lines_frames.push_back(frames[line]);
									}	
									lines.push_back(pos-lastWord.size()+c);
									lastLine.clear();
									show_image.push_back(false);
								}
							}
						}
						else
						{
							if (sprite && frames.size() > line)
							{	
								lines_frames.push_back(frames[line]);
							}	
							lines.push_back(pos-lastWord.size());
							lastLine.clear();
							show_image.push_back(false);
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
			if (sprite && frames.size() > line)
			{
				lines_frames.push_back(frames[line]);
			}
			lines.push_back(pos-lastWord.size());
		}
		
		if (cursorPosY >= lines.size())
			cursorPosY = lines.size()-1;
	}
}
