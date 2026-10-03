// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "SaveSnapshot.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Version.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#ifndef WIN32
#include <sys/resource.h>
#endif

GlobalContainer* globalContainer = nullptr;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }

// std::quoted is a C++ literal formatter, not a JSON encoder: filenames can
// contain tabs, newlines and other control bytes that JSON must escape.
static std::string jsonString(const std::string& value)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
        {
            result += '\\';
            result += char(byte);
        }
        else if (byte < 0x20)
        {
            result += "\\u00";
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
        else result += char(byte);
    }
    result += '"';
    return result;
}

class MeasuredStream : public GAGCore::BinaryOutputStream
{
    struct Entry { std::string name; size_t start; };
    std::vector<Entry> stack;
public:
    struct Section { std::string name; size_t start,end; };
    std::vector<Section> sections;
    explicit MeasuredStream(GAGCore::StreamBackend* b):BinaryOutputStream(b) {}
    void writeEnterSection(const std::string name) override { stack.push_back({name,getPosition()}); }
    void writeEnterSection(unsigned id) override { writeEnterSection(std::to_string(id)); }
    void writeLeaveSection(size_t count=1) override
    {
        while(count--)
        {
            if(stack.empty()) throw std::runtime_error("Unbalanced save sections");
            auto e=stack.back();
            if(stack.size()==2 && stack.front().name=="Game") sections.push_back({e.name,e.start,getPosition()});
            stack.pop_back();
        }
    }
};

int main(int argc,char** argv)
try
{
    if(argc!=3) throw std::runtime_error("Usage: SaveSizeHarness input.map[.gz]|input.game[.gz] output-prefix (use a disposable GLOB2_USER_DATA_DIR)");
    GlobalContainer globals; globalContainer=&globals;
    globals.runNoX=true; globals.settings.rememberUnit=false;
    globals.buildingsTypes.init(); IntBuildingType::init(); Race::loadDefault();
    GameGUI gui;
    auto start=Clock::now();
    GAGCore::BinaryInputStream input(globals.fileManager->openInflatingInputStreamBackend(argv[1]));
    if(!gui.load(&input,false)) throw std::runtime_error("Cannot load input");
    const double loadMs=ms(start);
    const bool isMap=!gui.game.mapHeader.getIsSavedGame();
    auto* memory=new GAGCore::MemoryStreamBackend;
    MeasuredStream output(memory);
    start=Clock::now();
    double captureMs=0, encodeMs=0;
    std::string bytes;
    if(std::getenv("GLOB2_BENCH_SNAPSHOT")) {
        auto encode=captureSave([&](GAGCore::OutputStream* stream,DeferredGameSHA1* sha) {
            if(isMap) gui.game.save(stream,true,"Size fixture",sha); else gui.save(stream,"Size fixture",sha);
        });
        captureMs=ms(start);start=Clock::now();
        GAGCore::ChunkedBuffer chunks; if(!encode(chunks).run()) throw std::runtime_error("Snapshot failed");encodeMs=ms(start);
        bytes.resize(chunks.size());chunks.readAt(0,bytes.data(),bytes.size());
    } else {
        if(isMap) gui.game.save(&output,true,"Size fixture"); else gui.save(&output,"Size fixture");
        captureMs=ms(start);bytes=memory->takeContents();
    }
    const double saveMs=captureMs+encodeMs;
    std::string gzip;
    start=Clock::now();
    if(!GAGCore::gzipCompress(bytes,6,gzip)) throw std::runtime_error("Cannot compress save");
    const double gzipMs=ms(start);
    const std::string prefix=argv[2];
    start=Clock::now();
    std::ofstream file(prefix+(isMap?".map.gz":".game.gz"),std::ios::binary);
    file.write(gzip.data(),gzip.size()); file.close(); if(!file) throw std::runtime_error("Cannot write output");
    const double writeMs=ms(start);
    std::ofstream report(prefix+".json");
    report << "{\"version\":" << VERSION_MINOR << ",\"input\":" << jsonString(argv[1])
           << ",\"raw_bytes\":" << bytes.size() << ",\"gzip_bytes\":" << gzip.size()
           << ",\"write_ms\":" << writeMs << ",\"total_ms\":" << saveMs+gzipMs+writeMs
           << ",\"capture_ms\":" << captureMs << ",\"encode_ms\":" << encodeMs
           << ",\"load_ms\":" << loadMs << ",\"serialize_ms\":" << saveMs << ",\"compress_ms\":" << gzipMs;
#ifndef WIN32
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    report << ",\"process_peak_rss_bytes\":" << uint64_t(usage.ru_maxrss)
#ifndef __APPLE__
        *1024
#endif
        ;
#endif
    report << ",\"sections\":[";
    bool first=true;
    for(const auto& section:output.sections)
    {
        if(section.end<section.start) continue; // Header backpatch.
        std::string packed;
        if(!GAGCore::gzipCompress(bytes.substr(section.start,section.end-section.start),6,packed)) throw std::runtime_error("Section compression failed");
        if(!first) report << ','; first=false;
        report << "{\"name\":" << jsonString(section.name) << ",\"raw_bytes\":" << section.end-section.start << ",\"gzip_bytes\":" << packed.size() << '}';
    }
    report << "]}\n";
    if(!report) throw std::runtime_error("Cannot write report");
    std::cout << prefix << ": " << bytes.size() << " raw, " << gzip.size() << " gzip bytes\n";
    return 0;
}
catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
