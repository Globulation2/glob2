// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyMapThumbnail.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <chrono>
#include <iostream>
#include <cassert>
GlobalContainer* globalContainer=nullptr;
int main(int argc,char**argv)
{
    GlobalContainer globals("glob2-map-preview-baseline"); globalContainer=&globals;
    globals.runNoX=true; globals.settings.rememberUnit=false; globals.load();
    LegacyMapThumbnail missing; missing.loadFromMap("maps/does-not-exist-preview-regression.map");
    std::cout<<"BASELINE missing-file isLoaded="<<missing.isLoaded()<<" (expected false)\n";
    const std::string filename=argc>1?argv[1]:"maps/FourSquares1.map";
    auto start=std::chrono::steady_clock::now();
    for (int i=0;i<20;++i)
    {
        GAGCore::BinaryInputStream headerInput(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(filename));
        MapHeader header; assert(header.load(&headerInput));
        Game world(nullptr);
        GAGCore::BinaryInputStream input(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(filename));
        assert(world.load(&input));
        LegacyMapThumbnail image; image.loadFromMap(filename); assert(image.isLoaded());
    }
    auto end=std::chrono::steady_clock::now();
    std::cout<<"BASELINE 20 selections, original header + Game + thumbnail pipeline ms: "<<std::chrono::duration<double,std::milli>(end-start).count()<<"\n";
}
