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

#include <FileManager.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <SDL_endian.h>
#include <iostream>
#include <valarray>
#include <zlib.h>
#include "BinaryStream.h"
#include "TextStream.h"

// here we handle compile time options
#ifdef HAVE_CONFIG_H
#  include "config.h"
#else
#	ifdef WIN32
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	else
#		define PACKAGE_DATA_DIR ".."
#		define PACKAGE_SOURCE_DIR "../.."
#	endif
#endif

// include for directory listing
#ifdef WIN32
#	include <windows.h>
#	include <io.h>
#	include <direct.h>
#	include "win32_dirent.h"
#else
#	include <sys/types.h>
#	include <dirent.h>
#	include <sys/stat.h>
#endif

#ifndef WIN32
#ifndef __APPLE__
#	include "unistd.h"
#endif
#endif

namespace GAGCore
{
	FileManager::FileManager(const std::string gameName)
	{
		#ifndef WIN32
		const std::string homeDir = getenv("HOME");
		if (!homeDir.empty())
		{
			std::string gameLocal(homeDir);
			gameLocal += "/.";
			gameLocal += gameName;
			mkdir(gameLocal.c_str(), S_IRWXU);
			addDir(gameLocal.c_str());
		}
		else
			std::cerr << "FileManager::FileManager : warning, can't get home directory by using getenv(\"HOME\")" << std::endl;
		#endif
#ifdef __APPLE__
		addDir("./Contents/Resources");
#endif
		addDir(".");

		#ifndef WIN32
		#ifndef __APPLE__
		/* Find own path
		 * TODO: Make nicer */

		char link[100];
		#ifdef __FreeBSD__
		char proc[]="/proc/curproc/file";
		#else
		char proc[]="/proc/self/exe";
		#endif
		char * pch;

		int linksize = readlink(proc, link, sizeof(link));
		if (linksize < 0)
		{
			perror("readlink() error");
		}
		else
		{
			assert ((int)sizeof(link) > linksize);
			link[linksize] = '\0';

			pch = strrchr(link,'/');
			if ( (pch-link) > 0)
				link[pch-link] = '\0';
			else
				link[1] = '\0';

			pch = strrchr(link,'/');
			if ( (pch-link) > 0)
				link[pch-link] = '\0';

			if ((linksize + 13) <= (int)sizeof(link))
			{
				strcat(link, "/share/glob2");
				addDir(link);
			}
		}
		#endif
		#endif

		addDir(PACKAGE_DATA_DIR);
		addDir(PACKAGE_SOURCE_DIR);
		fileListIndex = -1;
	}
	
	FileManager::~FileManager()
	{
		dirList.clear();
		clearFileList();
	}
	
	void FileManager::clearFileList(void)
	{
		fileList.clear();
		fileListIndex = -1;
	}
	
	void FileManager::addDir(const std::string dir)
	{
		dirList.push_back(dir);
	}
	
	void FileManager::addWriteSubdir(const std::string subdir)
	{
		for (size_t i = 0; i < dirList.size(); i++)
		{
			std::string toCreate(dirList[i]);
			toCreate += '/';
			toCreate += subdir;
			#ifdef WIN32
			int result = _mkdir(toCreate.c_str());
			#else
			int result = mkdir(toCreate.c_str(), S_IRWXU);
			#endif
			// NOTE : We only want to create the subdir for the first index
	// 		if (result==0)
				break;
			if ((result==-1) && (errno==EEXIST))
				break;
		}
	}
	
	SDL_RWops *FileManager::openWithbackup(const std::string filename, const std::string mode)
	{
		if (mode.find('w') != std::string::npos)
		{
			std::string backupName(filename);
			backupName += '~';
			rename(filename.c_str(), backupName.c_str());
		}
		return SDL_RWFromFile(filename.c_str(), mode.c_str());
	}
	
	FILE *FileManager::openWithbackupFP(const std::string filename, const std::string mode)
	{
		if (mode.find('w') != std::string::npos)
		{
			std::string backupName(filename);
			backupName += '~';
			rename(filename.c_str(), backupName.c_str());
		}
		return fopen(filename.c_str(), mode.c_str());
	}
	
	std::ofstream *FileManager::openWithbackupOFS(const std::string filename, std::ofstream::openmode mode)
	{
		if (mode & std::ios_base::out)
		{
			std::string backupName(filename);
			backupName += '~';
			rename(filename.c_str(), backupName.c_str());
		}
		std::ofstream *ofs = new std::ofstream(filename.c_str(), mode);
		if (ofs->is_open())
			return ofs;
		
		delete ofs;
		return NULL;
	}
	
	StreamBackend *FileManager::openOutputStreamBackend(const std::string filename)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			
			FILE *fp = fopen(path.c_str(), "wb");
			if (fp)
				return new FileStreamBackend(fp);
		}
	
		return new FileStreamBackend(NULL);
	}
	
	StreamBackend *FileManager::openInputStreamBackend(const std::string filename)
	{	
	
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			
			FILE *fp = fopen(path.c_str(), "rb");
			if (fp)
				return new FileStreamBackend(fp);
		}
	
		return new FileStreamBackend(NULL);
	}
	
	StreamBackend *FileManager::openCompressedOutputStreamBackend(const std::string filename)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			
			//Test if it can be opened first
			FILE *fp = fopen(path.c_str(), "wb");
			if(fp)
			{
				fclose(fp);
				return new ZLibStreamBackend(path, false);
			}
		}
	
		return new ZLibStreamBackend("", false);
	}
	
	StreamBackend *FileManager::openCompressedInputStreamBackend(const std::string filename)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			
			FILE *fp = fopen(path.c_str(), "rb");
			if(fp)
			{
				fclose(fp);
				return new ZLibStreamBackend(path, true);
			}
		}
	
		return new ZLibStreamBackend("", false);
	}
	
	SDL_RWops *FileManager::open(const std::string filename, const std::string mode)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
	
			//std::cerr << "FileManager::open trying to open " << path << " corresponding to source [" << dirList[i] << "] and filename [" << filename << "] with mode " << mode << "\n" << std::endl;
			SDL_RWops *fp = openWithbackup(path.c_str(), mode.c_str());
			if (fp)
				return fp;
		}
	
		return NULL;
	}
	
	FILE *FileManager::openFP(const std::string filename, const std::string mode)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			
			FILE *fp = openWithbackupFP(path.c_str(), mode.c_str());
			if (fp)
				return fp;
		}
	
		return NULL;
	}
	
	std::ifstream *FileManager::openIFStream(const std::string &fileName)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += fileName;
	
			std::ifstream *fp = new std::ifstream(path.c_str());
			if (fp->good())
				return fp;
			else
				delete fp;
		}
		
		return NULL;
	}
	
	Uint32 FileManager::checksum(const std::string filename)
	{
		Uint32 cs = 0;
		SDL_RWops *stream = open(filename);
		if (stream)
		{
			int length = SDL_RWseek(stream, 0, SEEK_END);
			SDL_RWseek(stream, 0, SEEK_SET);
			
			int lengthBlock = length & (~0x3);
			for (int i=0; i<(lengthBlock>>2); i++)
			{
				cs ^= SDL_ReadBE32(stream);
				cs = (cs<<31)|(cs>>1);
			}
			int lengthRest = length & 0x3;
			for (int i=0; i<lengthRest; i++)
			{
				unsigned char c;
				SDL_RWread(stream, &c, 1, 1);
				cs ^= (static_cast<Uint32>(c))<<(8*i);
			}
			SDL_RWclose(stream);
		}
		return cs;
	}
	
	time_t FileManager::mtime(const std::string filename)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
	
			//std::cerr << "FileManager::open trying to open " << path << " corresponding to source [" << dirList[i] << "] and filename [" << filename << "] with mode " << mode << "\n" << std::endl;
			struct stat stats;
			if (stat(path.c_str(), &stats) == 0)
				return stats.st_mtime;
		}
		return 0;
	}
	
	void FileManager::remove(const std::string filename)
	{
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			std::remove(path.c_str());
		}
	}
	
	bool FileManager::isDir(const std::string filename)
	{
		#ifdef WIN32
		struct _stat s;
		#else
		struct stat s;
		#endif
		s.st_mode = 0;
		int serr = 1;
		for (size_t i = 0; (serr != 0) && (i < dirList.size()); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += filename;
			#ifdef WIN32
			serr = ::_stat(path.c_str(), &s);
			#else
			serr = stat(path.c_str(), &s);
			#endif
		}
		return (s.st_mode & S_IFDIR) != 0;
	}
	
	bool FileManager::gzip(const std::string &source, const std::string &dest)
	{
		// Open streams
		StreamBackend *srcStream = openInputStreamBackend(source);
		FILE *destStream = openFP(dest.c_str(), "wb");
		
		// Check
		if ((!srcStream->isValid()) || (destStream == NULL))
		{
			delete srcStream;
			return false;
		}
		
		// Preapare source
		srcStream->seekFromEnd(0);
		size_t fileLength = srcStream->getPosition();
		srcStream->seekFromStart(0);
		std::valarray<char> buffer(fileLength);
		
		// Compress
		srcStream->read(&buffer[0], fileLength);
		gzFile gzStream = gzdopen(fileno(destStream), "wb");
		gzwrite(gzStream, &buffer[0], fileLength);
		
		// Close
		gzclose(gzStream);
		delete srcStream;
		
		return true;
	}
	
	bool FileManager::gunzip(const std::string &source, const std::string &dest)
	{
		// Open streams
		FILE *srcStream = openFP(source.c_str(), "rb");
		StreamBackend *destStream = openOutputStreamBackend(dest);
		
		// Check
		if ((!destStream->isValid()) || (srcStream == NULL))
		{
			delete destStream;
			return false;
		}
		
		// Preapare source
		gzFile gzStream = gzdopen(fileno(srcStream), "rb");
		#define BLOCK_SIZE 1024*1024
		std::string buffer;
		size_t len = 0;
		size_t bufferLength = 0;
		
		// Uncompress
		while (gzeof(gzStream) == 0)
		{
			buffer.resize(bufferLength + BLOCK_SIZE);
			len += gzread(gzStream, const_cast<void *>(static_cast<const void *>(buffer.data() + bufferLength)), BLOCK_SIZE);
			bufferLength += BLOCK_SIZE;
		}
		
		// Write
		destStream->write(buffer.c_str(), len);
		
		// Close
		gzclose(gzStream);
		delete destStream;
		
		return true;
	}

	bool FileManager::addListingForDir(const std::string realDir, const std::string extension, const bool dirs)
	{
		DIR *dir = opendir(realDir.c_str());
		struct dirent *dirEntry;
	
		if (!dir)
		{
			#ifdef DBG_VPATH_LIST
			std::cerr << "GAG : Open dir failed for dir " << realDir << std::endl;
			#endif
			return false;
		}
	
		while ((dirEntry = readdir(dir))!=NULL)
		{
			#ifdef DBG_VPATH_LIST
			std::cerr << realDir << std::endl;
			#endif
			
			// there might be a way to optimize the decision of the ok
			bool ok = true;
			// hide hidden stuff
			if (dirEntry->d_name[0] == '.')
			{
				ok = false;
			}
			// take directories if asked
			else if (isDir(dirEntry->d_name)) // d_type is not portable to all systems
			{
				ok = dirs;
			}
			// check extension if provided
			else if (!extension.empty())
			{
				size_t l, nl;
				l=extension.length();
				nl=strlen(dirEntry->d_name);
				ok = ((nl>l) &&
					(dirEntry->d_name[nl-l-1]=='.') &&
					(strcmp(extension.c_str(),dirEntry->d_name+(nl-l))==0));
			}
			if (ok)
			{
				// test if name already exists in vector
				bool alreadyIn = false;
				for (size_t i = 0; i < fileList.size(); ++i)
				{
					if (fileList[i] == dirEntry->d_name)
					{
						alreadyIn = true;
						break;
					}
				}
				if (!alreadyIn)
				{
					fileList.push_back(dirEntry->d_name);
				}
			}
		}
	
		closedir(dir);
		return true;
	}
	
	bool FileManager::initDirectoryListing(const std::string virtualDir, const std::string extension, const bool dirs)
	{
		bool result = false;
		clearFileList();
		for (size_t i = 0; i < dirList.size(); ++i)
		{
			std::string path(dirList[i]);
			path += DIR_SEPARATOR;
			path += virtualDir;
			result = addListingForDir(path.c_str(), extension, dirs) || result;
		}
		if (result)
			fileListIndex=0;
		else
			fileListIndex=-1;
		return result;
	}
	
	const std::string FileManager::getNextDirectoryEntry(void)
	{
		if ((fileListIndex >= 0) && (fileListIndex < (int)fileList.size()))
		{
			return fileList[fileListIndex++];
		}
		return "";
	}
}
