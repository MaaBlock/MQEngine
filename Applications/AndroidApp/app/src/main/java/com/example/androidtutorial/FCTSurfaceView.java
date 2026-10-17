package com.example.androidtutorial;

import android.content.Context;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.annotation.NonNull;

public class FCTSurfaceView extends SurfaceView implements SurfaceHolder.Callback {

    private long m_nativeWindowPtr;

    public FCTSurfaceView(Context context, long nativeWindowPtr) {
        super(context);
        m_nativeWindowPtr = nativeWindowPtr;
        init();
    }

    public FCTSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public FCTSurfaceView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        SurfaceHolder holder = getHolder();
        holder.addCallback(this);
    }

    public native void onSurfaceCreated(Surface surface, long nativeWindowPtr);
    public native void onSurfaceChanged(long nativeWindowPtr, int width, int height);
    public native void onSUrfaceDestroyed(long nativeWindowPtr);

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        android.util.Log.d("FCTSurfaceView", "surfaceCreated: Surface has been created");
        Surface surface = holder.getSurface();
        onSurfaceCreated(surface, m_nativeWindowPtr);
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        android.util.Log.d("FCTSurfaceView", "surfaceChanged: format=" + format +
                ", width=" + width + ", height=" + height);
        onSurfaceChanged(m_nativeWindowPtr, width, height);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        android.util.Log.d("FCTSurfaceView", "surfaceDestroyed: Surface is being destroyed");
        onSUrfaceDestroyed(m_nativeWindowPtr);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return super.onTouchEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            android.util.Log.d("FCTSurfaceView", "Back key pressed, intercepted");
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }
}
