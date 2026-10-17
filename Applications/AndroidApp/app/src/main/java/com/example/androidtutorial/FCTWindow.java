package com.example.androidtutorial;

import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public class FCTWindow extends Dialog {
    private Context context;
    private int x, y;
    private long m_nativeWindowPtr;
    private int width, height;
    private View contentView;

    public FCTWindow(Context context, View contentView, long nativeWindowPtr, int width, int height) {
        super(context, android.R.style.Theme_Translucent_NoTitleBar);

        this.context = context;
        this.contentView = contentView;
        this.m_nativeWindowPtr = nativeWindowPtr;
        this.width = width;
        this.height = height;

        Window window = getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));

            WindowManager.LayoutParams params = window.getAttributes();
            params.type = WindowManager.LayoutParams.TYPE_APPLICATION;
            params.width = width;
            params.height = height;
            params.gravity = Gravity.START | Gravity.TOP;
            params.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;

            window.setAttributes(params);
        }

        setContentView(contentView);

        contentView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                return false;
            }
        });

        setCancelable(false);
    }

    public long getNativeWindowPtr() {
        return m_nativeWindowPtr;
    }

    public native void onDismiss();

    public void show(int x, int y) {
        this.x = x;
        this.y = y;

        Window window = getWindow();
        if (window != null) {
            WindowManager.LayoutParams params = window.getAttributes();
            params.x = x;
            params.y = y;
            params.gravity = Gravity.START | Gravity.TOP;
            window.setAttributes(params);
        }

        show();
        android.util.Log.d("FCTWindow", "Window shown at " + x + ", " + y);
    }

    public void showInWindow(FCTWindow parentWindow, int x, int y) {
        int globalX = parentWindow.getX() + x;
        int globalY = parentWindow.getY() + y;
        show(globalX, globalY);
    }

    public void update(int x, int y, int width, int height) {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;

        Window window = getWindow();
        if (window != null && isShowing()) {
            WindowManager.LayoutParams params = window.getAttributes();
            params.x = x;
            params.y = y;
            params.width = width;
            params.height = height;
            params.flags |= WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
            params.flags |= WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS;
            params.gravity = Gravity.START | Gravity.TOP;
            window.setAttributes(params);
        }
    }

    public void onDestroy() {
        dismiss();
    }

    public void updatePosition(int x, int y) {
        this.x = x;
        this.y = y;

        Window window = getWindow();
        if (window != null && isShowing()) {
            WindowManager.LayoutParams params = window.getAttributes();
            params.x = x;
            params.y = y;
            params.gravity = Gravity.START | Gravity.TOP;
            window.setAttributes(params);
        }
    }

    public int getX() {
        return x;
    }

    public int getY() {
        return y;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }
}
