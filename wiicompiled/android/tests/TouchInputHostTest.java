// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

public final class TouchInputHostTest {
    private static int checks;
    private static void check(boolean v,String m) {++checks;if(!v) throw new AssertionError(m);}
    private static void near(float a,float b,String m) {check(Math.abs(a-b)<.001f,m+": "+a+" vs "+b);}
    private static void press(BrawlTouchInput i,int id,int action) {BrawlTouchInput.Control c=i.control(action);i.down(id,c.x,c.y,true);}
    public static void main(String[] args) {
        BrawlTouchInput i=new BrawlTouchInput();i.layout(2400,1080,40,10);
        int[] actions={BrawlTouchInput.A,BrawlTouchInput.B,BrawlTouchInput.X,BrawlTouchInput.L,BrawlTouchInput.R,BrawlTouchInput.Z,BrawlTouchInput.START};
        BrawlTouchInput.Control move=i.control(BrawlTouchInput.MOVE);
        press(i,19,BrawlTouchInput.MOVE);i.move(19,move.x+move.r,move.y);
        int bits=0;
        for(int n=0;n<actions.length;++n) {press(i,100+n*7,actions[n]);bits|=actions[n];check(i.state().buttons==bits,"independent simultaneous buttons "+n);near(i.state().x,1,"movement survives button press");}
        for(int n=0;n<actions.length;++n) {i.up(100+n*7,0,0);bits&=~actions[n];check(i.state().buttons==bits,"release only that pointer");near(i.state().x,1,"movement survives releases");}
        i.clear();near(i.state().x,0,"cancel clears stick");check(i.state().buttons==0,"cancel clears buttons");
        press(i,17,BrawlTouchInput.MOVE);
        i.move(17,move.x+move.r,Math.nextUp(move.y));
        check(i.state().y==0,"subpixel native MotionEvent roundoff keeps horizontal orthogonal axis exactly zero");
        check(i.state().x>.999f,"coordinate stabilization preserves full horizontal magnitude");
        i.move(17,Math.nextDown(move.x),move.y-move.r);
        check(i.state().x==0,"subpixel native MotionEvent roundoff keeps vertical orthogonal axis exactly zero");
        check(i.state().y>.999f,"coordinate stabilization preserves full vertical magnitude");
        i.clear();
        float[][] directions={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
        for(float[] d:directions) {
            float[] a=BrawlTouchInput.analog(d[0],d[1]);double length=Math.hypot(a[0],a[1]);
            check(Math.abs(length-1)<.001,"circular clamp");check(Math.signum(a[0])==Math.signum(d[0]),"x sign");check(Math.signum(a[1])==Math.signum(d[1]),"y sign");
        }
        near(BrawlTouchInput.analog(.1f,0)[0],0,"deadzone");
        float walk=BrawlTouchInput.analog(.45f,0)[0],run=BrawlTouchInput.analog(.82f,0)[0],smash=BrawlTouchInput.analog(1,0)[0];
        check(walk>0&&walk<BrawlTouchInput.WALK_RUN_THRESHOLD,"walk analog travel");
        check(run>BrawlTouchInput.WALK_RUN_THRESHOLD&&run<BrawlTouchInput.SMASH_THRESHOLD,"run analog travel");
        check(smash>BrawlTouchInput.SMASH_THRESHOLD,"full flick travel");
        near(BrawlTouchInput.analog(Float.NaN,0)[0],0,"reject NaN");
        near(BrawlTouchInput.directionalPad(.4f,0)[0],1,"right C-pad is discrete at partial travel");
        near(BrawlTouchInput.directionalPad(0,-.4f)[1],-1,"down C-pad is discrete at partial travel");
        near(BrawlTouchInput.directionalPad(.1f,.1f)[0],0,"C-pad centre is neutral");
        near(BrawlTouchInput.directionalPad(Float.NaN,1)[1],0,"C-pad rejects nonfinite coordinates");
        float[] diagonal=BrawlTouchInput.directionalPad(.6f,.6f);
        near((float)Math.hypot(diagonal[0],diagonal[1]),1,"C-pad diagonal preserves circular C-stick range");
        check(diagonal[0]>0&&diagonal[1]>0,"C-pad diagonal has both directions");
        press(i,4,BrawlTouchInput.MOVE);i.move(4,move.x+move.r,move.y);
        press(i,8,BrawlTouchInput.MOVE);i.move(8,move.x-move.r,move.y);near(i.state().x,1,"second finger cannot steal stick");
        BrawlTouchInput.Control a=i.control(BrawlTouchInput.A);press(i,12,BrawlTouchInput.A);i.move(12,move.x,move.y);near(i.state().x,1,"crossing buttons cannot steal stick");check(i.state().buttons==0,"drag off releases its owned button");i.move(12,a.x,a.y);check(i.state().buttons==BrawlTouchInput.A,"return reengages same button");
        BrawlTouchInput.Control c=i.control(BrawlTouchInput.CSTICK);press(i,28,BrawlTouchInput.CSTICK);i.move(28,c.x,c.y-c.r);near(i.state().cy,1,"C-stick up");near(i.state().x,1,"C-stick independent of movement");
        i.layout(1920,1080,0,0);check(i.state().buttons==0,"resize clears buttons");near(i.state().x,0,"resize clears movement");near(i.state().cy,0,"resize clears C-stick");
        a=i.control(BrawlTouchInput.A);i.down(1,a.x,a.y,false);check(i.state().buttons==0,"hidden controls inactive");
        c=i.control(BrawlTouchInput.MENU);i.down(2,c.x,c.y,false);check(i.up(2,c.x,c.y),"app menu reachable hidden");
        i.down(2,c.x,c.y,true);check(!i.up(2,0,0),"drag off does not open menu");
        System.out.println("PASS: GameCube touch host checks="+checks);
    }
}
