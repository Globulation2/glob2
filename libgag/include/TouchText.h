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
    for (size_t at=0;at<text.size();) {
        size_t end=at+1;
        while (end<text.size() && (static_cast<unsigned char>(text[end])&0xc0)==0x80) ++end;
        const auto next=text.substr(at,end-at);
        if (text[at]=='\n') { lines.push_back(line); line.clear(); at=end; continue; }
        if (!line.empty() && font->getStringWidth(line+next)>width) {
            const auto space=line.find_last_of(' ');
            if (space!=std::string::npos) { lines.push_back(line.substr(0,space));line.erase(0,space+1); }
            else { lines.push_back(line);line.clear(); }
        }
        line+=next;at=end;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}
}
