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

#include "xmlserializer.h"

namespace base
{
	Bool XMLFileWriter::flush(const base::ConstRefVector& refs)
	{
		fprintf(fptr," <refs>\n");
		for(Size i=1;i<refs.size();i++)
		{
			char ttn[256];
			strcpy(ttn,refs.elementAt(i)->getTypeNameV());
			for(char *a=ttn;*a;a++)
			{
				if(*a=='<')
				{
					memmove(&a[4],&a[1],strlen(a));
					memcpy(a,"&lt;",4);
				}
				if(*a=='>')
				{
					memmove(&a[4],&a[1],strlen(a));
					memcpy(a,"&gt;",4);
				}
			}
			fprintf(fptr,"  <ref type=\"%s\"/>\n",ttn);
		}
		fprintf(fptr," </refs>\n");
		fprintf(fptr,"</xmlserializeddata>\n");

		return true;
	}

	Ptr<XMLNode> readXML(const char *fname)
	{
		XMLInputStream source(fname);
		char currentText[65536];
		int currentTextLength=0;
		bool runningText=false;
		
		// Create a document skeleton
		XMLNode *rootNode=new XMLNode(XMLNode::ROOT,"ROOT");
		
		char cc;
		while(!source.eof())
		{
			cc=source.getNextChar();
			if(cc=='<')
			{
				if(currentTextLength)
				{
					// Dump current text
					currentText[currentTextLength]=0;
					if(runningText)
					{
						XMLNode *nn=new XMLNode(XMLNode::TEXT,currentText);
						rootNode->appendChild(nn);
					}
					currentTextLength=0;
				}
				// Start a new tag
				if(source.peekNextChar()=='/')
				{
					// Close tag
					source.getNextChar();
					std::string tag=source.getString();
					for(Int32 i=(Int32)rootNode->childNodes.size()-1;i>=0;i--)
					{
						if(rootNode->childNodes[i]->nodeName==tag && rootNode->childNodes[i]->childNodes.size()==0)
						{
							rootNode->moveInto(i);
							break;
						}
					}
					while(source.getNextChar()!=L'>');	
				}
				else if(source.peekNextChar()=='!')
				{
					// Probably a comment
					// Find --> and marker
					while(true)
					{
						while(source.getNextChar()!=L'-');
						char c1, c2;
						c1=source.getNextChar();
						c2=source.peekNextChar();
						if(c1==L'-' && c2==L'>')
						{
							source.getNextChar();
							break;
						}
					}
				}
				else if(source.peekNextChar()=='?')
				{
					// The XML start tag
					source.getNextChar();
					while(true)
					{
						while(source.getNextChar()!=L'?');
						char c2;
						c2=source.peekNextChar();
						if(c2==L'>')
						{
							source.getNextChar();
							break;
						}
					}
				}
				else
				{
					// Normal
					std::string tag=source.getString();
					// Open tag
					XMLNode *nn=new XMLNode(XMLNode::ELEMENT,tag.c_str());
					while(source.peekNextChar()!='>')
					{
						if(source.peekNextChar()!=L'/')
						{
							std::string attrib=source.getString();
							std::string value;
							if(source.peekNextChar()=='=')
							{
								source.getNextChar();	// Remove '='
								value=source.getString();
							}
							XMLNode *na=new XMLNode(XMLNode::ATTRIBUTE,attrib.c_str(),value.c_str());
							nn->appendAttribute(na);
						}
						else
							source.getNextChar();
					}
					source.getNextChar();	// Remove '>'
					rootNode->appendChild(nn);
				}
			}
			else
			{
				if(cc==' ' || cc=='\t' || cc=='\r' || cc=='\n')
				{
					if(runningText)
					{
						if(!currentTextLength || currentText[currentTextLength-1]!=' ')
							currentText[currentTextLength++]=' ';
					}	
				}
				else if(cc=='&')
				{
					// Special char coming up
					char sc[32];
					int scp=0;
					sc[scp]=source.getNextChar();
					while(sc[scp]!=';' && scp<30)
					{
						scp++;
						sc[scp]=source.getNextChar();
					}
					sc[scp]=0;
					
					static char *subst[]={
						"\x00a0" "nbsp",
						"&"      "amp",
						"<"      "lt",
						">"      "gt",
						"é"      "eacute",
						"è"      "egrave",
						NULL
					};
					
					for(scp=0;subst[scp];scp++)
					{
						if(!strcmp(&subst[scp][1],sc))
						{
							currentText[currentTextLength++]=subst[scp][0];
							break;
						}
					}
					if(!subst[scp])
						currentText[currentTextLength++]='?';
				}
				else
				{
					currentText[currentTextLength++]=cc;
					runningText=true;
				}
			}
		}
		if(currentTextLength)
		{
			// Dump current text
			currentText[currentTextLength]=0;
			if(runningText)
			{
				XMLNode *nn=new XMLNode(XMLNode::TEXT,currentText);
				rootNode->appendChild(nn);
			}
			currentTextLength=0;
		}
		
		return rootNode;
	}

	const char *XMLFileReader::read()
	{
		// Normally we should be in a leaf node here
		if(!current.valid || current.node->childNodes.size()!=1)
			return NULL;
		return current.node->childNodes[0]->nodeValue.c_str();
	}
	void XMLFileReader::enter(const char *str)
	{
		if(current.valid==false)
		{
			states.push_back(current);
			return;
		}
		for(current.child++;current.child<(Int32)current.node->childNodes.size();current.child++)
		{
			if(current.node->childNodes[current.child]->nodeName==str)
			{
				states.push_back(current);
				current.node=current.node->childNodes[current.child];
				current.child=-1;
				current.valid=true;
				return;
			}
		}
		current.valid=false;
	}
	void XMLFileReader::leave(const char *str)
	{
		current=*states.rbegin();
		states.pop_back();
	}
	bool XMLFileReader::init(base::RefVector& refs)
	{
		XMLNode *rr=root->getChild("xmlserializeddata")->getChild("refs");
		for(Size i=0;i<rr->childNodes.size();i++)
		{
			const char *tn=rr->childNodes[i]->getAttribute("type")->nodeValue.c_str();
			Ptr<Object> o=Registrar::create(tn);
			o->upcount();
			refs.addElement(o);
		}
		current.child=-1;
		current.node=root->getChild("xmlserializeddata");
		current.valid=true;
		return true;
	}

	Bool BinaryFileWriter::flush(const base::ConstRefVector& refs)
	{
		Int32 roff=ftell(fptr);
		for(Size i=1;i<refs.size();i++)
		{
			fprintf(fptr,"%s@",refs.elementAt(i)->getTypeNameV());
		}
		fwrite(&roff,4,1,fptr);

		return true;
	}

	Bool BinaryFileReader::init(base::RefVector& refs)
	{
		Ptr<Object> ret;

		fseek(fptr,-4,2);
		Int32 roff, foff;
		foff=ftell(fptr);
		fread(&roff,4,1,fptr);
		fseek(fptr,roff,0);

		char *tmp=(char *)malloc(foff-roff+1);
		fread(tmp,foff-roff,1,fptr);
		tmp[foff-roff]=0;
		char *tn=strtok(tmp,"@");
		while(tn!=NULL)
		{
			Ptr<Object> o=Registrar::create(tn);
			o->upcount();
			refs.addElement(o);
			tn=strtok(NULL,"@");
		}
		fseek(fptr,0,0);

		return true;
	}
}
