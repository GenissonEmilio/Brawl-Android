// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.util.LinkedHashMap;
import java.util.Map;

/** Android-free GameCube touch state. Geometry is shared by the View and tests. */
final class BrawlTouchInput {
    static final int A=0x100, B=0x200, X=0x400, Y=0x800, Z=0x10, L=0x40, R=0x20, START=0x1000;
    static final int MOVE=-1, CSTICK=-2, MENU=-3;
    static final float DEAD_ZONE=.12f, WALK_RUN_THRESHOLD=.72f, SMASH_THRESHOLD=.90f;
    // Android MotionEvent's native coordinate transforms can round-trip a centre coordinate
    // a fraction of a pixel off-axis. Keep a mathematically neutral axis exactly neutral.
    // This is below a thousandth of a display pixel, not a directional snap zone.
    private static final float COORDINATE_EPSILON=.001f;
    static final class Control {
        final int action; final String label; float x,y,r;
        Control(int action,String label) { this.action=action; this.label=label; }
        boolean hit(float px,float py) { return Math.hypot(px-x,py-y)<=r*1.1f; }
    }
    private static final class Finger {
        final int action; float x,y;
        Finger(int action,float x,float y) { this.action=action; this.x=x; this.y=y; }
    }
    static final class State {
        final int buttons; final float x,y,cx,cy;
        State(int b,float x,float y,float cx,float cy) { buttons=b;this.x=x;this.y=y;this.cx=cx;this.cy=cy; }
    }
    final Control[] controls={new Control(MOVE,"MOVE"),new Control(CSTICK,"C"),
        new Control(A,"A"),new Control(B,"B"),new Control(X,"X"),
        new Control(L,"L"),new Control(R,"R"),new Control(Z,"Z"),
        new Control(START,"START"),new Control(MENU,"⋮")};
    private final Map<Integer,Finger> fingers=new LinkedHashMap<>();
    void layout(float w,float h,float left,float right) {
        clear();
        // Side margins leave the centre and bottom damage HUD open.
        float u=Math.min(h,w/1.65f);
        place(MOVE,left+.25f*u,.67f*h,.17f*u);
        place(CSTICK,w-right-.66f*u,.78f*h,.105f*u);
        place(A,w-right-.18f*u,.60f*h,.090f*u);
        place(B,w-right-.39f*u,.68f*h,.075f*u);
        place(X,w-right-.30f*u,.38f*h,.075f*u);
        place(Z,w-right-.60f*u,.36f*h,.064f*u);
        place(R,w-right-.12f*u,.18f*h,.064f*u);
        place(L,left+.13f*u,.19f*h,.064f*u);
        place(START,w/2,.065f*h,.040f*u);
        place(MENU,w-right-.055f*u,.055f*h,.036f*u);
    }
    Control control(int action) { for(Control c:controls) if(c.action==action) return c; throw new IllegalArgumentException(); }
    private void place(int action,float x,float y,float r) { Control c=control(action);c.x=x;c.y=y;c.r=r; }
    int down(int id,float x,float y,boolean visible) {
        if(fingers.containsKey(id)) return 0;
        for(Control c:controls) {
            if((visible||c.action==MENU)&&c.hit(x,y)) {
                // A stick belongs to its first finger until it lifts. Crossing never steals roles.
                if(c.action<0&&c.action!=MENU) for(Finger f:fingers.values()) if(f.action==c.action) return 0;
                fingers.put(id,new Finger(c.action,x,y)); return c.action;
            }
        }
        return 0;
    }
    void move(int id,float x,float y) { Finger f=fingers.get(id); if(f!=null) {f.x=x;f.y=y;} }
    boolean up(int id,float x,float y) { Finger f=fingers.remove(id);return f!=null&&f.action==MENU&&control(MENU).hit(x,y); }
    void clear() { fingers.clear(); }
    State state() {
        int b=0;float x=0,y=0,cx=0,cy=0;
        for(Finger f:fingers.values()) {
            Control c=control(f.action);
            if(f.action==MOVE||f.action==CSTICK) {
                float dx=f.x-c.x,dy=c.y-f.y;
                if(Math.abs(dx)<COORDINATE_EPSILON) dx=0;
                if(Math.abs(dy)<COORDINATE_EPSILON) dy=0;
                float[] a=analog(dx/c.r,dy/c.r);
                if(f.action==MOVE) {x=a[0];y=a[1];} else {cx=a[0];cy=a[1];}
            } else if(f.action>0&&c.hit(f.x,f.y)) b|=f.action;
        }
        return new State(b,x,y,cx,cy);
    }
    static float[] analog(float x,float y) {
        float length=(float)Math.hypot(x,y);
        if(!Float.isFinite(length)||length<=DEAD_ZONE) return new float[]{0,0};
        float magnitude=Math.min(1,(length-DEAD_ZONE)/(1-DEAD_ZONE));
        return new float[]{x/length*magnitude,y/length*magnitude};
    }
}
