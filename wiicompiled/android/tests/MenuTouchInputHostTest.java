// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

public final class MenuTouchInputHostTest {
    private static int checks;
    private static void check(boolean value,String message) {
        ++checks;
        if(!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        MenuTouchInput touch=new MenuTouchInput();
        touch.down(19,100,200);
        check(!touch.up(1,100,200,10),"an unrelated pointer cannot select");
        check(touch.up(19,102,198,10),"a deliberate tap selects on its own release");
        check(!touch.up(19,102,198,10),"one release cannot select twice");
        touch.down(3,100,200);touch.move(3,130,200,10);touch.move(3,100,200,10);
        check(!touch.up(3,100,200,10),"a drag returning to its start never becomes a tap");
        touch.down(7,50,50);touch.clear();
        check(!touch.up(7,50,50,10),"pause/focus/resize/mode changes cancel pending selections");
        touch.down(4,50,50);touch.down(20,150,150);
        check(!touch.up(20,150,150,10),"second pointer never selects");
        check(!touch.up(4,50,50,10),"two-finger gesture cannot accidentally confirm");
        touch.down(17,600,540);touch.down(29,1800,540);
        touch.move(17,800,650,10);touch.move(29,1600,450,10);
        check(!touch.up(17,2600,-20,10),"photo drag released outside never becomes a menu tap");
        check(!touch.up(29,1600,450,10),"remaining pinch finger cannot confirm after the first release");
        touch.down(31,1200,540);
        check(touch.up(31,1200,540,10),"a separate deliberate tap still works after a photo pinch");
        touch.down(9,50,50);touch.move(9,Float.NaN,50,10);
        check(!touch.up(9,50,50,10),"invalid coordinates cancel selection");
        touch.down(12,50,50);
        check(touch.up(12,50,50,10),"a new deliberate tap after cancellation works");
        BrawlTouchInput controls=new BrawlTouchInput();controls.layout(2400,1080,0,0);
        for(BrawlTouchInput.Control control:controls.controls) {
            controls.clear();controls.downMenu(3,control.x,control.y);
            check(controls.state().buttons==(control.action==BrawlTouchInput.START?BrawlTouchInput.START:0),
                    "menu mode exposes only START, not "+control.label);
            check(controls.state().x==0&&controls.state().cx==0,"menu mode never steers a hidden stick");
        }
        System.out.println("PASS: direct menu touch host checks="+checks);
    }
}
