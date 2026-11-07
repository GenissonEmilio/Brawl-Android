// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;

/** GameCube controls: continuous movement and a directional C-pad for attacks. */
final class TouchControlsView extends View {
    interface Listener {
        void onTouchInput(BrawlTouchInput.State state);
        /** Cancel every held and unsampled native press; ordinary UP never calls this. */
        void onTouchCancel();
        void onMenu();
        default void onMenuTap(float x,float y) {}
        /** Surface-normalized photo/menu pointer stream: 0 DOWN, 1 UP, 2 MOVE, 3 CANCEL. */
        default void onMenuPointer(int action,int id,float x,float y) {}
    }
    final BrawlTouchInput input=new BrawlTouchInput();
    private Listener listener;
    private boolean visible=true,haptics=true;
    private boolean menuMode;
    private final MenuTouchInput menuInput=new MenuTouchInput();
    private final java.util.Set<Integer> menuPointers=new java.util.LinkedHashSet<>();
    private final float tapSlop;
    private float opacity=.45f;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    TouchControlsView(Context context) { super(context);setFocusable(false);tapSlop=ViewConfiguration.get(context).getScaledTouchSlop(); }
    void setListener(Listener value) {listener=value;}
    void setOpacity(float value) {opacity=Math.max(.15f,Math.min(1,value));invalidate();}
    void setHaptics(boolean value) {haptics=value;}
    void setControlsVisible(boolean value) {visible=value;release();}
    void setMenuMode(boolean value) {if(menuMode!=value) {menuMode=value;release();}}
    boolean isMenuMode() {return menuMode;}
    void release() {input.clear();menuInput.clear();cancelMenuPointers();cancelPending();publish();}
    private boolean sendMenuPointer(int action,int id,float x,float y) {
        if(listener==null || getWidth()<=0 || getHeight()<=0 || !Float.isFinite(x) || !Float.isFinite(y)) return false;
        listener.onMenuPointer(action,id,x/getWidth(),y/getHeight());
        return true;
    }
    private void cancelMenuPointers() {
        if(listener!=null) for(int id:menuPointers) listener.onMenuPointer(3,id,0,0);
        menuPointers.clear();
    }
    private void cancelPending() {if(listener!=null) listener.onTouchCancel();}
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) {super.onSizeChanged(w,h,oldw,oldh);layoutControls();}
    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {layoutControls();return super.onApplyWindowInsets(insets);}
    private void layoutControls() {
        float left=0,right=0;WindowInsets insets=getRootWindowInsets();
        if(insets!=null&&insets.getDisplayCutout()!=null) {left=insets.getDisplayCutout().getSafeInsetLeft();right=insets.getDisplayCutout().getSafeInsetRight();}
        input.layout(getWidth(),getHeight(),left,right);menuInput.clear();cancelMenuPointers();cancelPending();publish();
    }
    @Override public void onWindowFocusChanged(boolean focus) {super.onWindowFocusChanged(focus);if(!focus) release();}
    @Override protected void onDetachedFromWindow() {release();super.onDetachedFromWindow();}
    @Override public boolean onTouchEvent(MotionEvent e) {
        // Physical controller events stay with SDL, never translated into phone touches.
        if(!e.isFromSource(InputDevice.SOURCE_TOUCHSCREEN)) return false;
        int action=e.getActionMasked(),index=e.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL) {release();return true;}
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            int pressed=menuMode?input.downMenu(e.getPointerId(index),e.getX(index),e.getY(index))
                    :input.down(e.getPointerId(index),e.getX(index),e.getY(index),visible);
            if(menuMode&&pressed==0) {
                int id=e.getPointerId(index);float x=e.getX(index),y=e.getY(index);
                menuInput.down(id,x,y);
                if(!menuPointers.contains(id)&&sendMenuPointer(0,id,x,y)) menuPointers.add(id);
            }
            if(pressed!=0&&haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        } else if(action==MotionEvent.ACTION_MOVE) {
            for(int i=0;i<e.getPointerCount();++i) {
                input.move(e.getPointerId(i),e.getX(i),e.getY(i));
                if(menuMode) {
                    int id=e.getPointerId(i);float x=e.getX(i),y=e.getY(i);
                    menuInput.move(id,x,y,tapSlop);
                    if(menuPointers.contains(id)) sendMenuPointer(2,id,x,y);
                }
            }
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            int released=e.getPointerId(index);
            boolean menu=input.up(released,e.getX(index),e.getY(index));
            if(menuPointers.remove(released)&&!sendMenuPointer(1,released,e.getX(index),e.getY(index))&&listener!=null)
                listener.onMenuPointer(3,released,0,0);
            boolean menuTap=menuMode&&menuInput.up(e.getPointerId(index),e.getX(index),e.getY(index),tapSlop);
            if(menuTap&&listener!=null&&getWidth()>0&&getHeight()>0) {
                float x=e.getX(index)/getWidth(),y=e.getY(index)/getHeight();
                if(x>=0&&x<=1&&y>=0&&y<=1) listener.onMenuTap(x,y);
            }
            publish();if(menu&&listener!=null) {release();listener.onMenu();}return true;
        }
        publish();return true;
    }
    private void publish() {if(listener!=null) listener.onTouchInput(input.state());invalidate();}
    @Override protected void onDraw(Canvas canvas) {
        BrawlTouchInput.State state=input.state();int alpha=Math.round(opacity*255);
        for(BrawlTouchInput.Control c:input.controls) {
            if(menuMode&&c.action!=BrawlTouchInput.START) continue;
            if(!menuMode&&!visible&&c.action!=BrawlTouchInput.MENU) continue;
            if(c.action==BrawlTouchInput.CSTICK) {drawDirectionalPad(canvas,c,state,alpha);continue;}
            boolean stick=c.action==BrawlTouchInput.MOVE;
            boolean held=c.action>0&&(state.buttons&c.action)!=0;
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.argb(held?alpha/2:alpha/6,255,255,255));
            canvas.drawCircle(c.x,c.y,c.r,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(Math.max(2,c.r*.035f));
            paint.setColor(Color.argb(alpha,255,255,255));canvas.drawCircle(c.x,c.y,c.r,paint);
            if(stick) {
                float x=c.action==BrawlTouchInput.MOVE?state.x:state.cx,y=c.action==BrawlTouchInput.MOVE?state.y:state.cy;
                if(c.action==BrawlTouchInput.MOVE) {paint.setAlpha(alpha/3);canvas.drawCircle(c.x,c.y,c.r*BrawlTouchInput.WALK_RUN_THRESHOLD,paint);paint.setAlpha(alpha);}
                canvas.drawCircle(c.x+x*c.r*.7f,c.y-y*c.r*.7f,c.r*.25f,paint);
            }
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.argb(Math.min(255,alpha+60),255,255,255));
            paint.setTextAlign(Paint.Align.CENTER);paint.setTextSize(c.r*(c.label.length()>2?.38f:.85f));paint.setFakeBoldText(true);
            canvas.drawText(c.label,c.x,c.y-(paint.ascent()+paint.descent())/2,paint);
        }
    }
    private void drawDirectionalPad(Canvas canvas,BrawlTouchInput.Control c,BrawlTouchInput.State state,int alpha) {
        float arm=c.r*.40f;
        for(int axis=0;axis<2;++axis) for(int sign=-1;sign<=1;sign+=2) {
            float value=axis==0?state.cx:state.cy;
            boolean held=value*sign>0;
            float x=c.x+(axis==0?sign*c.r*.62f:0),y=c.y-(axis==1?sign*c.r*.62f:0);
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.argb(held?alpha/2:alpha/6,255,255,255));
            canvas.drawRoundRect(x-arm,y-arm,x+arm,y+arm,c.r*.12f,c.r*.12f,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(Math.max(2,c.r*.035f));
            paint.setColor(Color.argb(alpha,255,255,255));
            canvas.drawRoundRect(x-arm,y-arm,x+arm,y+arm,c.r*.12f,c.r*.12f,paint);
            paint.setStyle(Paint.Style.FILL);paint.setTextAlign(Paint.Align.CENTER);paint.setTextSize(c.r*.48f);
            String label=axis==0?(sign<0?"◀":"▶"):(sign<0?"▼":"▲");
            canvas.drawText(label,x,y-(paint.ascent()+paint.descent())/2,paint);
        }
        paint.setStyle(Paint.Style.FILL);paint.setColor(Color.argb(alpha,255,255,255));
        paint.setTextSize(c.r*.40f);paint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText("C",c.x,c.y-(paint.ascent()+paint.descent())/2,paint);
    }
}
