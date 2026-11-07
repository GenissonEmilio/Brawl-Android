// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

struct CpuContext;

namespace BrawlMenuTouch {
// Coordinates are relative to the presented game image, excluding letterboxes.
// SubmitTap, Cancel and IsDirectTouchScreen are safe on the UI thread.
// Update, ObserveGuestEntry and ConsumePadButtons run on the game thread;
// consumption revalidates the scene before delivering a one-poll edge.
void Update() noexcept;
void SubmitTap(float normalizedX, float normalizedY) noexcept;
void Cancel() noexcept;
// Actions: DOWN=0, UP=1, MOVE=2, CANCEL=3. Pointer samples are accepted only
// by a validated photo-pause surface; all other menus retain tap semantics.
void SubmitPointer(unsigned action,unsigned id,float normalizedX,float normalizedY) noexcept;
bool ReadPointerAxes(unsigned port,int8_t* stickX,int8_t* stickY,int8_t* substickX,int8_t* substickY) noexcept;
bool IsDirectTouchScreen() noexcept;
uint16_t ConsumePadButtons(unsigned port) noexcept;
void ObserveGuestEntry(uint32_t address, CpuContext* cpu) noexcept;

namespace Detail {
// Route through the original name-entry cursor graph, including its remembered
// cell when leaving Delete/Enter. START only moves to Enter; A is sent later.
inline uint16_t NameEntryDirection(unsigned selected,unsigned remembered,unsigned target) {
    if(selected>13||remembered>13||target>13||selected==target)return 0;
    if(selected==0||selected==13) {
        if(remembered==0||remembered==13)return 0x1000;
        return selected==0 ? 0x4 : 0x8;
    }
    if(target==0)return 0x8;
    if(target==13)return 0x4;
    const unsigned row=(selected-1)/3,targetRow=(target-1)/3;
    if(row!=targetRow)return targetRow<row ? 0x8 : 0x4;
    return target<selected ? 0x1 : 0x2;
}

// Native Item Switch cursor layout: a frequency row, a double-width All
// cell, seven-column item rows and two cells on the last row.
inline unsigned ItemSwitchIndex(unsigned row,unsigned column) {
    return row==0 ? 0 : row==1 ? (column ? column : 1) : (row-1)*7+column;
}
inline unsigned ItemSwitchStep(unsigned position,uint16_t direction) {
    unsigned row=position/7,column=position%7;
    if(row>8)return position;
    if(direction==8) {
        if(row==0)row=column>1 ? 7 : 8;
        else {if(row==2&&column==1)column=0;--row;}
    } else if(direction==4) {
        if(row==8||(row==7&&column>1))row=0;else ++row;
    } else if(direction==1&&row) {
        if(row==1&&column==2)column=0;
        else column=column ? column-1 : row==8 ? 1 : 6;
    } else if(direction==2&&row) {
        if(row==1&&column==0)column=2;
        else column=(column==6||(row==8&&column==1)) ? 0 : column+1;
    }
    return row*7+column;
}
inline uint16_t ItemSwitchDirection(unsigned position,unsigned target) {
    if(position>=63||target>50||ItemSwitchIndex(position/7,position%7)==target)return 0;
    std::array<unsigned,63> queue{};
    std::array<uint16_t,63> first{};
    std::array<bool,63> seen{};
    unsigned begin=0,end=1;queue[0]=position;seen[position]=true;
    constexpr uint16_t directions[]={8,4,1,2};
    while(begin<end) {
        const auto at=queue[begin++];
        for(auto direction:directions) {
            const auto next=ItemSwitchStep(at,direction);
            if(seen[next])continue;
            seen[next]=true;first[next]=at==position ? direction : first[at];
            if(ItemSwitchIndex(next/7,next%7)==target)return first[next];
            queue[end++]=next;
        }
    }
    return 0;
}

struct Point { double x = 0, y = 0; };
inline double WidenScreenX(double x, double center, double scale) {
    return center+(x-center)*scale;
}
inline bool InTriangle(Point p, Point a, Point b, Point c) {
    auto cross = [](Point x, Point y, Point z) {
        return (y.x-x.x)*(z.y-x.y)-(y.y-x.y)*(z.x-x.x);
    };
    const double area = cross(a,b,c);
    if (!std::isfinite(area) || std::abs(area) < 1e-10) return false;
    const double x = cross(a,b,p), y = cross(b,c,p), z = cross(c,a,p);
    return area > 0 ? x >= 0 && y >= 0 && z >= 0 : x <= 0 && y <= 0 && z <= 0;
}

// Invert the projective image of the unit square. Unlike a viewport scale,
// this also handles perspective, camera rotation and the cursor's actual Z.
// Corner order: (0,0), (1,0), (0,1), (1,1).
inline bool Unproject(const std::array<Point, 4>& p, Point screen, Point& world) {
    for (const auto& v : p)
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) return false;
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return false;
    const double dx1 = p[1].x - p[3].x, dx2 = p[2].x - p[3].x;
    const double dy1 = p[1].y - p[3].y, dy2 = p[2].y - p[3].y;
    const double dx3 = p[0].x - p[1].x - p[2].x + p[3].x;
    const double dy3 = p[0].y - p[1].y - p[2].y + p[3].y;
    const double determinant = dx1 * dy2 - dx2 * dy1;
    if (std::abs(determinant) < 1e-9) return false;
    const double g = (dx3 * dy2 - dx2 * dy3) / determinant;
    const double h = (dx1 * dy3 - dx3 * dy1) / determinant;
    const double a = p[1].x - p[0].x + g * p[1].x - screen.x * g;
    const double b = p[2].x - p[0].x + h * p[2].x - screen.x * h;
    const double d = p[1].y - p[0].y + g * p[1].y - screen.y * g;
    const double e = p[2].y - p[0].y + h * p[2].y - screen.y * h;
    const double det = a * e - b * d;
    if (std::abs(det) < 1e-9) return false;
    const double x = screen.x - p[0].x, y = screen.y - p[0].y;
    world = {(x * e - b * y) / det, (a * y - x * d) / det};
    return std::isfinite(world.x) && std::isfinite(world.y);
}
} // namespace Detail
} // namespace BrawlMenuTouch
