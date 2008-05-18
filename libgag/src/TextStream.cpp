/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <TextStream.h>
#include <assert.h>
#include <fstream>
#include <iostream>
#include <stack>
#ifdef WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

namespace GAGCore
{
	void TextOutputStream::printLevel(void)
	{
		for (unsigned i=0; i<level; i++)
			backend->putc('\t');
	}
	
	void TextOutputStream::printString(const char *string)
	{
		assert(string);
		backend->write(string, strlen(string));
	}
	
	void TextOutputStream::write(const void *data, const size_t size, const char *name)
	{
		printLevel();
		if (name)
		{
			printString(name);
			printString(" = ");
		}
		const unsigned char *dataChars = static_cast<const unsigned char *>(data);
		for (size_t i=0; i<size; i++)
		{
			char buffer[3];
			snprintf(buffer, sizeof(buffer), "%02x", dataChars[i]);
			printString(buffer);
		}
		printString(";\n");
	}
	
	void TextOutputStream::writeText(const std::string &v, const char *name)
	{
		printLevel();
		if (name)
		{
			printString(name);
			printString(" = \"");
		}
		for (size_t i = 0; i<v.size(); i++)
			if (v[i] == '\"')
				printString("\\\"");
			else
				backend->putc(v[i]);
		printString("\";\n");
	}
	
	void TextOutputStream::writeEnterSection(const char *name)
	{
		printLevel();
		printString(name);
		printString("\n");
		printLevel();
		printString("{\n");
		level++;
	}
	
	void TextOutputStream::writeEnterSection(unsigned id)
	{
		printLevel();
		print(id);
		printString("\n");
		printLevel();
		printString("{\n");
		level++;
	}
	
	void TextOutputStream::writeLeaveSection(size_t count)
	{
		level -= count;
		printLevel();
		printString("}\n");
	}
	
	// Parser for TextInputStream
	
	struct Parser
	{
		// parser
		struct Token
		{
			enum Type
			{
				END_OF_INPUT = 0,
				OPEN_BRACKET,
				CLOSE_BRACKET,
				EQUAL,
				SEMI_COLON,
				COLON,
				VAL,
				NONE
			} type;
			
			std::string val;
			
			Token(Type type, const std::string &val = "")
			{
				this->type = type;
				this->val = val;
			}
		};
		
		int next;
		StreamBackend *stream;
		Token token;
		size_t line, column;
		
		Parser(StreamBackend *stream)
		 : token(Token::NONE)
		{
			assert(stream);
			
			line = 1;
			column = 0;
			this->stream = stream;
			
			if (!stream->isEndOfStream())
				nextChar();
		}
		
		int nextChar(void)
		{
			next = stream->getc();
			if 	(next == '\n')
			{
				column = 0;
				line++;
			}
			else
				column++;
			return next;
		}
		
		Token nextToken(void)
		{
			if (!stream->isEndOfStream())
			{
				switch (next)
				{
					case '/':
					nextChar();
					if (next == '/')
					{
						// single line comment
						while ((next != '\n') && (next != '\r') && !stream->isEndOfStream())
							nextChar();
						while (((next == '\n') || (next == '\r')) && !stream->isEndOfStream())
							nextChar();
					}
					else if (next == '*')
					{
						// multi line comment
						nextChar();
						while ((next != '*' || (nextChar(), next != '/')) && !stream->isEndOfStream())
							nextChar();
						nextChar();
						
						/* More human readable version.
						nextChar();
						while (!ifs.eof())
						{
							while ((next != '*') && !ifs.eof())
								nextChar();
							nextChar();
							if (next == '/')
							{
								nextChar();
								break;
							}
						}*/
					}
					else
					{
						std::cerr << "TextStream::lexer : error @ " << line << ':' << column << " : / or * expected" << std::endl;
					}
					token = nextToken();
					break;
					
					case '{':
					nextChar();
					token = Token(Token::OPEN_BRACKET);
					break;
					
					case '}':
					nextChar();
					token = Token(Token::CLOSE_BRACKET);
					break;
					
					case '=':
					nextChar();
					token = Token(Token::EQUAL);
					break;
					
					case ';':
					nextChar();
					token = Token(Token::SEMI_COLON);
					break;
					
					case ':':
					nextChar();
					token = Token(Token::COLON);
					break;
					
					case '"':
					{
						std::string tempValue;
						while ((nextChar() != '"') && !stream->isEndOfStream())
						{
							tempValue += static_cast<std::string::value_type>(next);
						}
						nextChar();
						token = Token(Token::VAL, tempValue);
					}
					break;
					
					case ' ':
					case '\t':
					case '\n':
					case '\r':
					while (next==' ' || next=='\t' || next=='\r' || next=='\n') //std::isspace(next))
						nextChar();
					token = nextToken();
					break;
					
					default:
					{
						if (isalnum(next) || (next == '.') || (next == '-') || (next == '[') || (next == ']'))
						{
							std::string tempValue;
							do
							{
								tempValue += static_cast<std::string::value_type>(next);
								nextChar();
							}
							while (isalnum(next) || (next == '.') || (next == '[') || (next==']'));
							token = Token(Token::VAL, tempValue);
						}
						else
						{
							std::cerr << "TextStream::lexer : error @ " << line << ':' << column << " : character or number or . expected" << std::endl;
							token = nextToken();
						}
					}
					break;
				}
			}
			else
			{
				token = Token(Token::END_OF_INPUT);
			}
			return token;
		}
		
		//! Macro that return false and an error message if parser encountered EOF
		#define CHECK_NOT_EOF \
		if (token.type == Token::END_OF_INPUT) \
		{ \
			 std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : unexpected EOF" << std::endl; \
			 return false; \
		}
		
		//! Macro that return false and an error message if anything else then a value is encountered
		#define CHECK_VAL \
		if (token.type != Token::VAL) \
		{ \
			std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : value expected" << std::endl; \
			return false; \
		}
		
		//! Macro that return false and an error message if anything else then a ; is encountered
		#define CHECK_SEMICOLON \
		if (token.type != Token::SEMI_COLON) \
		{ \
			std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : ; (semicolon) expected" << std::endl; \
			return false; \
		}
		
		//! Macro that return false and an error message if anything else then a { is encountered
		#define CHECK_OPEN_BRACKET \
		if (token.type != Token::OPEN_BRACKET) \
		{ \
			std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : ; { expected" << std::endl; \
			return false; \
		}
		
		bool parse(std::map<std::string, std::string> *table)
		{
			if (stream->isEndOfStream())
				return false;
				
			std::vector<std::string> levels;
			std::string id;
			std::string fullId;
			std::stack<unsigned> autovectors; // stack of unsigned used for implicit vectors. If value is 0, the level is not considered using autovector
			
			assert(table);
			
			nextToken();
			while (token.type != Token::END_OF_INPUT)
			{
				CHECK_NOT_EOF
				if (token.type == Token::CLOSE_BRACKET)
				{
					if (levels.size())
					{
						// when using autovector, add a count member
						if (autovectors.top() > 0)
						{
							std::ostringstream oss;
							oss << autovectors.top();
							(*table)[fullId + ".count"] = oss.str();
						}
						
						autovectors.pop();
						levels.pop_back();
						fullId.clear();
						
						std::vector<std::string>::size_type size = levels.size();
						std::vector<std::string>::size_type j = 0;
						if (size != 0)
						{
							for (std::vector<std::string>::const_iterator i = levels.begin(); i != levels.end(); ++i)
							{
								fullId += *i;
								if (++j < size)
									fullId += ".";
							}
						}
					}
					else
					{
						std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : bracket closed but none was opened" << std::endl;
					}
					nextToken();
				}
				else
				{
					if (token.type == Token::OPEN_BRACKET)
					{
						// autovector entry
						std::ostringstream oss;
						oss << autovectors.top()++;
						id = oss.str();
					}
					else
					{
						// normal entry, have an id
						CHECK_VAL
						id = token.val;
						
						nextToken();
						CHECK_NOT_EOF
					}
					
					if (token.type == Token::EQUAL)
					{
						nextToken();
						CHECK_NOT_EOF
						CHECK_VAL
						std::string val = token.val;
						
						nextToken();
						CHECK_NOT_EOF
						CHECK_SEMICOLON
						
						std::string path = fullId;
						if (fullId.length())
							path += ".";
						path += id;
						
						(*table)[path] = val;
						nextToken();
					}
					else if (token.type == Token::OPEN_BRACKET)
					{
						// build new path
						if (fullId.length())
							fullId += ".";
						fullId += id;
						levels.push_back(id);
						autovectors.push(0);
						nextToken();
						CHECK_NOT_EOF
					}
					else if (token.type == Token::COLON)
					{
						nextToken();
						CHECK_NOT_EOF
						CHECK_VAL
						std::string copyPathSource = token.val;
						
						nextToken();
						CHECK_NOT_EOF
						CHECK_OPEN_BRACKET
						
						// build new path
						if (fullId.length())
							fullId += ".";
						fullId += id;
						levels.push_back(id);
						autovectors.push(0);
						
						// copy subkeys to actual
						size_t len = copyPathSource.length();
						for (std::map<std::string, std::string>::iterator i = table->begin(); i != table->end(); i++)
						{
							if (i->first.find(copyPathSource) == 0)
							{
								std::string copyPathSub = i->first.substr(len, i->first.length() - len);
								(*table)[fullId + copyPathSub] = i->second;
							}
						}
						
						nextToken();
						CHECK_NOT_EOF
					}
					else
					{
						std::cerr << "TextStream::parser : error @ " << line << ':' << column << " : = or { expected" << std::endl;
						return false; 
					}
				}
			}
			
			if (levels.size())
			{
				std::cerr << "TextStream::parser : error @ " << line << ':' << column << "End of file called while all } are not closed" << std::endl;
				return false;
			}
			
			return true;
		}
	};
	
	TextInputStream::TextInputStream(StreamBackend *backend)
	{
		assert(backend);
		Parser p(backend);
		p.parse(&table);
		/*for (std::map<std::string, std::string>::iterator i = table.begin(); i != table.end(); ++i)
			std::cout << i->first << " = " << i->second << std::endl;*/
	}
	
	void TextInputStream::readEnterSection(const char *name)
	{
		if (levels.size() > 0)
			key += ".";
		levels.push_back(std::string(name));
		key += name;
	}
	
	void TextInputStream::readEnterSection(unsigned id)
	{
		std::ostringstream oss;
		oss << id;
		levels.push_back(oss.str());
		key += ".";
		key += oss.str();
	}
	
	void TextInputStream::readLeaveSection(size_t count)
	{
		assert(count <= levels.size());
		
		levels.resize(levels.size() - count);
		key.clear();
		size_t size = levels.size();
		if (size != 0)
		{
			for (size_t i = 0; i < size; ++i)
			{
				key += levels[i];
				if (i+1 < size)
					key += ".";
			}
		}
	}
	
	void TextInputStream::readFromTableToString(const char *name, std::string *result)
	{
		assert(result);
		
		std::string fullKey;
		if (levels.size() > 0)
		{
			fullKey = key;
			fullKey += ".";
		}
		fullKey += name;
		
		const std::map<std::string, std::string>::const_iterator it = table.find(fullKey);
		if (it != table.end())
		{
			*result = it->second;
		}
		else
		{
			std::cerr << "TextInputStream::readFromTableToString : no entry named " << fullKey << std::endl;
			*result = "";
		}
	}
	
	void TextInputStream::read(void *data, size_t size, const char *name)
	{
		std::string s;
		readFromTableToString(name, &s);
		size_t serializedLength = s.length() >> 1;
		if (serializedLength != size)
			std::cerr << "TextInputStream::read : requested length " << size << " differs from serialized size " << serializedLength << std::endl;
		size_t toReadLength = std::min(size, serializedLength);
		char *destBuffer = static_cast<char *>(data);
		for (size_t i=0; i<toReadLength; i++)
		{
			char buffer[3];
			unsigned val;
			buffer[0] = s[i*2];
			buffer[1] = s[(i*2)+1];
			buffer[2] = 0;
			sscanf(buffer, "%02x", &val);
			destBuffer[i] = val;
		}
	}
	
	void TextInputStream::getSubSections(const std::string &root, std::set<std::string> *sections)
	{
		size_t rootLength = root.length();
		for (std::map<std::string, std::string>::const_iterator it = table.begin(); it != table.end(); ++it)
		{
			const std::string &key = it->first;
			if ((rootLength == 0) || (key.find(root) == 0))
			{
				// root is correct, find non empty subsections
				std::string::size_type end = key.find('.', rootLength);
				if (end != std::string::npos)
					sections->insert(key.substr(rootLength, end - rootLength));
			}
		}
	}
}
