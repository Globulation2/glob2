// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <vector>

namespace GAGCore {
// Lay out variable-height rows while preserving a reachable fixed last action.
// Oversized footer groups join the scrollable content instead of obscuring it.
struct ResponsiveDialog {
    struct Row { ViewRect rect; bool footer; };
    ViewRect content;
    std::vector<Row> rows;
    double maximum=0, offset=0;
    template<class Height>
    static ResponsiveDialog calculate(ViewRect safe, const std::vector<bool>& footer,
                                       Height height, double requestedOffset=0, double unit=1) {
        ResponsiveDialog out;
        const double gap=8*unit, width=std::max(0.0,std::min(safe.w-2*gap,640*unit));
        const double x=safe.x+(safe.w-width)/2;
        int columns=1;
        double cell=width;
        auto fixed=footer;
        auto footerRows=[&]() {
            columns=width>=320*unit && std::count(fixed.begin(),fixed.end(),true)>1 ? 2 : 1;
            cell=(width-(columns-1)*gap)/columns;
            std::vector<double> result;int n=0;
            for(size_t i=0;i<fixed.size();++i) if(fixed[i]) {
                if(n%columns==0) result.push_back(0);
                result.back()=std::max(result.back(),height(i,cell));++n;
            }
            return result;
        };
        auto total=[&](const std::vector<double>& heights) {
            double sum=0;for(double h:heights) sum+=h+gap;return sum;
        };
        auto heights=footerRows();
        for(size_t i=0;i<fixed.size() && total(heights)>std::max(48*unit,safe.h/2);++i) {
            if(!fixed[i] || std::count(fixed.begin(),fixed.end(),true)<=1) continue;
            fixed[i]=false;heights=footerRows();
        }
        const double footerHeight=total(heights);
        out.content={x,safe.y+gap,width,std::max(0.0,safe.h-footerHeight-2*gap)};
        double y=0;
        for(size_t i=0;i<fixed.size();++i) {
            const double h=height(i,fixed[i] ? cell : width);
            out.rows.push_back({{x,y,width,h},fixed[i]});
            if(!fixed[i]) y+=h+gap;
        }
        out.maximum=std::max(0.0,y-out.content.h);
        out.offset=std::clamp(requestedOffset,0.0,out.maximum);
        int n=0;double bottom=safe.y+safe.h-footerHeight;
        for(auto& row:out.rows) {
            if(row.footer) {
                if(n && n%columns==0) bottom+=heights[n/columns-1]+gap;
                row.rect={x+(n%columns)*(cell+gap),bottom,cell,heights[n/columns]};++n;
            } else row.rect.y+=out.content.y-out.offset;
        }
        return out;
    }
};
}
