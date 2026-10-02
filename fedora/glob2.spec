Name:           glob2
Version:        0.9.5.4
Release:        1%{?dist}
Summary:        Real time strategy game with automatic unit task assignment
License:        GPL-3.0-or-later
URL:            https://globulation2.org/
Source0:        https://github.com/Globulation2/glob2/releases/download/v%{version}/glob2-%{version}.tar.gz

Source1:        https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
Source2:        https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.6/SDL3_image-3.4.6.tar.gz
Source3:        https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz
Source4:        https://github.com/libsdl-org/SDL_net/releases/download/release-3.2.0/SDL3_net-3.2.0.tar.gz
Source5:        https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
Source6:        https://files.pythonhosted.org/packages/18/5d/3bf57dcd21979b887f014ea83c24ae194cfcd12b9e0fda66b957c69d1fca/setuptools-80.9.0.tar.gz
Source7:        https://files.pythonhosted.org/packages/a5/98/9118a0659646f1628c592ef9bb48e0056efa6bf27c951fd12a178e0136fb/pybind11-3.0.2.tar.gz
Source8:        https://files.pythonhosted.org/packages/8c/21/c2bcdd5906101a30244eaffc1b6e6ce71a31bd0742a01eb89e660ebfac2d/pillow-12.2.0.tar.gz

BuildRequires:  cmake
BuildRequires:  make
BuildRequires:  python3-devel
BuildRequires:  python3-pip
BuildRequires:  python3-wheel
BuildRequires:  libjpeg-turbo-devel

BuildRequires:  gcc-c++
BuildRequires:  python3
BuildRequires:  scons
BuildRequires:  cmake
BuildRequires:  freetype-devel
BuildRequires:  libX11-devel
BuildRequires:  libXext-devel
BuildRequires:  libXcursor-devel
BuildRequires:  libXi-devel
BuildRequires:  libXrandr-devel
BuildRequires:  alsa-lib-devel
BuildRequires:  pulseaudio-libs-devel
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
python3 tools/build_asset_encoder.py build --sources "%{_sourcedir}" --output "$PWD/artifacts/asset-encoder"
mkdir -p build/sdl3/sources
cp %{SOURCE1} build/sdl3/sources/SDL3-3.4.16.tar.gz
cp %{SOURCE2} build/sdl3/sources/SDL3_image-3.4.6.tar.gz
cp %{SOURCE3} build/sdl3/sources/SDL3_ttf-3.2.2.tar.gz
cp %{SOURCE4} build/sdl3/sources/SDL3_net-3.2.0.tar.gz
export GLOB2_SDL3_PREFIX="$PWD/build/sdl3/prefix"
python3 scons/sdl3_dependencies.py --prefix "$GLOB2_SDL3_PREFIX" --work build/sdl3/sources --jobs 2
export RPM_PACKAGE_NAME=%{name} RPM_PACKAGE_VERSION=%{version} \
    RPM_PACKAGE_RELEASE=%{release} RPM_ARCH=%{_arch}
scons -j2 release=0 server=0 CXXFLAGS="%{optflags}" LINKFLAGS="%{build_ldflags}" \
    BINDIR=%{_bindir} INSTALLDIR=%{_datadir} DATADIR=%{_datadir}

%install
export GLOB2_ASSET_ENCODER_PYTHON="$PWD/artifacts/asset-encoder/venv/bin/python"
export GLOB2_SDL3_PREFIX="$PWD/build/sdl3/prefix"
export RPM_PACKAGE_NAME=%{name} RPM_PACKAGE_VERSION=%{version} \
    RPM_PACKAGE_RELEASE=%{release} RPM_ARCH=%{_arch}
scons -j2 release=0 server=0 CXXFLAGS="%{optflags}" LINKFLAGS="%{build_ldflags}" \
    BINDIR=%{buildroot}%{_bindir} \
    INSTALLDIR=%{buildroot}%{_datadir} DATADIR=%{_datadir} optimized_assets=1 install

%files
%license COPYING
%doc README.md
%{_bindir}/glob2
%{_prefix}/lib/glob2/
%{_datadir}/glob2/
%{_datadir}/applications/org.globulation2.Globulation2.desktop
%{_datadir}/metainfo/org.globulation2.Globulation2.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/glob2.png

%changelog
* Wed Sep 30 2026 Globulation 2 maintainers <glob2-devel@nongnu.org> - 0.9.5.4-1
- Prepare F-Droid Android candidate

* Wed Sep 30 2026 Globulation 2 maintainers <glob2-devel@nongnu.org> - 0.9.5.3-1
- Align the Fedora package recipe with the current source version

* Wed Sep 30 2026 Globulation 2 maintainers <glob2-devel@nongnu.org> - 0.9.5.2-1
- Prepare Windows desktop candidate and Epic packaging

* Wed Sep 30 2026 Globulation 2 maintainers <glob2-devel@nongnu.org> - 0.9.5.0-1
- Prepare package recipe for the next upstream release
