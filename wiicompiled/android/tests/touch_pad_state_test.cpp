// SPDX-License-Identifier: GPL-3.0-only
#include "../../runtime/src/android/android_touch_state.h"

#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
void Check(bool passed, const char* message) {
    ++checks;
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main() {
    using BrawlAndroid::TouchPadSample;
    using BrawlAndroid::TouchPadState;
    constexpr uint16_t A=0x100, B=0x200, X=0x400, L=0x40, Z=0x10, START=0x1000;
    TouchPadState state;
    TouchPadSample sample;
    Check(!state.Sample(0,sample),"disabled overlay is disconnected");

    // A complete DOWN/UP between PAD polls must become exactly one game frame.
    state.Set(A,{0,0,0,0},true);
    state.Set(0,{0,0,0,0},true);
    Check(!state.Sample(1,sample),"other ports cannot consume pending player-one edges");
    Check(state.Sample(0,sample)&&sample.buttons==A,"fast attack tap reaches the first poll");
    Check(state.Sample(0,sample)&&sample.buttons==0,"fast attack tap is released at the next poll");
    Check(state.Sample(0,sample)&&sample.buttons==0,"fast attack never repeats later");
    state.Set(X,{0,0,0,0},true);
    state.Set(0,{0,0,0,0},true);
    Check(state.Sample(0,sample)&&sample.buttons==X,"fast jump tap reaches one poll");
    Check(state.Sample(0,sample)&&sample.buttons==0,"fast jump is not stretched beyond one poll");

    state.Set(A|L,{64,-32,0,100},true);
    for (unsigned n=0;n<3;++n) {
        Check(state.Sample(0,sample)&&sample.buttons==(A|L),"held buttons remain continuous");
        Check(sample.axes==std::array<int8_t,4>{64,-32,0,100},"both analog sticks remain independent");
    }
    state.Set(L,{20,10,-40,60},true);
    Check(state.Sample(0,sample)&&sample.buttons==L,"sampled attack UP does not delay release");
    Check(sample.axes==std::array<int8_t,4>{20,10,-40,60},"axis updates are current and never queued");

    // Overlapping fingers: a released tap is pending alongside other currently held buttons.
    state.Set(L|X,{100,0,0,-100},true);
    state.Set(L,{100,0,0,-100},true);
    state.Set(L|A|Z,{100,0,0,-100},true);
    Check(state.Sample(0,sample)&&sample.buttons==(L|X|A|Z),"jump tap coexists with held attack/shield/grab");
    Check(state.Sample(0,sample)&&sample.buttons==(L|A|Z),"only the released jump drops on the next poll");
    state.Set(0,{0,0,0,0},true);
    Check(state.Sample(0,sample)&&sample.buttons==0,"ordinary releases do not manufacture new presses");

    state.Set(B,{30,50,60,-20},true);
    state.Set(0,{0,0,0,0},true);
    state.Cancel();
    Check(state.Sample(0,sample),"cancel preserves enabled controller connection");
    Check(sample.buttons==0&&sample.axes==std::array<int8_t,4>{},"cancel discards fast unsampled edge and axes");
    Check(state.Sample(0,sample)&&sample.buttons==0,"cancelled edge never resurfaces");
    state.Set(A|X|L|Z,{100,100,-100,100},true);
    state.Cancel();
    Check(state.Sample(0,sample)&&sample.buttons==0&&sample.axes==std::array<int8_t,4>{},"cancel clears held and pending together");
    state.Set(0,{0,0,0,0},true);
    Check(state.Sample(0,sample)&&sample.buttons==0,"neutral publish after cancel cannot restore an edge");
    state.Set(X,{0,0,0,0},true);
    Check(state.Sample(0,sample)&&sample.buttons==X,"new deliberate press after cancel still works");

    state.Set(A,{50,50,50,50},true);
    state.Set(0,{0,0,0,0},false);
    Check(!state.Sample(0,sample),"pause/disable disconnects only when explicitly requested");
    state.Cancel();
    Check(!state.Sample(0,sample),"cancel never reconnects a disabled overlay");
    state.Set(0,{0,0,0,0},true);
    Check(state.Sample(0,sample)&&sample.buttons==0,"reenabling never restores stale edge");

    TouchPadState startup;
    Check(!startup.ConsumeStartupPress(true),"disabled title input cannot start");
    startup.Set(A,{0,0,0,0},true);
    startup.Set(0,{0,0,0,0},true);
    Check(!startup.ConsumeStartupPress(false),"title not ready never acknowledges a queued tap");
    Check(startup.Sample(0,sample)&&sample.buttons==A,"background PAD still samples its ordinary edge");
    Check(startup.Sample(0,sample)&&sample.buttons==0,"background PAD edge remains single-poll");
    Check(!startup.ConsumeStartupPress(false),"repeated not-ready check preserves startup edge");
    Check(startup.ConsumeStartupPress(true),"ready title receives tap after background PAD drained");
    Check(!startup.ConsumeStartupPress(true),"title consumes each startup edge exactly once");
    Check(startup.Sample(0,sample)&&sample.buttons==0,"consumed title tap never resurfaces in gameplay");

    startup.Set(START|L,{75,-25,30,40},true);
    Check(startup.ConsumeStartupPress(true),"START dismisses ready title");
    Check(startup.Sample(0,sample)&&sample.buttons==L,"accepted held START is suppressed until release");
    Check(sample.axes==std::array<int8_t,4>{75,-25,30,40},"title consumption preserves other controls and axes");
    startup.Set(START|L,{75,-25,30,40},true);
    Check(!startup.ConsumeStartupPress(true),"held START does not manufacture another title edge");
    Check(startup.Sample(0,sample)&&sample.buttons==L,"held title button cannot advance next game dialog");
    startup.Set(L,{0,0,0,0},true);
    startup.Set(START|L,{0,0,0,0},true);
    Check(startup.Sample(0,sample)&&sample.buttons==(START|L),"release and fresh START press work in gameplay");

    startup.Cancel();
    startup.Set(B,{0,0,0,0},true);
    Check(!startup.ConsumeStartupPress(true),"B cannot dismiss title");
    Check(startup.Sample(0,sample)&&sample.buttons==B,"non-start buttons are never consumed by title");
    startup.Set(A,{0,0,0,0},true);
    startup.Set(0,{0,0,0,0},true);
    startup.Cancel();
    Check(!startup.ConsumeStartupPress(true),"cancel discards an unsampled title tap");
    startup.Set(START,{0,0,0,0},true);
    startup.Set(0,{0,0,0,0},false);
    startup.Set(0,{0,0,0,0},true);
    Check(!startup.ConsumeStartupPress(true),"disable and reenable cannot replay title input");
    std::printf("PASS: production native touch state checks=%u\n",checks);
}
