// SPDX-License-Identifier: GPL-3.0-or-later
#include "MobilePaths.h"
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
using Stream=std::unique_ptr<SDL_RWops,decltype(&SDL_RWclose)>;
std::string readAsset(const char* name)
{
    Stream stream(SDL_RWFromFile(name,"rb"),SDL_RWclose);
    if(!stream) throw std::runtime_error(std::string("Cannot open packaged asset: ")+name);
    auto size=SDL_RWsize(stream.get());
    if(size<0 || size>1024*1024) throw std::runtime_error("Invalid asset index size");
    std::string contents(size,'\0');
    if(SDL_RWread(stream.get(),contents.data(),1,contents.size())!=contents.size())
        throw std::runtime_error("Cannot read asset index");
    return contents;
}
}
void initializeMobilePaths()
{
    std::unique_ptr<char,decltype(&SDL_free)> writable(SDL_GetPrefPath("Globulation2","glob2"),SDL_free);
    if(!writable) throw std::runtime_error(SDL_GetError());
    std::filesystem::path root(writable.get());
    std::filesystem::create_directories(root);
    SDL_setenv("GLOB2_USER_DATA_DIR",root.string().c_str(),1);
#ifdef __ANDROID__
    std::istringstream index(readAsset("glob2-bundle/index.list"));
    std::string version;
    std::getline(index,version);
    if(version.size()!=64 || version.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid asset bundle identity");
    auto assets=root/"assets"/version;
    std::ifstream marker(assets/".complete");
    std::string installedVersion;std::getline(marker,installedVersion);
    if(installedVersion!=version) {
        std::filesystem::create_directories(assets);
        std::string name;
        std::vector<char> buffer(64*1024);
        while(std::getline(index,name)) {
            std::filesystem::path relative(name);
            if(name.empty() || relative.is_absolute()) throw std::runtime_error("Invalid packaged asset path");
            for(const auto& part:relative) if(part==".." || part==".") throw std::runtime_error("Invalid packaged asset path");
            Stream source(SDL_RWFromFile(("glob2-bundle/"+name).c_str(),"rb"),SDL_RWclose);
            if(!source) throw std::runtime_error("Missing packaged asset: "+name);
            auto destination=assets/relative;
            std::filesystem::create_directories(destination.parent_path());
            auto temporary=destination;temporary+=".tmp";
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            auto remaining=SDL_RWsize(source.get());
            if(remaining<0) throw std::runtime_error("Cannot read asset size");
            while(remaining>0) {
                auto count=SDL_RWread(source.get(),buffer.data(),1,std::min<Sint64>(remaining,buffer.size()));
                if(!count) throw std::runtime_error("Truncated packaged asset: "+name);
                output.write(buffer.data(),count);remaining-=count;
            }
            output.close();
            if(!output) throw std::runtime_error("Cannot install packaged asset: "+name);
            std::filesystem::rename(temporary,destination);
        }
        std::ofstream completed(assets/".complete");completed<<version;completed.close();
        if(!completed) throw std::runtime_error("Cannot commit asset installation");
    }
    SDL_setenv("GLOB2_ASSET_DIR",assets.string().c_str(),1);
#else
    std::unique_ptr<char,decltype(&SDL_free)> assets(SDL_GetBasePath(),SDL_free);
    if(!assets) throw std::runtime_error(SDL_GetError());
    SDL_setenv("GLOB2_ASSET_DIR",assets.get(),1);
#endif
}
