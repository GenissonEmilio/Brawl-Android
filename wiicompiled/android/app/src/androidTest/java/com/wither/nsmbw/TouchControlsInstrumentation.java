// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;

/** Runs real Android View/MotionEvent cases without loading native code or private game data. */
public final class TouchControlsInstrumentation extends Instrumentation {
    private TouchControlsView view;
    private BrawlTouchInput.State state;
    private int checks,menus,cancels;
    private int menuTaps;
    private final java.util.List<float[]> pointerEvents=new java.util.ArrayList<>();
    private float tapX,tapY;
    private long tick;
    private String lastCoordinates;
    private boolean liveBrawl;
    private boolean liveVsyncClick;
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        liveBrawl=args!=null&&"true".equalsIgnoreCase(args.getString("liveBrawl"));
        liveVsyncClick=args!=null&&"true".equalsIgnoreCase(args.getString("liveVsyncClick"));
        start();
    }
    @Override public void onStart() {
        if(liveVsyncClick) {LiveBrawlInput.clickVsync(this);return;}
        if(liveBrawl) {LiveBrawlInput.run(this);return;}
        final Throwable[] failed={null};
        runOnMainSync(() -> {try {runCases();} catch(Throwable e) {failed[0]=e;} finally {if(view!=null)view.release();}});
        Bundle result=new Bundle();result.putInt("checks",checks);
        if(failed[0]==null) {result.putString("stream","\nOK (Android touch, direct-menu and C-pad regressions, "+checks+" checks)\n");finish(Activity.RESULT_OK,result);}
        else {result.putString("stream",Log.getStackTraceString(failed[0]));result.putString("shortMsg",failed[0].toString());sendStatus(-2,result);finish(Activity.RESULT_CANCELED,result);}
    }
    private void check(boolean b,String m) {++checks;if(!b)throw new AssertionError(m);}
    private void fresh() {
        pointerEvents.clear();
        view=new TouchControlsView(getTargetContext());view.setHaptics(false);
        view.setListener(new TouchControlsView.Listener() {
            public void onTouchInput(BrawlTouchInput.State s) {state=s;}
            public void onTouchCancel() {++cancels;state=new BrawlTouchInput.State(0,0,0,0,0);}
            public void onMenu() {++menus;}
            public void onMenuTap(float x,float y) {++menuTaps;tapX=x;tapY=y;}
            public void onMenuPointer(int action,int id,float x,float y) {pointerEvents.add(new float[]{action,id,x,y});}
        });view.layout(0,0,2400,1080);tick=SystemClock.uptimeMillis();
    }
    private float[] point(int action,float x,float y) {BrawlTouchInput.Control c=view.input.control(action);return new float[]{c.x+x*c.r,c.y-y*c.r};}
    private void event(int action,int source,int[] ids,float[][] xy) {
        MotionEvent.PointerProperties[] p=new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] c=new MotionEvent.PointerCoords[ids.length];
        for(int n=0;n<ids.length;++n) {p[n]=new MotionEvent.PointerProperties();p[n].id=ids[n];p[n].toolType=MotionEvent.TOOL_TYPE_FINGER;c[n]=new MotionEvent.PointerCoords();c[n].x=xy[n][0];c[n].y=xy[n][1];c[n].pressure=1;c[n].size=1;}
        MotionEvent e=MotionEvent.obtain(tick,++tick,action,ids.length,p,c,0,0,1,1,0,0,source,0);
        try {
            lastCoordinates="requested=("+xy[0][0]+","+xy[0][1]+") delivered=("+e.getX(0)+","+e.getY(0)+")";
            view.dispatchTouchEvent(e);
        } finally {e.recycle();}
    }
    private void touch(int action,int[] ids,float[][] xy) {event(action,InputDevice.SOURCE_TOUCHSCREEN,ids,xy);}
    private void runCases() {
        fresh();
        int[] actions={BrawlTouchInput.MOVE,BrawlTouchInput.X,BrawlTouchInput.A,BrawlTouchInput.L,BrawlTouchInput.Z,BrawlTouchInput.CSTICK};
        int[] ids={2,5,9,14,21,30};float[][] xy=new float[6][];
        for(int n=0;n<6;++n) xy[n]=point(actions[n],n==0?1:0,n==5?1:0);
        for(int n=0;n<6;++n) touch(n==0?MotionEvent.ACTION_DOWN:MotionEvent.ACTION_POINTER_DOWN|(n<<8),java.util.Arrays.copyOf(ids,n+1),java.util.Arrays.copyOf(xy,n+1));
        check(state.buttons==(BrawlTouchInput.X|BrawlTouchInput.A|BrawlTouchInput.L|BrawlTouchInput.Z),"move+jump+attack+shield+grab together");check(state.x>.99f&&state.cy>.99f,"independent analog sticks");
        int before=cancels;
        touch(MotionEvent.ACTION_POINTER_UP|(2<<8),ids,xy);check((state.buttons&BrawlTouchInput.A)==0,"lifting attack releases attack");check((state.buttons&(BrawlTouchInput.X|BrawlTouchInput.L|BrawlTouchInput.Z))==(BrawlTouchInput.X|BrawlTouchInput.L|BrawlTouchInput.Z),"other fingers retained");
        check(cancels==before,"ordinary pointer UP preserves unsampled native press edges");
        touch(MotionEvent.ACTION_CANCEL,ids,xy);check(state.buttons==0&&state.x==0&&state.cy==0,"cancel releases all");
        check(cancels>before,"ACTION_CANCEL explicitly discards native pending edges");
        fresh();float[][] moving={point(BrawlTouchInput.MOVE,1,0)};touch(MotionEvent.ACTION_DOWN,new int[]{7},moving);before=cancels;view.onWindowFocusChanged(false);check(state.x==0&&state.buttons==0,"focus loss clears");check(cancels>before,"focus loss discards native pending edges");touch(MotionEvent.ACTION_DOWN,new int[]{7},moving);before=cancels;view.layout(0,0,1920,1080);check(state.x==0,"resize clears");check(cancels>before,"resize discards native pending edges");
        fresh();float[][] attack={point(BrawlTouchInput.A,0,0)};event(MotionEvent.ACTION_DOWN,InputDevice.SOURCE_JOYSTICK,new int[]{1},attack);check(state.buttons==0,"controller is not virtual phone input");
        touch(MotionEvent.ACTION_DOWN,new int[]{1},attack);before=cancels;view.setControlsVisible(false);check(state.buttons==0,"hiding controls clears held input");check(cancels>before,"hiding controls discards native pending edges");touch(MotionEvent.ACTION_DOWN,new int[]{8},attack);check(state.buttons==0,"hidden buttons stay inactive");float[][] menu={point(BrawlTouchInput.MENU,0,0)};touch(MotionEvent.ACTION_DOWN,new int[]{3},menu);touch(MotionEvent.ACTION_UP,new int[]{3},menu);check(menus==1,"app menu remains reachable");
        fresh();float[][] start={point(BrawlTouchInput.START,0,0)};before=cancels;touch(MotionEvent.ACTION_DOWN,new int[]{1},start);check(state.buttons==BrawlTouchInput.START,"pause is GC Start");touch(MotionEvent.ACTION_UP,new int[]{1},start);check(state.buttons==0,"pause release");check(cancels==before,"complete short tap UP is not native cancellation");
        for(float[] d:new float[][]{{1,0},{-1,0},{0,1},{0,-1},{.5f,.5f}}) {
            float[][] at={point(BrawlTouchInput.MOVE,d[0],d[1])};
            touch(MotionEvent.ACTION_DOWN,new int[]{1},at);
            check(Math.signum(state.x)==Math.signum(d[0])&&Math.signum(state.y)==Math.signum(d[1]),
                "analog direction expected=("+d[0]+","+d[1]+") actual=("+state.x+","+state.y+") "+lastCoordinates);
            if(d[0]==0||d[1]==0) check(Math.max(Math.abs(state.x),Math.abs(state.y))>.99f,"full cardinal analog magnitude");
            touch(MotionEvent.ACTION_UP,new int[]{1},at);
        }
        fresh();view.setMenuMode(true);
        int tapsBefore=menuTaps,menusBefore=menus;
        float[][] item={{1200,540}};
        touch(MotionEvent.ACTION_DOWN,new int[]{17},item);
        check(state.buttons==0&&state.x==0&&state.cx==0,"direct-menu touch never activates hidden gameplay controls");
        touch(MotionEvent.ACTION_UP,new int[]{17},item);
        check(menuTaps==tapsBefore+1&&tapX==.5f&&tapY==.5f,"original menu receives normalized surface coordinates on a deliberate tap");
        start=new float[][]{point(BrawlTouchInput.START,0,0)};
        touch(MotionEvent.ACTION_DOWN,new int[]{23},start);
        check(state.buttons==BrawlTouchInput.START,"START remains usable in a direct-touch menu");
        touch(MotionEvent.ACTION_UP,new int[]{23},start);
        check(menuTaps==tapsBefore+1&&menus==menusBefore,"START is not an extra item selection or app-menu click");
        touch(MotionEvent.ACTION_DOWN,new int[]{17},item);view.onWindowFocusChanged(false);
        touch(MotionEvent.ACTION_UP,new int[]{17},item);
        check(menuTaps==tapsBefore+1,"focus loss discards pending menu selection");
        touch(MotionEvent.ACTION_DOWN,new int[]{17},item);view.setMenuMode(false);
        touch(MotionEvent.ACTION_UP,new int[]{17},item);
        check(menuTaps==tapsBefore+1&&state.buttons==0,"scene/HUD transition cannot leak a menu tap into a match");
        fresh();float[][] direction={point(BrawlTouchInput.CSTICK,.4f,0)};
        touch(MotionEvent.ACTION_DOWN,new int[]{29},direction);
        check(state.cx==1&&state.cy==0,"C-pad direction is full discrete travel without a fixed right analog stick");
        touch(MotionEvent.ACTION_UP,new int[]{29},direction);
        check(state.cx==0&&state.cy==0,"releasing C-pad returns the right axes to neutral");
        runPointerCases();
    }
    private void pointer(int index,int action,int id,float x,float y,String message) {
        check(pointerEvents.size()>index,message+" exists");
        float[] actual=pointerEvents.get(index);
        check(actual[0]==action&&actual[1]==id&&Math.abs(actual[2]-x)<.0001f&&Math.abs(actual[3]-y)<.0001f,message);
    }
    private void runPointerCases() {
        fresh();view.setMenuMode(true);int taps=menuTaps;
        float[] start=point(BrawlTouchInput.START,0,0),left={600,540},right={1800,540};
        touch(MotionEvent.ACTION_DOWN,new int[]{3},new float[][]{start});
        check(pointerEvents.isEmpty(),"START down is excluded from photo pointer stream");
        touch(MotionEvent.ACTION_POINTER_DOWN|(1<<8),new int[]{3,17},new float[][]{start,left});
        touch(MotionEvent.ACTION_POINTER_DOWN|(2<<8),new int[]{3,17,29},new float[][]{start,left,right});
        pointer(0,0,17,.25f,.5f,"first photo finger DOWN keeps sparse ID and surface coordinates");
        pointer(1,0,29,.75f,.5f,"second photo finger DOWN remains independent of START");
        touch(MotionEvent.ACTION_MOVE,new int[]{3,17,29},new float[][]{start,{800,650},{1600,450}});
        pointer(2,2,17,800f/2400,650f/1080,"first drag MOVE uses its own pointer coordinates");
        pointer(3,2,29,1600f/2400,450f/1080,"pinch MOVE preserves second pointer coordinates");
        touch(MotionEvent.ACTION_POINTER_UP|(1<<8),new int[]{3,17,29},new float[][]{start,{2600,-20},{1600,450}});
        pointer(4,1,17,2600f/2400,-20f/1080,"UP outside the surface is forwarded without clamping");
        touch(MotionEvent.ACTION_POINTER_UP|(1<<8),new int[]{3,29},new float[][]{start,{1600,450}});
        pointer(5,1,29,1600f/2400,450f/1080,"second finger UP follows its unchanged sparse ID");
        touch(MotionEvent.ACTION_UP,new int[]{3},new float[][]{start});
        check(pointerEvents.size()==6&&menuTaps==taps,"START release is excluded and multi-finger drag never confirms a menu tap");

        fresh();view.setMenuMode(true);taps=menuTaps;
        touch(MotionEvent.ACTION_DOWN,new int[]{7},new float[][]{left});
        touch(MotionEvent.ACTION_MOVE,new int[]{7},new float[][]{{900,540}});
        touch(MotionEvent.ACTION_MOVE,new int[]{7},new float[][]{left});
        touch(MotionEvent.ACTION_UP,new int[]{7},new float[][]{left});
        check(pointerEvents.size()==4&&menuTaps==taps,"single drag returning to start keeps its stream and never becomes a tap");

        fresh();view.setMenuMode(true);
        touch(MotionEvent.ACTION_DOWN,new int[]{7},new float[][]{left});
        touch(MotionEvent.ACTION_POINTER_DOWN|(1<<8),new int[]{7,11},new float[][]{left,right});
        touch(MotionEvent.ACTION_CANCEL,new int[]{7,11},new float[][]{left,right});
        pointer(2,3,7,0,0,"ACTION_CANCEL terminates first forwarded pointer");
        pointer(3,3,11,0,0,"ACTION_CANCEL terminates second forwarded pointer");
        view.release();check(pointerEvents.size()==4,"repeated lifecycle release cannot duplicate pointer cancellation");

        fresh();view.setMenuMode(true);taps=menuTaps;
        touch(MotionEvent.ACTION_DOWN,new int[]{17},new float[][]{left});view.onWindowFocusChanged(false);
        pointer(1,3,17,0,0,"focus loss cancels the active photo pointer");
        touch(MotionEvent.ACTION_UP,new int[]{17},new float[][]{left});
        check(pointerEvents.size()==2&&menuTaps==taps,"stale release after focus cancellation cannot select or reopen a pointer");

        fresh();view.setMenuMode(true);
        touch(MotionEvent.ACTION_DOWN,new int[]{17},new float[][]{left});view.layout(0,0,1920,1080);
        pointer(1,3,17,0,0,"resize cancels before changed surface normalization is used");

        fresh();view.setMenuMode(true);taps=menuTaps;
        touch(MotionEvent.ACTION_DOWN,new int[]{17},new float[][]{left});view.setMenuMode(false);
        pointer(1,3,17,0,0,"leaving menu mode terminates active photo pointer");
        touch(MotionEvent.ACTION_MOVE,new int[]{17},new float[][]{{900,540}});
        touch(MotionEvent.ACTION_UP,new int[]{17},new float[][]{left});
        check(pointerEvents.size()==2&&menuTaps==taps&&state.buttons==0,"mode transition cannot leak drag/tap into gameplay");

        fresh();touch(MotionEvent.ACTION_DOWN,new int[]{17},new float[][]{left});
        touch(MotionEvent.ACTION_MOVE,new int[]{17},new float[][]{{900,540}});
        touch(MotionEvent.ACTION_UP,new int[]{17},new float[][]{left});
        check(pointerEvents.isEmpty(),"gameplay never forwards the photo/menu pointer stream");
    }

}
