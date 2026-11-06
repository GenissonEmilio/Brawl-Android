// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.UiAutomation;
import android.content.Context;
import android.graphics.Point;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityNodeInfo;

import java.util.ArrayList;
import java.util.List;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/** Live input only: no fake Activity, native calls, game-memory edits, screenshots or recordings. */
final class LiveBrawlInput {
    private static final String PUBLIC_PACKAGE="com.genisson.brawl.android";
    private static final int WIDTH=2772,HEIGHT=1280;
    private static final float MOVE_X=472,MOVE_Y=858,MOVE_R=218;
    private static final float C_X=1927,C_Y=998,C_R=134;
    private static final float A_X=2542,A_Y=768,B_X=2273,B_Y=870,X_X=2388,X_Y=486;
    private static final float Z_X=2004,Z_Y=461,R_X=2618,R_Y=230,L_X=318,L_Y=243;
    private final Instrumentation instrumentation;
    private final UiAutomation automation;
    private final List<Finger> fingers=new ArrayList<>();
    private int attempted,acknowledged,phases;
    private long started,downTime;
    private String phase="foreground guard";
    private boolean mousePressed;

    private static final class Finger {
        final int id;float x,y;
        Finger(int id,float x,float y) {this.id=id;this.x=x;this.y=y;}
    }
    private LiveBrawlInput(Instrumentation instrumentation) {
        this.instrumentation=instrumentation;
        automation=instrumentation.getUiAutomation();
    }
    static void run(Instrumentation instrumentation) {
        execute(instrumentation,false);
    }
    static void clickVsync(Instrumentation instrumentation) {
        execute(instrumentation,true);
    }
    private static void execute(Instrumentation instrumentation,boolean mouseClick) {
        final LiveBrawlInput live=new LiveBrawlInput(instrumentation);
        Throwable failure=null;
        try {if(mouseClick) live.vsyncMouseClick();else live.script();}
        catch(Throwable error) {failure=error;}
        finally {
            try {live.releaseMouse();live.cancel();}
            catch(Throwable cleanup) {if(failure==null) failure=cleanup;else failure.addSuppressed(cleanup);}
        }
        Bundle result=new Bundle();
        result.putString("mode",mouseClick?"liveVsyncClick":"liveBrawl");
        result.putString("targetPackage",PUBLIC_PACKAGE);
        result.putInt("injectionsAttempted",live.attempted);
        result.putInt("injectionsAcknowledged",live.acknowledged);
        result.putInt("phasesCompleted",live.phases);
        result.putLong("elapsedMs",SystemClock.uptimeMillis()-live.started);
        if(failure==null) {
            result.putString("stream","\nOK: "+live.acknowledged+"/"+live.attempted
                    +" live "+(mouseClick?"mouse":"touch")+" injections acknowledged across "+live.phases+" phases. "
                    +(mouseClick?"Injection acknowledgment does not establish VSync state; inspect the native option label/log separately.\n"
                    :"Injection acknowledgment does not establish gameplay effects; review the live match separately.\n"));
            instrumentation.finish(Activity.RESULT_OK,result);
        } else {
            result.putString("shortMsg",failure.toString());
            result.putString("stream","\nLive input stopped during "+live.phase+". "
                    +live.acknowledged+"/"+live.attempted+" injections acknowledged.\n"+Log.getStackTraceString(failure));
            instrumentation.sendStatus(-2,result);
            instrumentation.finish(Activity.RESULT_CANCELED,result);
        }
    }
    private void guard() {
        if(started!=0&&SystemClock.uptimeMillis()-started>20000) throw new IllegalStateException("live input exceeded its 20-second limit");
        AccessibilityNodeInfo root=automation.getRootInActiveWindow();
        String name=null;
        if(root!=null) {
            try {CharSequence value=root.getPackageName();name=value==null?null:value.toString();}
            finally {root.recycle();}
        }
        boolean expected=name!=null&&!name.isEmpty()?PUBLIC_PACKAGE.equals(name):windowManagerHasGameFocus();
        if(!expected) throw new IllegalStateException("foreground focus could not be confirmed as the public Brawl game; no further input injected");
        WindowManager windows=(WindowManager)instrumentation.getTargetContext().getSystemService(Context.WINDOW_SERVICE);
        Point size=new Point();windows.getDefaultDisplay().getRealSize(size);
        if(size.x!=WIDTH||size.y!=HEIGHT) throw new IllegalStateException("display is "+size.x+"x"+size.y+", expected "+WIDTH+"x"+HEIGHT);
    }
    private boolean windowManagerHasGameFocus() {
        // Native SDL surfaces may expose no accessibility root. This fallback is read-only;
        // parse only the current-focus/focused-app fields and never print the window dump.
        try(BufferedReader dump=new BufferedReader(new InputStreamReader(
                new ParcelFileDescriptor.AutoCloseInputStream(automation.executeShellCommand("dumpsys window displays")),
                StandardCharsets.UTF_8))) {
            return LiveWindowFocus.matches(dump,PUBLIC_PACKAGE);
        } catch(IOException failure) {
            throw new IllegalStateException("cannot read the window-manager focus guard",failure);
        }
    }
    private void begin(String name) {
        phase=name;guard();
        Bundle status=new Bundle();status.putString("phase",name);
        status.putString("stream","\nInjecting live Brawl phase: "+name+"\n");
        instrumentation.sendStatus(1,status);
    }
    private void done() {++phases;}
    private void waitMs(long duration) {
        SystemClock.sleep(duration);
        if(SystemClock.uptimeMillis()-started>20000) throw new IllegalStateException("live input exceeded its 20-second limit");
    }
    private void vsyncMouseClick() {
        started=SystemClock.uptimeMillis();
        begin("native options VSync primary mouse click");
        mouseEvent(MotionEvent.ACTION_HOVER_MOVE,0);waitMs(80);
        guard();
        mousePressed=true;
        mouseEvent(MotionEvent.ACTION_DOWN,MotionEvent.BUTTON_PRIMARY);waitMs(120);
        mouseEvent(MotionEvent.ACTION_UP,0);mousePressed=false;
        waitMs(100);done();
    }
    private void releaseMouse() {
        if(!mousePressed) return;
        try {mouseEvent(MotionEvent.ACTION_UP,0);} finally {mousePressed=false;}
    }
    private void mouseEvent(int action,int buttons) {
        long time=SystemClock.uptimeMillis();
        if(action==MotionEvent.ACTION_DOWN) downTime=time;
        MotionEvent.PointerProperties properties=new MotionEvent.PointerProperties();
        properties.id=0;properties.toolType=MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords coordinates=new MotionEvent.PointerCoords();
        coordinates.x=2290;coordinates.y=680;coordinates.pressure=buttons==0?0:1;coordinates.size=1;
        MotionEvent event=MotionEvent.obtain(downTime==0?time:downTime,time,action,1,
                new MotionEvent.PointerProperties[]{properties},new MotionEvent.PointerCoords[]{coordinates},
                0,buttons,1,1,0,0,InputDevice.SOURCE_MOUSE,0);
        try {
            ++attempted;
            if(!automation.injectInputEvent(event,true)) throw new IllegalStateException("Android rejected mouse injection "+attempted);
            ++acknowledged;
        } finally {event.recycle();}
    }
    private void script() {
        started=SystemClock.uptimeMillis();guard();waitMs(500);
        begin("analog walk right then left");
        down(0,MOVE_X,MOVE_Y);move(0,MOVE_X+MOVE_R*.45f,MOVE_Y);waitMs(650);
        move(0,MOVE_X-MOVE_R*.45f,MOVE_Y);waitMs(650);up(0);done();
        begin("X jump");tap(X_X,X_Y,100);waitMs(500);done();
        begin("B special");tap(B_X,B_Y,100);waitMs(400);done();
        begin("A attack");tap(A_X,A_Y,100);waitMs(400);done();
        begin("C-stick right attack");
        down(0,C_X,C_Y);move(0,C_X+C_R,C_Y);waitMs(120);up(0);waitMs(400);done();
        begin("Z grab");tap(Z_X,Z_Y,100);waitMs(400);done();
        begin("L shield");tap(L_X,L_Y,500);waitMs(200);done();
        begin("R shield");tap(R_X,R_Y,500);waitMs(200);done();
        begin("simultaneous movement and X");combo(X_X,X_Y,MOVE_R*.80f,120,300);done();
        begin("simultaneous movement and A");combo(A_X,A_Y,-MOVE_R*.80f,120,300);done();
        begin("simultaneous movement and R");combo(R_X,R_Y,MOVE_R*.75f,400,200);done();
        begin("explicit multitouch cancellation");
        down(0,MOVE_X,MOVE_Y);move(0,MOVE_X-MOVE_R*.60f,MOVE_Y);
        down(1,A_X,A_Y);waitMs(120);cancel();waitMs(700);done();
    }
    private void tap(float x,float y,long hold) {down(0,x,y);waitMs(hold);up(0);}
    private void combo(float x,float y,float movement,long hold,long continueMoving) {
        down(0,MOVE_X,MOVE_Y);move(0,MOVE_X+movement,MOVE_Y);waitMs(100);
        down(1,x,y);waitMs(hold);up(1);waitMs(continueMoving);up(0);waitMs(250);
    }
    private int index(int id) {
        for(int n=0;n<fingers.size();++n) if(fingers.get(n).id==id) return n;
        throw new IllegalStateException("pointer "+id+" is not down");
    }
    private void down(int id,float x,float y) {
        for(Finger f:fingers) if(f.id==id) throw new IllegalStateException("pointer already down");
        if(fingers.isEmpty()) downTime=SystemClock.uptimeMillis();
        fingers.add(new Finger(id,x,y));
        emit(fingers.size()==1?MotionEvent.ACTION_DOWN:MotionEvent.ACTION_POINTER_DOWN|((fingers.size()-1)<<MotionEvent.ACTION_POINTER_INDEX_SHIFT));
    }
    private void move(int id,float x,float y) {Finger f=fingers.get(index(id));f.x=x;f.y=y;emit(MotionEvent.ACTION_MOVE);}
    private void up(int id) {
        int index=index(id);
        emit(fingers.size()==1?MotionEvent.ACTION_UP:MotionEvent.ACTION_POINTER_UP|(index<<MotionEvent.ACTION_POINTER_INDEX_SHIFT));
        fingers.remove(index);
    }
    private void cancel() {
        if(fingers.isEmpty()) return;
        try {emit(MotionEvent.ACTION_CANCEL);} finally {fingers.clear();}
    }
    private void emit(int action) {
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[fingers.size()];
        MotionEvent.PointerCoords[] coordinates=new MotionEvent.PointerCoords[fingers.size()];
        for(int n=0;n<fingers.size();++n) {
            Finger f=fingers.get(n);
            properties[n]=new MotionEvent.PointerProperties();properties[n].id=f.id;properties[n].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coordinates[n]=new MotionEvent.PointerCoords();coordinates[n].x=f.x;coordinates[n].y=f.y;
            coordinates[n].pressure=1;coordinates[n].size=1;
        }
        MotionEvent event=MotionEvent.obtain(downTime,SystemClock.uptimeMillis(),action,fingers.size(),properties,coordinates,
                0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        try {
            ++attempted;
            if(!automation.injectInputEvent(event,true)) throw new IllegalStateException("Android rejected touch injection "+attempted);
            ++acknowledged;
        } finally {event.recycle();}
    }
}
