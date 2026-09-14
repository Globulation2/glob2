// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace GAGCore;

void SettingsScreen::buildGeneral()
{
    auto& s=globalContainer->settings;
    if(current==Category::Display) {
        info(tr("Choose how the game looks on your screen."));
        section("Display");
        choice("display.mode","Window mode","Choose a window or fill the screen.",bool(s.screenFlags & GraphicContext::FULLSCREEN),
            {tr("Windowed"),tr("Fullscreen")},[this](int v){changeDisplay([v](Settings& s){
                if(v){s.screenFlags|=GraphicContext::FULLSCREEN;s.screenFlags&=~GraphicContext::RESIZABLE;}
                else{s.screenFlags&=~GraphicContext::FULLSCREEN;s.screenFlags|=GraphicContext::RESIZABLE;}
            });});
        const auto modes=globalContainer->gfx->listVideoModes();
        std::vector<std::pair<int,int>> sizes;
        std::vector<std::string> names;
        std::vector<bool> windowOnly;
        auto append=[&](int w,int h,bool restricted){
            if(std::find(sizes.begin(),sizes.end(),std::make_pair(w,h))!=sizes.end())return;
            sizes.emplace_back(w,h);windowOnly.push_back(restricted);
            names.push_back(std::to_string(w)+" × "+std::to_string(h)+(restricted?" — "+tr("Windowed only"):""));
        };
        for(auto m:modes)append(m.w,m.h,false);
        for(auto size:std::vector<std::pair<int,int>>{{640,480},{800,600},{1024,768},{1280,1024},{1600,1200}})append(size.first,size.second,true);
        append(s.screenWidth,s.screenHeight,true);
        int selected=std::find(sizes.begin(),sizes.end(),std::make_pair(s.screenWidth,s.screenHeight))-sizes.begin();
        choice("display.resolution","Resolution","Window-only sizes also switch the game to windowed mode.",selected,names,
            [this,sizes,windowOnly](int v){if(v<0 || v>=int(sizes.size()))return;changeDisplay([=](Settings& s){
                s.screenWidth=sizes[v].first;s.screenHeight=sizes[v].second;
                if(windowOnly[v]){s.screenFlags&=~GraphicContext::FULLSCREEN;s.screenFlags|=GraphicContext::RESIZABLE;}
            });});
        form.back().value=std::to_string(s.screenWidth)+" × "+std::to_string(s.screenHeight);
        {
            // 0 follows the desktop; the rest are the scales desktops actually offer.
            static const int percents[]={0,100,125,150,175,200,250,300};
            const float desktop=GraphicContext::querySystemUiScale();
            const int desktopPercent=int(std::lround(std::max(1.0f,desktop>0.0f?desktop:1.0f)*100));
            std::vector<std::string> labels;
            for(int p:percents)
                labels.push_back(p ? std::to_string(p)+" %"
                    : tr("Match the desktop")+" ("+std::to_string(desktopPercent)+" %)");
            int selected=std::find(std::begin(percents),std::end(percents),s.uiScale)-std::begin(percents);
            if(selected>=int(std::size(percents)))selected=0;
            choice("display.uiscale","Interface scale",
                "Enlarge menus, text and the sidebar on a high-resolution screen.",selected,labels,
                [this](int v){if(v>=0 && v<int(std::size(percents)))changeUiScale(percents[v]);});
        }
        info(tr("Current display")+": "+std::to_string(globalContainer->gfx->getW())+" × "+std::to_string(globalContainer->gfx->getH())+
             " · "+((globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)?"OpenGL":tr("Software"))+
             " · "+tr(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN?"Fullscreen":"Windowed"));
        if(restartRequired()) info(tr("Saved — restart required"));
        if(displayError)info(tr("Could not change display mode. The previous mode was restored."));
        section("Artwork & effects");
        choice("graphics.detail","Graphics detail","Reduced detail disables clouds and their shadows, simplifies magic effects, and reduces transparency.",
            bool(s.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX),{tr("Full"),tr("Reduced")},[this](int v){
                auto& flags=globalContainer->settings.optionFlags;
                if(v)flags|=GlobalContainer::OPTION_LOW_SPEED_GFX;else flags&=~GlobalContainer::OPTION_LOW_SPEED_GFX;commit();
            });
        toggle("graphics.artwork","High-resolution artwork","Apply artwork on the next game or editor load (OpenGL).",s.highResolutionArtwork,[this](int v){globalContainer->settings.highResolutionArtwork=v;commit();});
        toggle("graphics.torus","Automatic torus view","Automatically show the torus overview while moving around the map (OpenGL).",s.automaticTorus,[this](int v){globalContainer->settings.automaticTorus=v;commit();});
        choice("graphics.renderer","Renderer","Changing the renderer requires a restart.",bool(s.screenFlags & GraphicContext::USEGPU),
            {tr("Software"),"OpenGL"},[this](int v){changeDisplay([v](Settings& s){if(v)s.screenFlags|=GraphicContext::USEGPU;else s.screenFlags&=~GraphicContext::USEGPU;});});
#ifndef HAVE_OPENGL
        form.back().enabled=false;
        form.back().help=tr("OpenGL is not available in this build.");
#endif
    } else if(current==Category::Audio) {
        info(tr("Adjust music and voice volume."));
        toggle("audio.mute","Mute audio","Keep your volume levels while silencing audio.",s.mute,[this](int v){
            auto& s=globalContainer->settings;s.mute=v;
            globalContainer->mix->setVolume(s.musicVolume,s.voiceVolume,s.mute);commit();
        });
        for(int voice=0;voice<2;++voice){
            auto& r=add(voice?"audio.voice":"audio.music",Kind::Slider,tr(voice?"Voice volume":"Music volume"));
            r.number=voice?s.voiceVolume:s.musicVolume;r.maximum=256;r.enabled=!s.mute;
            r.value=std::to_string((r.number*100+128)/256)+"%";
            r.change=[this,voice](int v){auto& s=globalContainer->settings;
                (voice?s.voiceVolume:s.musicVolume)=std::clamp(v,0,256);
                globalContainer->mix->setVolume(s.musicVolume,s.voiceVolume,s.mute);commit(true);
            };
        }
    } else if(current==Category::Gameplay) {
        info(tr("Adjust the pace of play."));
        std::vector<std::string> labels;
        Settings copy=s;
        for(int i=0;i<=Settings::GAME_SPEED_MAXIMUM;++i){copy.gameSpeed=i;labels.push_back(copy.getGameSpeedText());}
        choice("gameplay.speed","Game speed","Single-player and replays only. Multiplayer runs at 1x.",s.gameSpeed,labels,[this](int v){
            globalContainer->settings.gameSpeed=std::clamp(v,0,int(Settings::GAME_SPEED_MAXIMUM));commit();
        });
        toggle("gameplay.autosave","Autosave","Save the game automatically about every 10 seconds.",s.autosaveGames,[this](int v){globalContainer->settings.autosaveGames=v;commit();});
    } else if(current==Category::Player) {
        info(tr("Set your language and player name."));
        auto* strings=Toolkit::getStringTable();std::vector<std::string> labels;
        for(int i=0;i<strings->getNumberOfLanguage();++i){
            auto label=strings->getStringInLang(strings->isLangComplete(i)?"[language]":"[language incomplete]",i);
            if(!Toolkit::getFont("standard")->hasGlyphsFor(label))label=strings->getStringInLang("[language-code]",i)+" — "+tr("Missing font");
            labels.push_back(label);
        }
        choice("player.language","Language","Language used throughout the interface.",strings->getLang(),labels,[this](int v){
            auto* strings=Toolkit::getStringTable();strings->setLang(v);
            globalContainer->settings.language=strings->getStringInLang("[language-code]",v);commit();
        });
        auto& r=add("player.name",Kind::Text,tr("Player name"),tr("Name shown to other players."));
        r.value=editingText?textDraft:s.getUsername();
    }
}
