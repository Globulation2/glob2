/*
    DPS - Dynamic Parallel Schedules
    Copyright (C) 2000-2003 Sebastian Gerlach

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

/*! \file base.cpp 
	\brief Base implementation
	
	This file contains implementations for all non-templated methods from the
	base namespace.
*/

#include <dps/base.h>

#ifdef DPS_COMPILER_MS
// Force inclusion of Windows Sockets library on Win32 builds with the MS compilers
#pragma comment(lib,"ws2_32.lib")
#endif

namespace base
{
	//! Start time for timer
	Int64 Timer::baseTime;
	//! Frequency of timer
	Int64 Timer::frequency;

	//! Name of executable
	std::string executableName;

	RegistrarItem *Registrar::head = NULL;
	RegistrarItem *Registrar::tail = NULL;
	Size Registrar::count = 0;
	RegistrarItem **Registrar::items = NULL;

	//! Lock for reference counter critical section (UNIX only)
	CriticalSection refLock;

	//! Default subsystem
	Subsystem baseSubsystem("base");

	//! Head of subsystem list
	Subsystem *subsystemHead = NULL;

	//! Print a nicely formatted string to standard output (inner)
	void baseDump(const Subsystem&  subsystem, Int32 level, const AnsiChar *owner, const AnsiChar *format, va_list args)
	{
		if(level<subsystem.getDumpLevel())
		{
			AnsiChar buf[1024];
			vsprintf(buf,format,args);
#ifdef DPS_PLATFORM_WIN32
			printf("%4.4d %-8I64d %-20s:%d: %s\n",GetCurrentThreadId(),(Timer::get())*1000000/Timer::getFrequency(),owner,level,buf);
#else
			printf("%-20s:%d: %s\n",owner,level,buf);
#endif
			fflush(stdout);
		}
	}

	//! Print a nicely formatted string to standard output (outer)
	void baseDump(const Subsystem&  subsystem, Int32 level, const AnsiChar *owner, const AnsiChar *format, ...)
	{
		va_list args;
		va_start(args,format);
		baseDump(subsystem,level,owner,format,args);
		va_end(args);
	}

#ifdef DPS_PLATFORM_WIN32
	void dumpError(int err)
	{
		char buf[1024];
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,err,0,buf,1024,NULL);
		printf(buf);
	}
#endif

	void fatalError(const char *fmt, ...)
	{
		va_list args;
		va_start(args,fmt);
		vfprintf(stderr,fmt,args);
		va_end(args);
		exit(0);
	}

	RegistrarItem::RegistrarItem(const AnsiChar *name)
	{
		this->name=name;
		next=NULL;
		Registrar::addItem(this);
	}


	void Object::dump(Int32 level, const char *fmt, ...)
	{
		va_list args;
		va_start(args,fmt);
		baseDump(dpsGetSubsystem(),level,getTypeNameV(),fmt,args);
		va_end(args);
	}

#ifdef DPS_PLATFORM_WIN32
	// Windows implementations of the base stuff

	Result SysThread::start()
	{
//		dump(9,"Starting thread %s",getTypeNameV());
		hThread=CreateThread(NULL,0,threadWrapper,this,0,&tid);
		if(hThread==NULL)
			return 0;
		return 1;
	}
	
	Result SysThread::stop(Bool fast)
	{
		if(hThread==NULL)
			return 0;
//		dump(8,"Stopping");

		HANDLE ht;
		DuplicateHandle(GetCurrentProcess(),hThread,GetCurrentProcess(),&ht,0,FALSE,DUPLICATE_SAME_ACCESS);

		terminateEvent.set();
		if(!fast)
		{
			if(WaitForSingleObject(ht,THREAD_TIMEOUT)!=WAIT_OBJECT_0)
			{
				char buf[1024];
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,GetLastError(),0,buf,1024,NULL);
				TerminateThread(ht,0xbad);
				CloseHandle(hThread);
				return 0;
			}
		}
//		dump(8,"Stopped");
		CloseHandle(ht);
		return 1;
	}
	
	Result SysThread::wait()
	{
		if(hThread && WaitForSingleObject(hThread,INFINITE)!=WAIT_OBJECT_0)
			return 0;
		return 1;
	}

	UInt32 ExecHelper::exec(const char *cmd)
	{
		STARTUPINFO si;
		memset(&si,0,sizeof(si));
		si.cb=sizeof(si);
		PROCESS_INFORMATION pi;

		// Copy to temp buffer since CreateProcess doesn't like the const string...
		char *tb=(char*)malloc(strlen(cmd)+1);
		strcpy(tb,cmd);
		char *tb2=(char*)malloc(strlen(cmd)+1);
		strcpy(tb2,&cmd[1]);
		strtok(tb2,"\"");
		char *a;
		for(a=&tb2[strlen(tb2)];*a!='\\' && *a!='/';a--);
		*a=0;

		// Exchange comments below for detached console on launched processes
//		BOOL rv=CreateProcess(NULL,tb,NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,tb2,&si,&pi);
		BOOL rv=CreateProcess(NULL,tb,NULL,NULL,FALSE,0,NULL,tb2,&si,&pi);

		if(!rv)
		{
			char buf[1024];
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,GetLastError(),0,buf,1024,NULL);
			base::baseDump(base::baseSubsystem,0,"OSExec","Could not launch %s, error %s",tb,buf);
		}
		free(tb);
		free(tb2);
		
		if(rv)
			return pi.dwProcessId;
		return 0;
	}

	std::string ExecHelper::getExecutableName()
	{
		char buf[1024];
		GetModuleFileName(NULL,buf,1024);
		return buf;
	}

	void ExecHelper::kill(UInt32 pid)
	{
		HANDLE hp;
		hp=OpenProcess(PROCESS_TERMINATE,TRUE,pid);
		TerminateProcess(hp,0);
	}

	SharedLibrary::SharedLibrary(const char *libName)
	{
		hLib=LoadLibrary(libName);
	}

	SharedLibrary::~SharedLibrary()
	{
		FreeLibrary(hLib);
	}

	void *SharedLibrary::getProcAddress(const char *procName)
	{
		return (void*)GetProcAddress(hLib,procName);
	}

#else
	// UNIX implementations of the base stuff

	Result SysThread::start()
	{
//		dump(8,"Starting thread %s",getTypeNameV());
		pthread_create(&theThread,NULL,threadFuncWrapper,(void *)this);
		return 1;
	}
	
	Result SysThread::stop(Bool fast)
	{
		if(theThread==0)
			return 0;
//		dump(8,"Stopping");

		terminateEvent.set();
		if(!fast)
		{
			if(finalEvent.wait(THREAD_TIMEOUT)!=WAIT_OBJECT_0)
			{
				pthread_detach(theThread);
			}
		}
		theThread=0;
//		dump(8,"Stopped");
		return 1;
	}
	
	Result SysThread::wait()
	{
		if(theThread && finalEvent.wait(WAIT_INFINITE)!=WAIT_OBJECT_0)
			return 0;
		return 1;
	}

	UInt32 ExecHelper::exec(const char *cmd)
	{
		// Not optimally elegant
		pid_t p=fork();
		if(p==0)
		{
			char cmdLine[1024];
			strcpy(cmdLine,cmd);
			char *args[128];
			args[0]=strtok(cmdLine," ");
			int ac=1;
			while((args[ac++]=strtok(NULL," "))!=NULL);
//			printf("Executing %s",args[0]);
			execv(args[0],args);
		}
		else if(p==-1)
			return 0;
		return 1;
	}


	std::string ExecHelper::getExecutableName()
	{
		return executableName;
	}

	void ExecHelper::kill(UInt32 pid)
	{
		::kill(pid,SIGKILL);
	}

#endif

	SocketHelper socketHelper;

#define LOCALADDRESS 0x7f000001

	SocketHelper::SocketHelper()
	{
		localName="localhost";
		localAddress=LOCALADDRESS;

#ifdef DPS_PLATFORM_WIN32
		WORD wVersionRequested;
		WSADATA wsaData;
		
		wVersionRequested = MAKEWORD( 2,0 );
		
		WSAStartup( wVersionRequested, &wsaData );
#endif

		// Get a valid address for this host
		char tempLocalName[128];
		gethostname(tempLocalName,128);
		localName=tempLocalName;
		
		hostent *he;
		he=gethostbyname(tempLocalName);
		if(he==NULL)
		{
			// TODO: Make this somewhat cleaner
			printf("Could not resolve %s",localName.c_str());
			localAddress=LOCALADDRESS;
			return;
		}

		Int32 i;
		for(i=0;he->h_addr_list[i];i++);
		Int32 addressIndex=0;
		// TODO: Reimplement multiple network adapter use properly
/*		Int32 addressCount=i;
		if(addressCount>1)
		{
			baseDump(7,"Network","Multiple adapters:");
			for(i=0;he->h_addr_list[i];i++)
				baseDump(7,"Network"," %s",formatIP(*((UInt32 *)he->h_addr_list[i])));
			i=atoi(params->getValue("net","0"));
			i=0;
			baseDump(7,"Network","Using address %s",formatIP(*((UInt32 *)he->h_addr_list[i])));
			return *((UInt32 *)he->h_addr_list[i]);
		}
		baseDump(7,"Network","Using address %s",formatIP(*((UInt32 *)he->h_addr)));
*/		
		localAddress=ntohl(*((UInt32 *)he->h_addr_list[addressIndex]));
	}
	
	SocketHelper::~SocketHelper()
	{
#ifdef DPS_PLATFORM_WIN32
		WSACleanup();
#endif
	}

	std::string SocketHelper::formatIPAddress(UInt32 addr)
	{
		static char buf[32];
		sprintf(buf,"%u.%u.%u.%u",(addr&0xff000000)>>24,(addr&0xff0000)>>16,(addr&0xff00)>>8,(addr&0xff));
		return buf;
	}

	TCPIPAddress SocketHelper::resolve(const char *name, UInt16 defPort)
	{
		TCPIPAddress address;

		// TODO: Remove potential buffer overflow
		char buf[128];
		strcpy(buf,name);
		address.port=defPort;
		char *dp=strchr(buf,':');
		if(dp)
		{
			address.port=atoi(&dp[1]);
			*dp=0;
		}
		hostent *he;
		he=gethostbyname(buf);
		if(he==NULL)
		{
			Int32 a,b,c,d;
			if(sscanf(buf,"%d.%d.%d.%d",&a,&b,&c,&d)==4)
				address=d+(c<<8)+(b<<16)+(a<<24);
		}
		else
			address.address=ntohl(*((UInt32 *)he->h_addr));

		return address;
	}


	std::string TCPIPAddress::format() const
	{
		char buf[1024];
		hostent *he;
		UInt32 a2=htonl(address);
		he=gethostbyaddr((const char *)&a2,4,AF_INET);
		if(he==NULL)
			sprintf(buf,"%s:%u",SocketHelper::formatIPAddress(address).c_str(),port);
		else
//			sprintf(buf,"%s(%u.%u.%u.%u):%u",he->h_name,address&0xff,(address>>8)&0xff,(address>>16)&0xff,(address>>24)&0xff,port);
			sprintf(buf,"%s:%u",he->h_name,port);
		return buf;
	}

	TCPSocket::TCPSocket()
	{
		sock=SOCKET_ERROR;
	}

	TCPSocket::~TCPSocket()
	{
	}

	Result TCPSocket::disconnect()
	{
		if(sock!=SOCKET_ERROR)
			closesocket(sock);
		sock=SOCKET_ERROR;
		return 1;
	}

	bool TCPSocket::noDelay = true;

	void TCPSocket::setOptions()
	{
		Int32 bl=262144;
		setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&bl,4);
		setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&bl,4);
		if(noDelay)
		{
			Int32 en=1;
			setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&en,4);
		}
	}


	Result TCPSocket::connect(SOCKET s, sockaddr_in *a)
	{
		sock=s;
		if(a)
		{
			address.address=ntohl(a->sin_addr.s_addr);
			address.port=ntohs(a->sin_port);
			name=address.format();
		}
		setOptions();
		return 1;
//		int bl=1048576;
//		setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&bl,4);
//		Bool en=true;
//		if(!params->isSet("tcpdelay"))
//			setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&en,sizeof(Bool));
	}
	
	Result TCPSocket::connect(const AnsiChar *ad, UInt16 defPort)
	{
		char buf[128];
		strcpy(buf,ad);
		address.port=defPort;
		char *dp=strchr(buf,':');
		if(dp)
		{
			address.port=atoi(&dp[1]);
			*dp=0;
		}
		hostent *he;
		he=gethostbyname(buf);
		if(he==NULL)
		{
			Int32 a,b,c,d;
			if(sscanf(buf,"%d.%d.%d.%d",&a,&b,&c,&d)!=4)
				return 0;
			address.address=d+(c<<8)+(b<<16)+(a<<24);
		}
		else
			address.address=ntohl(*((UInt32 *)he->h_addr));
		name=address.format();
		
		sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		if(sock==(SOCKET)SOCKET_ERROR)
			return 0;
		
		sockaddr_in addr;
		addr.sin_family=AF_INET;
		addr.sin_port=htons(address.port);
		addr.sin_addr.s_addr=htonl(address.address);
		if(::connect(sock,(sockaddr *)&addr,sizeof(addr))==SOCKET_ERROR)
		{
			SysThread::sleep(100);
			if(::connect(sock,(sockaddr *)&addr,sizeof(addr))==SOCKET_ERROR)
			{
				return 0;
			}
		}

		setOptions();
//		int bl=1048576;
//		setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&bl,4);
//		Bool en=true;
//		if(!params->isSet("tcpdelay"))
//			setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&en,sizeof(Bool));
		return 1;
	}

	Result TCPSocket::connect(const TCPIPAddress& addr)
	{
		sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		if(sock==(SOCKET)SOCKET_ERROR)
			return 0;

		address=addr;
		name=address.format();

		sockaddr_in saddr;
		saddr.sin_family=AF_INET;
		saddr.sin_port=htons(address.port);
		saddr.sin_addr.s_addr=htonl(address.address);
		if(::connect(sock,(sockaddr *)&saddr,sizeof(saddr))!=SOCKET_ERROR)
		{
			setOptions();
			return 1;
		}


		return 0;
	}

	
	Result TCPSocket::connect(UInt32 ipa, UInt16 prt, UInt32 retries)
	{
		sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		if(sock==(SOCKET)SOCKET_ERROR)
			return 0;
		address.port=prt;
		address.address=ipa;
		name=address.format();

		sockaddr_in addr;
		addr.sin_family=AF_INET;
		addr.sin_port=htons(address.port);
		addr.sin_addr.s_addr=htonl(address.address);
		UInt32 r;
		for(r=0;r<retries;r++)
		{
			if(::connect(sock,(sockaddr *)&addr,sizeof(addr))!=SOCKET_ERROR)
				break;
			SysThread::sleep(100);
		}
		if(r>=retries)
			return 0;

		setOptions();
//		Bool en=true;
//		if(!params->isSet("tcpdelay"))
//			setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&en,sizeof(Bool));
		return 1;
	}
	
	
	Int32 TCPSocket::psend(const void *data, Int32 len)
	{
		return ::send(sock,(const char *)data,len,0);
	}

	Int32 TCPSocket::send(const void *data, Int32 len)
	{
#ifdef DPS_SEGMENT_PACKETS
		for(Int32 cp=0;cp<len;cp+=DPS_SEGMENT_SIZE)
		{
			if(psend(&((const char *)data)[cp],min(len-cp,DPS_SEGMENT_SIZE))==SOCKET_ERROR)
				return SOCKET_ERROR;
		}
		return len;
#else
		return psend(data,len);
#endif
	}
	
	Int32 TCPSocket::precv(void *data, Int32 len)
	{
		return ::recv(sock,(char *)data,len,0);
	}
	
	Int32 TCPSocket::recv(void *data, Int32 len)
	{
		Int32 tc=0;
		char *b=(char *)data;
		
		while(tc<len)
		{
#ifdef DPS_SEGMENT_PACKETS
			Int32 bc=precv(&b[tc],min(len-tc,DPS_SEGMENT_SIZE));
#else
			Int32 bc=precv(&b[tc],len-tc);
#endif
			if(bc==0 || bc==SOCKET_ERROR)
				break;
			tc+=bc;
		}
		if(tc<len)
		{
			return SOCKET_ERROR;
		}
		return len;
	}

	Int32 TCPSocket::skip(Int32 len)
	{
		char tmp[1024];
		Int32 rl=len;

		while(rl>0)
		{
			recv(tmp,std::min(rl,1024));
			rl-=1024;
		}

		return len;
	}


	Int32 TCPSocket::available(int to)
	{
		fd_set ts;
		TIMEVAL tv;
		tv.tv_sec = 0;
		tv.tv_usec = to*1000;
		int rv;
		do 
		{
			errno = 0;
			FD_ZERO(&ts);
			FD_SET(sock,&ts);
			rv = select(sock+1, &ts, NULL, NULL, &tv);
		} 
		while ( errno == EINTR );
		return rv;
	}



	TCPListenSocket::TCPListenSocket(IIncomingConnection *target, UInt16 prt, UInt32 address) : THREAD_CONS(listenThread)
	{
		trg=target;

		sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
		if(sock==INVALID_SOCKET)
			return;

		Int32 en=1;
		setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&en,sizeof(Int32));
		
		sockaddr_in addr;
		addr.sin_family=AF_INET;
		addr.sin_port=htons(prt);
		addr.sin_addr.s_addr=htonl(address);
		dump(1,"Binding %s:%d",SocketHelper::formatIPAddress(address).c_str(),prt);
		if(bind(sock,(sockaddr *)&addr,sizeof(addr))==SOCKET_ERROR)
		{
			perror("Could not bind socket");
			return;
		}

		socklen_t as=sizeof(addr);
		getsockname(sock,(sockaddr*)&addr,&as);

		if(addr.sin_addr.s_addr==INADDR_ANY)
			addr.sin_addr.s_addr=htonl(socketHelper.getLocalAddress());

		this->address.address=ntohl(addr.sin_addr.s_addr);
		this->address.port=ntohs(addr.sin_port);
		
		if(listen(sock,SOMAXCONN)==SOCKET_ERROR)
			return;
		
		name=this->address.format();
	}
	
	TCPListenSocket::~TCPListenSocket()
	{
		stop();
		closesocket(sock);
	}
	
	Int32 TCPListenSocket::run()
	{
		dump(0,"Listening on %s",name.c_str());
		fd_set ts;
		while(!listenThread.shouldStop())
		{
			FD_ZERO(&ts);
			FD_SET(sock,&ts);
			TIMEVAL tv={1,0};
			if(select(sock+1,&ts,NULL,NULL,&tv)==1)
			{
				sockaddr_in address;
				socklen_t as=sizeof(address);
				SOCKET ns=accept(sock,(sockaddr *)&address,&as);
				if(ns!=(SOCKET)SOCKET_ERROR)
				{
					TCPSocket s;
					s.connect(ns,&address);
					dump(1,"Inbound connection from %s",s.getName());
					trg->incomingConnection(s);
				}
				else
					return 0;
			}
		}
		return 0;
	}

	StringMap::StringMap(Int32 argc, char *argv[])
	{
		for(Int32 i=1;i<argc;i++)
		{
			if(argv[i][0]=='-')
			{
				Ptr<StringPair> pa=new StringPair;
				pa->name=&argv[i][1];
				if(i<argc-1 && argv[i+1][0]!='-')
				{
					pa->value=argv[i+1];
					i++;
				}
				pairs.add(pa);
			}
		}
	}
	
	StringMap::StringMap(const char *fname)
	{
		if(!fname)
			return;
		FILE *fptr=fopen(fname,"rb");
		if(fptr)
		{
			char buf[1024];
			while(fgets(buf,1024,fptr))
			{
				char *n=strtok(buf,":");
				char *v=strtok(NULL,"\r\n");
				Ptr<StringPair> p=new StringPair;
				p->name=n;
				p->value=v;
				pairs.add(p);
			}
			fclose(fptr);
		}
	}


	
}
