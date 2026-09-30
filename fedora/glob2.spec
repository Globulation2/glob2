Name:           glob2
Version:        0.9.5.3
Release:        1%{?dist}
Summary:        Real time strategy game with automatic unit task assignment
License:        GPL-3.0-or-later
URL:            https://globulation2.org/
Source0:        https://github.com/Globulation2/glob2/releases/download/v%{version}/glob2-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  python3
BuildRequires:  scons
BuildRequires:  SDL2-devel
BuildRequires:  SDL2_image-devel
BuildRequires:  SDL2_net-devel
BuildRequires:  SDL2_ttf-devel
BuildRequires:  libvorbis-devel
BuildRequires:  libogg-devel
BuildRequires:  speex-devel
BuildRequires:  boost-devel
BuildRequires:  openssl-devel
BuildRequires:  zlib-devel
BuildRequires:  fribidi-devel
BuildRequires:  pcre-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  libepoxy-devel

%description
Globulation 2 lets players assign units to tasks instead of directing each
unit individually. It includes computer opponents, a map editor, and
multiplayer play.

%prep
%autosetup -n glob2-%{version}

%build
export RPM_PACKAGE_NAME=%{name} RPM_PACKAGE_VERSION=%{version} \
    RPM_PACKAGE_RELEASE=%{release} RPM_ARCH=%{_arch}
scons -j2 release=0 server=0 CXXFLAGS="%{optflags}" LINKFLAGS="%{build_ldflags}" \
    BINDIR=%{_bindir} INSTALLDIR=%{_datadir} DATADIR=%{_datadir}

%install
export RPM_PACKAGE_NAME=%{name} RPM_PACKAGE_VERSION=%{version} \
    RPM_PACKAGE_RELEASE=%{release} RPM_ARCH=%{_arch}
scons -j2 release=0 server=0 CXXFLAGS="%{optflags}" LINKFLAGS="%{build_ldflags}" \
    BINDIR=%{buildroot}%{_bindir} \
    INSTALLDIR=%{buildroot}%{_datadir} DATADIR=%{_datadir} install

%files
%license COPYING
%doc README.md
%{_bindir}/glob2
%{_datadir}/glob2/
%{_datadir}/applications/org.globulation2.Globulation2.desktop
%{_datadir}/metainfo/org.globulation2.Globulation2.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/glob2.png

%changelog
* Wed Sep 30 2026 Globulation 2 maintainers <glob2-devel@nongnu.org> - 0.9.5.3-1
- Prepare package recipe for the next upstream release
