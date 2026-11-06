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
import android.view.WindowInsets;

/** Original vector GameCube controls. C-stick drives attacks; the match camera stays native. */
final class TouchControlsView extends View {
    interface Listener {
        void onTouchInput(BrawlTouchInput.State state);
        /** Cancel every held and unsampled native press; ordinary UP never calls this. */
        void onTouchCancel();
        void onMenu();
    }
    final BrawlTouchInput input=new BrawlTouchInput();
    private Listener listener;
    private boolean visible=true,haptics=true;
    private float opacity=.45f;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    TouchControlsView(Context context) { super(context);setFocusable(false); }
    void setListener(Listener value) {listener=value;}
    void setOpacity(float value) {opacity=Math.max(.15f,Math.min(1,value));invalidate();}
    void setHaptics(boolean value) {haptics=value;}
    void setControlsVisible(boolean value) {visible=value;release();}
    void release() {input.clear();cancelPending();publish();}
    private void cancelPending() {if(listener!=null) listener.onTouchCancel();}
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) {super.onSizeChanged(w,h,oldw,oldh);layoutControls();}
    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {layoutControls();return super.onApplyWindowInsets(insets);}
    private void layoutControls() {
        float left=0,right=0;WindowInsets insets=getRootWindowInsets();
        if(insets!=null&&insets.getDisplayCutout()!=null) {left=insets.getDisplayCutout().getSafeInsetLeft();right=insets.getDisplayCutout().getSafeInsetRight();}
        input.layout(getWidth(),getHeight(),left,right);cancelPending();publish();
    }
    @Override public void onWindowFocusChanged(boolean focus) {super.onWindowFocusChanged(focus);if(!focus) release();}
    @Override protected void onDetachedFromWindow() {release();super.onDetachedFromWindow();}
    @Override public boolean onTouchEvent(MotionEvent e) {
        // Physical controller events stay with SDL, never translated into phone touches.
        if(!e.isFromSource(InputDevice.SOURCE_TOUCHSCREEN)) return false;
        int action=e.getActionMasked(),index=e.getActionIndex();
        if(action==MotionEvent.ACTION_CANCEL) {release();return true;}
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            int pressed=input.down(e.getPointerId(index),e.getX(index),e.getY(index),visible);
            if(pressed!=0&&haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        } else if(action==MotionEvent.ACTION_MOVE) {
            for(int i=0;i<e.getPointerCount();++i) input.move(e.getPointerId(i),e.getX(i),e.getY(i));
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            boolean menu=input.up(e.getPointerId(index),e.getX(index),e.getY(index));
            publish();if(menu&&listener!=null) {release();listener.onMenu();}return true;
        }
        publish();return true;
    }
    private void publish() {if(listener!=null) listener.onTouchInput(input.state());invalidate();}
    @Override protected void onDraw(Canvas canvas) {
        BrawlTouchInput.State state=input.state();int alpha=Math.round(opacity*255);
        for(BrawlTouchInput.Control c:input.controls) {
            if(!visible&&c.action!=BrawlTouchInput.MENU) continue;
            boolean stick=c.action==BrawlTouchInput.MOVE||c.action==BrawlTouchInput.CSTICK;
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
}
