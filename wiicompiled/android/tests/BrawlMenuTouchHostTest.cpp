// SPDX-License-Identifier: GPL-3.0-only
#include "../../runtime/src/android/brawl_menu_touch.h"
#include <cassert>
#include <limits>

using BrawlMenuTouch::Detail::Point;
using BrawlMenuTouch::Detail::Unproject;
using BrawlMenuTouch::Detail::InTriangle;

static bool Near(double a, double b) { return std::abs(a - b) < 1e-7; }

int main() {
    const std::array<Point, 4> viewport{{{320,240}, {420,240}, {320,140}, {420,140}}};
    Point result;
    assert(Unproject(viewport, {370,215}, result));
    assert(Near(result.x, .5) && Near(result.y, .25));
    // Menus can move their camera. Exercise a perspective plane rather than
    // testing only the convenient orthographic/axis-aligned special case.
    auto project = [](double x, double y) -> Point {
        const double w = 1 + .12*x + .08*y;
        return {(320 + 70*x + 15*y)/w, (240 - 5*x - 100*y)/w};
    };
    const std::array<Point,4> perspective{{project(0,0), project(1,0), project(0,1), project(1,1)}};
    for (int x = -20; x <= 20; ++x) for (int y = -20; y <= 20; ++y) {
        const double a = x/10.0, b = y/10.0;
        assert(Unproject(perspective, project(a,b), result));
        assert(Near(result.x,a) && Near(result.y,b));
    }
    // Aurora applies Hor+ after the guest builds its camera. The hit inverse
    // must use the rendered points; the old unmodified camera misses this tap.
    constexpr double wideScale=1.65/(21.0/9.0);
    auto widened=perspective;
    for(auto& p:widened)p.x=BrawlMenuTouch::Detail::WidenScreenX(p.x,320,wideScale);
    auto rendered=project(.8,.3);
    rendered.x=BrawlMenuTouch::Detail::WidenScreenX(rendered.x,320,wideScale);
    assert(Unproject(widened,rendered,result));
    assert(Near(result.x,.8)&&Near(result.y,.3));
    assert(Unproject(perspective,rendered,result));
    assert(std::abs(result.x-.8)>.01);
    assert(Near(BrawlMenuTouch::Detail::WidenScreenX(320,320,wideScale),320));
    std::array<Point,4> collapsed{};
    assert(!Unproject(collapsed, {0,0}, result));
    auto invalid = viewport;
    invalid[2].x = std::numeric_limits<double>::quiet_NaN();
    assert(!Unproject(invalid, {0,0}, result));
    assert(!Unproject(viewport, {std::numeric_limits<double>::infinity(),0}, result));
    // A slanted icon's transparent corner lies inside its AABB but outside
    // its actual triangle. Both windings and degenerate polygons are covered.
    assert(InTriangle({.2,.2},{0,0},{1,0},{0,1}));
    assert(InTriangle({.2,.2},{0,1},{1,0},{0,0}));
    assert(!InTriangle({.9,.9},{0,0},{1,0},{0,1}));
    assert(!InTriangle({.2,.2},{0,0},{1,0},{2,0}));
    // Exhaust every selected/remembered cell and destination against the
    // documented native cursor transitions. This catches edge memory loops
    // and row-boundary mistakes that leave a tap waiting forever.
    for(unsigned initial=0;initial<14;++initial)for(unsigned memory=0;memory<14;++memory)
        for(unsigned target=0;target<14;++target) {
            unsigned selected=initial,remembered=memory;
            for(unsigned step=0;selected!=target&&step<16;++step) {
                const auto direction=BrawlMenuTouch::Detail::NameEntryDirection(selected,remembered,target);
                const unsigned old=selected;
                switch(direction) {
                case 0x1000:selected=13;remembered=12;break;
                case 0x8:
                    selected=old==13 ? remembered : old<3 ? 0 : old-3;
                    if(selected==0&&old!=0)remembered=old;
                    break;
                case 0x4:
                    selected=old==0 ? remembered : old+3>=14 ? 13 : old+3;
                    if(selected==13&&old!=13)remembered=old;
                    break;
                case 0x2:if(old!=0&&old!=13&&old%3!=0)++selected;break;
                case 0x1:if(old!=0&&old!=13&&old%3!=1)--selected;break;
                default:assert(false);
                }
                assert(selected<14&&remembered<14);
            }
            assert(selected==target);
        }
    assert(BrawlMenuTouch::Detail::NameEntryDirection(14,1,1)==0);
    using BrawlMenuTouch::Detail::ItemSwitchStep;
    using BrawlMenuTouch::Detail::ItemSwitchIndex;
    using BrawlMenuTouch::Detail::ItemSwitchDirection;
    // Native special edges: All spans the first two columns; the final row
    // has only two items; moving vertically retains the original column.
    assert(ItemSwitchStep(9,1)==7);
    assert(ItemSwitchStep(7,2)==9);
    assert(ItemSwitchStep(15,8)==7);
    assert(ItemSwitchStep(57,2)==56);
    assert(ItemSwitchStep(56,1)==57);
    assert(ItemSwitchStep(51,4)==2);
    assert(ItemSwitchStep(2,8)==51);
    assert(ItemSwitchIndex(1,0)==1&&ItemSwitchIndex(8,1)==50);
    for(unsigned initial=0;initial<63;++initial) {
        if((initial/7==1&&initial%7==1)||(initial/7==8&&initial%7>1))continue;
        for(unsigned target=0;target<=50;++target) {
            unsigned current=initial;
            for(unsigned step=0;ItemSwitchIndex(current/7,current%7)!=target&&step<20;++step) {
                const auto direction=ItemSwitchDirection(current,target);
                assert(direction!=0);
                current=ItemSwitchStep(current,direction);
                assert(current<63);
            }
            assert(ItemSwitchIndex(current/7,current%7)==target);
        }
    }

}
