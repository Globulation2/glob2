Name: glob2
Summary:  Innovative Strategy Game Globulation 2
Version: 0.9.3
Release: 1
License: GPL Version 3
Group: Amusements/Games
URL: http://www.globulation2.org/
Source0: %{name}-%{version}.tar.gz
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: SDL,SDL_net,SDL_image,SDL_ttf,speex,libvorbis,libogg,zlib,boost,fribidi
BuildPrereq: scons,SDL-devel,SDL_net-devel,SDL_image-devel,SDL_ttf-devel,speex-devel,zlib-devel,boost-devel,fribidi-devel,libvorbis-devel,libogg-devel
%description
Globulation 2 brings a new type of gameplay to RTS games. The player chooses
the number of units to assign to various tasks, and the units do their best to
satisfy the requests. This allows players to manage more units and focus on
strategy rather than individual unit's jobs. Globulation 2 also features AI
allowing single-player games or any possible combination of human-computer
teams. Also included is a scripting language for versatile gameplay or
tutorials and an integrated map editor. Globulation2 can be played in single
player mode, through your local network, or over the Internet with Ysagoon
Online Gaming (or YOG for short).


%prep
%setup -q

%build
scons release=True INSTALLDIR="%{buildroot}/usr/share" DATADIR="/usr/share" BINDIR="%{buildroot}/usr/bin"

%install
rm -fr %{buildroot}
scons install

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root) 
/usr/bin/glob2
/usr/share/glob2/*
/usr/share/applications/glob2.desktop
/usr/share/icons/*

