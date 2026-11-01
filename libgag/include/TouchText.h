// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <vector>
#include <string>
namespace GAGCore {
inline std::vector<std::string> wrapTouchText(Font* font,const std::string& text,double width) {
    std::vector<std::string> lines;
    std::string line;
    width=std::max(1.0,width);
    for (size_t paragraph=0;paragraph<text.size();) {
        const auto newline=text.find('\n',paragraph);
        const auto run=text.substr(paragraph,newline==std::string::npos ? newline : newline-paragraph);
        auto ends=font->getStringClusterEnds(run);
        size_t previous=0;
        bool valid=!ends.empty() || run.empty();
        for (const auto end:ends) {
            if (end<=previous || end>run.size() ||
                (end<run.size() && (static_cast<unsigned char>(run[end])&0xc0)==0x80)) {
                valid=false;break;
            }
            previous=end;
        }
        valid=valid && previous==run.size();
        // Fonts without shaped clusters retain their UTF-8 codepoint policy;
        // malformed boundaries must never split an encoded character.
        if (!valid) {
            ends.clear();
            for (size_t at=0;at<run.size();) {
                size_t end=at+1;
                while (end<run.size() && (static_cast<unsigned char>(run[end])&0xc0)==0x80) ++end;
                ends.push_back(end);at=end;
            }
        }
        size_t at=0;
        for (const auto end:ends) {
            const auto next=run.substr(at,end-at);
            if (!line.empty() && font->getStringWidth(line+next)>width) {
                const auto space=line.find_last_of(' ');
                if (space!=std::string::npos) { lines.push_back(line.substr(0,space));line.erase(0,space+1); }
                else { lines.push_back(line);line.clear(); }
            }
            line+=next;at=end;
        }
        if (newline==std::string::npos) break;
        lines.push_back(line);line.clear();paragraph=newline+1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}
}
