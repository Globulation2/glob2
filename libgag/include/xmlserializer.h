/*
    DPS - Dynamic Parallel Schedules
    Copyright (C) 2000-2003 Sebastian Gerlach (EPFL-IC-LSP)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
	XML Serializer

	The DPS serializer can be used to serialize complex objects to many formats.
	These classes provide an XML back-end for the serializer. It should not be
	considered perfect industrial strength code, in particular the simple XML
	parser should be replaced by something more solid in production code. It is
	kept simple here in order to limit dependencies on external code.
*/

#ifndef INCLUDED_XMLSERIALIZER_H
#define INCLUDED_XMLSERIALIZER_H

// The XML serializer requires rich naming
#define DPS_NAMED_SERIALIZATION

#include <dps/base.h>
#include "Toolkit.h"
#include "FileManager.h"

namespace base
{
	//! Text output stream
	template<typename Writer> class TextOutputStream : public OutputStream
	{
		//! Target writer
		Writer *writer;
	public:
		//! Constructor
		TextOutputStream(Writer *w)
		{
			writer=w;
		}

		virtual void writeInt8(const Int8& item)     { char t[8]; sprintf(t,"%d",item); writer->write(t); }
		virtual void writeUInt8(const UInt8& item)   { char t[8]; sprintf(t,"%u",item); writer->write(t); }
		virtual void writeInt16(const Int16& item)   { char t[8]; sprintf(t,"%d",item); writer->write(t); }
		virtual void writeUInt16(const UInt16& item) { char t[8]; sprintf(t,"%u",item); writer->write(t); }
		virtual void writeInt32(const Int32& item)   { char t[12]; sprintf(t,"%d",item); writer->write(t); }
		virtual void writeUInt32(const UInt32& item) { char t[12]; sprintf(t,"%u",item); writer->write(t); }
		virtual void writeInt64(const Int64& item)   { char t[24]; sprintf(t,I64F,item); writer->write(t); }
		virtual void writeUInt64(const UInt64& item) { char t[24]; sprintf(t,U64F,item); writer->write(t); }
		virtual void writeFloat(const Float& item)   { char t[32]; sprintf(t,"%g",item); writer->write(t); }
		virtual void writeDouble(const Double& item) { char t[32]; sprintf(t,"%lg",item); writer->write(t); }
		virtual void writeBool(const Bool& item) { writer->write(item ? "true" : "false"); }
		virtual void writeString(const std::string& item) { writer->write(item.c_str()); }

		virtual void writeArray(const void *data, const Size count, const Size size)
		{ 
			UInt8 *d=(UInt8*)data;
			char *t=(char*)malloc(count*10+1);
			char *ct=t;
			for(Size i=0;i<count;i++)
			{
				if(i>0) *ct++=',';
				switch(size)
				{
				case 1: sprintf(ct,"%u",*(UInt8*)d); break;
				case 2: sprintf(ct,"%u",*(UInt16*)d); break;
				case 4: sprintf(ct,"%u",*(UInt32*)d); break;
				case 8: sprintf(ct,U64F,*(UInt64*)d); break;
				}
				ct+=strlen(ct);
				d+=size;
			}
			writer->write(t);
		}

		// For named serialization
		virtual void enter(const char *name) { writer->enter(name); }
		virtual void leave(const char *name) { writer->leave(name); }

		virtual Bool write(const Object *item)
		{
			refs.addElement(item);
			for(Size i=1;i<refs.size();i++)
			{
				enter("item");			
				refs.elementAt(i)->writeV(this);
				leave("item");
			}

			return writer->flush(refs);
		}
	};

	//! Text input stream
	template<typename Reader> class TextInputStream : public InputStream
	{
		//! Source reader
		Reader *reader;
	public:
		//! Constructor
		TextInputStream(Reader *r)
		{
			reader=r;
		}

		virtual void readInt8(Int8& item)     { const char *s=reader->read();
			if(s) { Int32 t; sscanf(s,"%d",&t); item=(Int8)t; } }
		virtual void readUInt8(UInt8& item)   { const char *s=reader->read();
			if(s) { UInt32 t; sscanf(s,"%u",&t); item=(UInt8)t; } }
		virtual void readInt16(Int16& item)   { const char *s=reader->read();
			if(s) { Int32 t; sscanf(s,"%d",&t); item=(Int16)t; } }
		virtual void readUInt16(UInt16& item) { const char *s=reader->read();
			if(s) { UInt32 t; sscanf(s,"%u",&t); item=(UInt16)t; } }
		virtual void readInt32(Int32& item)   { const char *s=reader->read();
			if(s) { Int32 t; sscanf(s,"%d",&t); item=(Int32)t; } }
		virtual void readUInt32(UInt32& item) { const char *s=reader->read();
			if(s) { UInt32 t; sscanf(s,"%u",&t); item=(UInt32)t; } }
		virtual void readInt64(Int64& item)   { const char *s=reader->read();
			if(s) { Int64 t; sscanf(s,I64F,&t); item=(Int64)t; } }
		virtual void readUInt64(UInt64& item) { const char *s=reader->read();
			if(s) { UInt64 t; sscanf(s,U64F,&t); item=(UInt64)t; } }
		virtual void readFloat(Float& item)   { const char *s=reader->read();
			if(s) { Float t; sscanf(s,"%f",&t); item=(Float)t; } }
		virtual void readDouble(Double& item) { const char *s=reader->read();
			if(s) { Double t; sscanf(s,"%lf",&t); item=(Double)t; } }
		virtual void readBool(Bool& item)     { const char *s=reader->read();
			if(s) { item=!strcmp(s,"true"); } }
		virtual void readString(std::string& item) { const char *s=reader->read();
			if(s) { item=s; } }

		virtual void readArray(void *data, const Size count, const Size size)
		{
			const char *s=reader->read();
			if(s==NULL)
				return;

			char *t=(char*)malloc(strlen(s)+1);
			strcpy(t,s);
			UInt8 *d=(UInt8*)data;
			char *c=strtok(t,",");
			for(Size i=0;i<count;i++)
			{
				switch(size)
				{
					case 1: { UInt32 v; sscanf(c,"%u",&v); *(UInt8*)d=v; } break;
					case 2: { UInt32 v; sscanf(c,"%u",&v); *(UInt16*)d=v; } break;
					case 4: { UInt32 v; sscanf(c,"%u",&v); *(UInt32*)d=v; } break;
					case 8: { UInt64 v; sscanf(c,U64F,&v); *(UInt64*)d=v; } break;
				}
				d+=size;
				c=strtok(NULL,",");
			}
			free(t);
		}

		// For named serialization
		virtual void enter(const char *name) { reader->enter(name); }
		virtual void leave(const char *name) { reader->leave(name); }

		virtual Object *read()
		{
			if(!reader->init(refs))
				return NULL;

			for(Size i=1;i<refs.size();i++)
			{
				enter("item");
				refs.elementAt(i)->readV(this);
				leave("item");			
			}

			return refs.elementAt(1);
		}
	};

	//! Serialization target for binary files
	/*! Writes an object to a binary file. Exposes the writer concept used by
		TextOutputStream.
	*/
	class XMLFileWriter
	{
	private:
		//! File pointer
		FILE *fptr;
		//! counter for indentation. Increased on enter, decreased on leave
		int indent;

		enum LastCall
		{
			ENTER,
			LEAVE,
			WRITE,
			NONE
		} lastCall;
	public:
		//! Constructor
		XMLFileWriter(const char *fname)
		{
			fptr=Toolkit::getFileManager()->openFP(fname,"w");
			fprintf(fptr,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
			fprintf(fptr,"<xmlserializeddata>\n");
			indent=0;
			lastCall=NONE;
		}
		//! Destructor
		~XMLFileWriter()
		{
			fclose(fptr);
		}

		//! Enter a new object/item
		void enter(const char *str)
		{
			if (lastCall==ENTER)
				fprintf(fptr, "\n");
			for(int i=0;i<indent;i++)
				fprintf(fptr, " ");
			fprintf(fptr,"<%s>",str);
			indent++;
			lastCall=ENTER;
		}

		//! Leave current object/item
		void leave(const char *str)
		{
			indent--;
			if (lastCall==LEAVE)
			{
				for(int i=0;i<indent;i++)
					fprintf(fptr, " ");
			}
			fprintf(fptr,"</%s>\n",str);
			lastCall=LEAVE;
		}

		//! Write string to target
		void write(const char *str)
		{
			fprintf(fptr,str);
			lastCall=WRITE;
		}

		//! Serialize an object to this target
		Bool flush(const base::ConstRefVector& refs);
	};

	//! Helper class for XML parser
	/*! This class basically reads characters from a file in a slightly
		structured fashion.
	*/
	class XMLInputStream
	{
	private:
		//! Source file
		FILE *fptr;
		//! Next character in stream
		char nextChar;

	public:
		//! Constructor
		XMLInputStream(const char *fname)
		{
			fptr=Toolkit::getFileManager()->openFP(fname,"r");

			if (fptr)
			{
				nextChar=0;
				nextChar=fgetc(fptr);
			}
		}
		//! Destructor
		~XMLInputStream()
		{
			if (fptr)
			{
				fclose(fptr);
			}
		}
		
		char getNextChar()
		{
			if (fptr)
			{
				char b=nextChar;
				nextChar=fgetc(fptr);
				return b;
			}
			else
				return 0;
		}
		
		char peekNextChar()
		{
			return nextChar;
		}
		
		void skipBlanks()
		{
			while(nextChar==' ' || nextChar=='\t' || nextChar=='\r' || nextChar=='\n')
				getNextChar();
		}

		void convert(char *str)
		{
			static char *subst[]={
				"\x00a0" "nbsp;",
				"&"      "amp;",
				"<"      "lt;",
				">"      "gt;",
				"\xc3\xa9" "eacute;",
				"\xc3\xa0" "egrave;",
				NULL
			};

			for(char *a=str;*a;a++)
			{
				if(*a=='&')
				{
					for(int c=0;subst[c];c++)
					{
						if(!strncmp(&a[1],&subst[c][1],strlen(&subst[c][1])))
						{
							*a=subst[c][0];
							memmove(&a[1],&a[strlen(&subst[c][1])+1],strlen(&a[strlen(&subst[c][1])+1])+1);
							break;
						}
					}
				}
			}
		}
		
		std::string getString()
		{
			skipBlanks();
			char tmp[1024];
			int tp=0;
			if(nextChar=='"')
			{
				// Double-quoted string
				getNextChar();
				while(nextChar!='"')			
				{
					tmp[tp++]=getNextChar();
				}
				tmp[tp]=0;
				getNextChar();
			}
			else if(nextChar=='\'')
			{
				// Single-quoted string
				getNextChar();
				while(nextChar!='\'')
				{
					tmp[tp++]=getNextChar();
				}
				tmp[tp]=0;
				getNextChar();
			}
			else
			{
				// Unquoted string, cut on '>' and '=' as well...
				while(nextChar!='>' && nextChar!='/' && nextChar!='=' && nextChar!=' ' && nextChar!='\t' && nextChar!='\r' && nextChar!='\n')
				{
					tmp[tp++]=getNextChar();
				} 
				tmp[tp]=0;
			}
			skipBlanks();
			convert(tmp);
			
			return std::string(tmp);
		}

		bool valid() { return fptr!=NULL; }
		bool eof() { return feof(fptr)!=0; }
	};

	//! Node in an XML document
	class XMLNode : public Object
	{
		CLASSDEF(XMLNode)
			BASECLASS(Object)
		MEMBERS
			//! Child nodes
			ITEM(Vector<XMLNode>,childNodes)
			//! Attributes of this node
			ITEM(Vector<XMLNode>,attributes)

			//! Name of the node
			ITEM(std::string,nodeName)
			//! Value of the node
			ITEM(std::string,nodeValue)
			//! Type of the node
			ITEM(Int32,nodeType)
		CLASSEND;

		//! Node types
		enum {
			ROOT, ELEMENT, TEXT, ATTRIBUTE
		};

		//! Constructor
		XMLNode(int type = ROOT)
		{
			nodeType=type;
		}

		//! Constructor
		XMLNode(int type, const char *name)
		{
			nodeType=type;
			if(type!=TEXT)
				nodeName=name;
			else
				nodeValue=name;
		}
	
		//! Constructor
		XMLNode(int type, const char *name, const char *val)
		{
			nodeType=type;
			nodeName=name;
			nodeValue=val;
		}
	
		//! Add a child node
		base::Ptr<XMLNode> appendChild(XMLNode *child)
		{
			childNodes.add(child);
			return child;
		}
	
		//! Add an attribute
		base::Ptr<XMLNode> appendAttribute(XMLNode *attr)
		{
			attributes.add(attr);
			return attr;
		}

		//! Move all following nodes into node
		/*! This is used to construct the tree while parsing. Nodes are
			added to a simple list, and when the closing tag is found, the
			nodes are collapsed into the startup node.
		*/
		void moveInto(Size node)
		{
			bool single=(node+2==childNodes.size());
			while(childNodes.size()>node+1)
			{
				base::Ptr<XMLNode> t=childNodes[node+1];
				childNodes.remove(t);
				if(t->nodeType!=TEXT || single)
					childNodes[node]->appendChild(t);
			}
		}
		
		//! Retrieve named attribute
		base::Ptr<XMLNode> getAttribute(const char *attr)
		{
			for(Size i=0;i<attributes.size();i++)
			{
				if(attributes[i]->nodeName==attr)
					return attributes[i];
			}		
			return NULL;
		}

		//! Retrieve named child
		base::Ptr<XMLNode> getChild(const char *node)
		{
			for(Size i=0;i<childNodes.size();i++)
			{
				if(childNodes[i]->nodeName==node)
					return childNodes[i];
			}
			return NULL;
		}
	};

	//! Read an XML document from a file
	Ptr<XMLNode> readXML(const char *fname);

	//! Reader for XML files
	class XMLFileReader
	{
		//! Document root node
		Ptr<XMLNode> root;
		//! Active node in document
		struct State
		{
			//! Node
			XMLNode *node;
			//! Child index
			int child;
			//! Is the state valid?
			bool valid;
		} current;
		//! Hierarchy of traversed nodes
		std::vector<State> states;

	public:
		//! Constructor
		XMLFileReader(const char *fname)
		{
			root=readXML(fname);
			if (root==NULL)
				current.valid=false;
			else
				current.valid=true;
		}
		//! Read a string from document
		const char *read();
		//! Enter new item
		void enter(const char *str);
		//! Leave current item
		void leave(const char *str);
		//! Initialize reference vector
		bool init(base::RefVector& refs);
		//! Return if reader is in valid state
		bool valid() { return current.valid; }
	};

	//! Serialization target for binary files
	/*! Writes an object to a binary file.
	*/
	class BinaryFileWriter
	{
		//! File pointer
		FILE *fptr;
	public:
		//! Constructor
		BinaryFileWriter(const AnsiChar *fname)
		{
			fptr=Toolkit::getFileManager()->openFP(fname,"wb");
		}
		//! Destructor
		~BinaryFileWriter()
		{
			fclose(fptr);
		}

		//! Write bytes to target
		Bool writeBytes(const void *buf, Size len) 
		{ 
			fwrite(buf,len,1,fptr);
			return true; 
		}

		//! Serialize an object to this target
		Bool flush(const base::ConstRefVector& refs);
	};

	//! Serialization source for binary files
	/*! Read an object from a file.
	*/
	class BinaryFileReader
	{
		//! File pointer
		FILE *fptr;
	public:
		//! Constructor
		BinaryFileReader(const AnsiChar *fname)
		{
			fptr=Toolkit::getFileManager()->openFP(fname,"rb");
		}
		//! Destructor
		~BinaryFileReader()
		{
			fclose(fptr);
		}

		//! Read bytes from source
		Bool readBytes(void *buf, Size len) 
		{ 
			fread(buf,len,1,fptr);
			return true;
		}

		//! Initialize references
		Bool init(base::RefVector& refs);
	};


}

template<typename T>
T* deserialize(const char *filename)
{
	T* obj;
	base::XMLFileReader xr(filename);
	if (xr.valid())
	{
		base::TextInputStream<base::XMLFileReader> tis(&xr);
		obj=(T*)(base::Object*)tis.read();
	}
	else
	{
		obj=new T();
	}
	return obj;
}

#endif

