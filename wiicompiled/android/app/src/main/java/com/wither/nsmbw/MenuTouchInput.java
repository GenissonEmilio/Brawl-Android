// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

/** Single deliberate tap on the original menu, independent of Android pointer indices. */
final class MenuTouchInput {
    private int pointer=-1;
    private float startX,startY;
    private boolean moved;
    void down(int id,float x,float y) {
        if(pointer!=-1) {moved=true;return;}
        if(id<0||!Float.isFinite(x)||!Float.isFinite(y)) return;
        pointer=id;startX=x;startY=y;moved=false;
    }
    void move(int id,float x,float y,float slop) {
        if(id==pointer&&(!Float.isFinite(x)||!Float.isFinite(y)||Math.hypot(x-startX,y-startY)>slop)) moved=true;
    }
    boolean up(int id,float x,float y,float slop) {
        if(id!=pointer) return false;
        move(id,x,y,slop);
        boolean tap=!moved;
        clear();
        return tap;
    }
    void clear() {pointer=-1;moved=false;}
}
