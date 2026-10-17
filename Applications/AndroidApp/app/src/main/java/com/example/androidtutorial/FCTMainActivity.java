package com.example.androidtutorial;

import androidx.appcompat.app.AppCompatActivity;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.WindowManager;
import android.view.WindowMetrics;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class FCTMainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("node");
        System.loadLibrary("AndroidApp");
    }

    private final List<FCTWindow> activeWindows = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        extractAssetDirectory("res", new File(getFilesDir(), "res"));
        setNativeWorkingDir(getFilesDir().getAbsolutePath());

        android.view.View emptyView = new android.view.View(this);
        emptyView.setLayoutParams(new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        emptyView.setBackgroundColor(android.graphics.Color.TRANSPARENT);

        setContentView(emptyView);

        if (getSupportActionBar() != null) {
            getSupportActionBar().hide();
        }

        findViewById(android.R.id.content).post(new Runnable() {
            @Override
            public void run() {
                CPPEntry();
            }
        });
    }

    private void extractAssetDirectory(String assetPath, File targetDir) {
        AssetManager assetManager = getAssets();
        try {
            String[] children = assetManager.list(assetPath);
            if (children == null || children.length == 0) {
                copyAssetFile(assetManager, assetPath, targetDir);
                return;
            }
            if (!targetDir.exists()) {
                targetDir.mkdirs();
            }
            for (String child : children) {
                String childAssetPath = assetPath.isEmpty() ? child : (assetPath + "/" + child);
                extractAssetDirectory(childAssetPath, new File(targetDir, child));
            }
        } catch (IOException ignored) {
        }
    }

    private void copyAssetFile(AssetManager assetManager, String assetPath, File targetFile) {
        try (InputStream in = assetManager.open(assetPath)) {
            File parent = targetFile.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            try (OutputStream out = new FileOutputStream(targetFile)) {
                byte[] buffer = new byte[8192];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
            }
        } catch (IOException ignored) {
        }
    }

    @Override
    protected void onDestroy() {
        for (FCTWindow window : new ArrayList<>(activeWindows)) {
            window.onDestroy();
        }
        activeWindows.clear();

        super.onDestroy();
    }

    public int getDeviceWidth() {
        WindowManager windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        WindowMetrics maxMetrics = windowManager.getMaximumWindowMetrics();
        Rect bounds = maxMetrics.getBounds();
        return bounds.width();
    }

    public FCTWindow createWindow(long userdata, int x, int y, int w, int h) {
        FCTSurfaceView surfaceView = new FCTSurfaceView(this, userdata);
        FCTWindow fctWindow = new FCTWindow(this, surfaceView, userdata, w, h);
        activeWindows.add(fctWindow);
        fctWindow.show(x, y);
        return fctWindow;
    }

    public int getDeviceHeight() {
        WindowManager windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        WindowMetrics maxMetrics = windowManager.getMaximumWindowMetrics();
        Rect bounds = maxMetrics.getBounds();
        return bounds.height();
    }

    public native void setNativeWorkingDir(String path);

    public native void CPPEntry();
}
