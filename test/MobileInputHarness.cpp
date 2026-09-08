// SPDX-License-Identifier: GPL-3.0-or-later
#include <TouchInput.h>
#include <ResponsiveMenu.h>
#include <cstdio>
#include <stdexcept>
using namespace GAGCore;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void near(double a,double b) { require(std::abs(a-b)<0.000001,"Coordinate mismatch"); }
int main()
{
    try {
        ViewportTransform view({0,48,320,472},{4096,4096});
        view.moveTo({4090,4});
        auto at=view.screenToWorld({215,110});
        view.zoom(2.3,{215,110});
        auto after=view.screenToWorld({215,110});near(at.x,after.x);near(at.y,after.y);
        auto screen=view.worldToScreen(after);near(screen.x,215);near(screen.y,110);
        auto center=view.position();view.resize({0,48,568,224});
        near(center.x,view.position().x);near(center.y,view.position().y);
        view.zoom(100,{284,160});near(view.zoom(),3);
        view.zoom(0.01,{284,160});near(view.zoom(),0.5);
        view.pan({10000,-10000});require(view.position().x>=0&&view.position().x<4096,"Toroidal pan failed");
        for(auto dimensions : {ViewPoint{320,568},{568,320},{360,640},{640,360},{1024,768}})
            for(double scale : {1.0,1.5,2.0}) for(double keyboard : {0.0,260.0}) {
                auto layout=MobileLayout::calculate(dimensions.x,dimensions.y,{0,24,0,20},keyboard,scale,true);
                for(auto rect : {layout.status,layout.world,layout.actions,layout.panel}) {
                    require(rect.w>=0&&rect.h>=0,"Negative layout dimensions");
                    if(rect.w&&rect.h) require(rect.x>=0&&rect.y>=24&&rect.x+rect.w<=dimensions.x&&rect.y+rect.h<=dimensions.y-std::max(20.0,keyboard)+0.001,"Layout escapes safe area");
                }
                if(layout.persistentPanel) require(layout.world.w>=480,"Persistent panel crowds map");
            }
        for (auto size : {ViewPoint{320,568}, {568,320}, {360,640}, {640,360}, {768,1024}})
            for (double scale : {1.0, 1.5, 2.0}) for (double keyboard : {0.0, 200.0}) {
                ViewRect safe{12,24,size.x-24,size.y-48-keyboard};
                auto menu = ResponsiveMenu::calculate(safe, 10, 224*scale, 0, scale);
                for (size_t i=0; i<menu.buttons.size(); ++i) {
                    auto rect=menu.buttons[i];
                    require(rect.h>=48*scale && rect.w>=48, "Menu touch target too small");
                    require(rect.x>=safe.x && rect.x+rect.w<=safe.x+safe.w, "Menu exceeds safe width");
                }
                menu = ResponsiveMenu::calculate(safe,10,224*scale,100000,scale);
                auto last=menu.buttons.back();
                require(last.y+last.h<=menu.content.y+menu.content.h+0.001,"Last action cannot scroll into view");
                if (menu.content.h>=last.h)
                    require(menu.hit({last.x+last.w/2,last.y+last.h/2})==9,"Scrolled action hit mismatch");
                require(menu.hit({0,0})==-1,"Header or safe inset activates action");
            }
        TouchInput touch;
        require(touch.down(1,1,{20,20}).empty(),"Tap selected on down");
        require(touch.move(1,1,{25,20}).empty(),"Tap slop became drag");
        auto actions=touch.up(1,1,{25,20});require(actions.size()==1&&actions[0].kind==TouchActionKind::Select,"Tap not selected");
        touch.down(1,1,{20,20});actions=touch.move(1,1,{28,20});
        require(actions.size()==1&&actions[0].kind==TouchActionKind::Pan,"Threshold did not start pan");near(actions[0].point.x,8);
        require(touch.up(1,1,{28,20}).empty(),"Pan released as tap");
        touch.down(1,1,{10,10});touch.down(1,2,{30,10});actions=touch.move(1,2,{50,10});
        require(actions.size()==2&&actions[1].kind==TouchActionKind::Zoom,"Pinch missing");near(actions[1].factor,2);
        touch.up(1,2,{50,10});require(touch.move(1,1,{90,90}).empty(),"Remaining finger moved world");
        require(touch.up(1,1,{90,90}).empty(),"Pinch ended as tap");
        touch.setMode(TouchMode::Placement);actions=touch.down(1,1,{20,20});
        require(actions[0].kind==TouchActionKind::Preview,"Placement not a preview");
        require(touch.up(1,1,{20,20}).empty(),"Placement committed on release");
        touch.setMode(TouchMode::Paint);actions=touch.down(1,1,{20,20});
        require(actions[0].kind==TouchActionKind::BeginStroke,"Paint did not start");
        actions=touch.down(1,2,{40,40});require(actions[0].kind==TouchActionKind::EndStroke,"Pinch did not end paint");
        touch.cancel();require(touch.move(1,1,{30,30}).empty(),"Canceled finger still active");
        touch.setMode(TouchMode::Navigate);touch.down(1,1,{10,10});touch.down(2,1,{30,10});
        actions=touch.move(2,1,{40,10});require(actions.size()==2,"Device finger IDs collided");
        touch.down(1,3,{20,20});require(touch.move(2,1,{50,10}).empty(),"Third finger failed to cancel");
        std::puts("PASS mobile geometry/input: zoom anchoring, seams, rotation, safe layouts, gestures, cancellation");
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
