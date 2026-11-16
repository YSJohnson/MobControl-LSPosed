package com.yg.mobctrl;

import android.app.Activity;
import android.content.Context;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayDeque;

/**
 * 游戏内悬浮窗。
 *
 * 不申请 SYSTEM_ALERT_WINDOW，也不 hook WindowManagerService：
 * 直接把 View 挂到当前 Activity 的 decorView 上，跟随游戏显示/隐藏，
 * 天然位于 Unity SurfaceView 之上，零权限、零系统服务改动。
 */
public final class OverlayPanel {

    private static final String TAG = "MobCtrl";
    private static final Handler UI = new Handler(Looper.getMainLooper());

    /** 折叠球直径 */
    private static final int BALL_DP = 46;
    /** 面板宽度 */
    private static final int PANEL_W_DP = 300;

    private static final int C_BG     = 0xF2141418;
    private static final int C_HEAD   = 0xFF1E1E24;
    private static final int C_ACCENT = 0xFF4C9AFF;
    private static final int C_TEXT   = 0xFFECECEC;
    private static final int C_SUB    = 0xFF8A8A93;

    /** 开关型功能：label + Config 位 */
    private static final String[] SW_LABEL = {
            "官方 Cheats 解锁", "跳过插屏广告", "无限金币（临时）", "无限跳过卷", "精英俱乐部",
    };
    private static final int[] SW_BIT = {
            Config.F_CHEATS_UNLOCK, Config.F_NO_ADS, Config.F_CURRENCY, Config.F_SKIPETS, Config.F_ELITE_VIP,
    };
    /** 开关位默认值（与 Config 默认一致） */
    private static final boolean[] SW_DEF = { true, false, false, false, false };

    /** 滑块型功能：label + SP key + 上限(%) + 默认(%) + 对应功能位 */
    private static final String[] SL_LABEL = { "射速倍率" };
    private static final String[] SL_KEY   = { "fire_rate_pct" };
    private static final int[]    SL_MAX   = { 1000 };
    private static final int[]    SL_DEF   = { 300 };
    private static final int[]    SL_BIT   = { Config.F_FIRE_RATE };

    private static OverlayPanel sInstance;

    private final Activity activity;
    private final Context ctx;

    private FrameLayout root;      // 挂在 decorView 上的容器
    private View ball;             // 折叠态
    private View panel;            // 展开态
    private boolean expanded = false;

    private int ballX = -1, ballY = -1;

    private OverlayPanel(Activity a) {
        this.activity = a;
        this.ctx = a;
    }

    // ------------------------------------------------------------------ //

    /** 在 Activity 上挂载（幂等）。 */
    public static void attach(final Activity activity) {
        if (activity == null) return;
        Config.init(activity);
        UI.post(new Runnable() {
            @Override public void run() {
                try {
                    if (sInstance != null && sInstance.activity == activity && sInstance.root != null) {
                        return;
                    }
                    if (sInstance != null) sInstance.detach();
                    OverlayPanel p = new OverlayPanel(activity);
                    p.install();
                    sInstance = p;
                    Log.i(TAG, "overlay attached: " + activity.getClass().getName());
                } catch (Throwable t) {
                    Log.e(TAG, "overlay attach failed", t);
                }
            }
        });
    }

    /** Activity 销毁前摘掉，避免窗口泄漏。 */
    public static void detachFrom(final Activity activity) {
        UI.post(new Runnable() {
            @Override public void run() {
                if (sInstance != null && sInstance.activity == activity) {
                    sInstance.detach();
                    sInstance = null;
                }
            }
        });
    }

    // ------------------------------------------------------------------ //

    private void install() {
        ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();

        root = new FrameLayout(ctx);
        root.setClipChildren(false);
        FrameLayout.LayoutParams rlp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        decor.addView(root, rlp);

        ball = buildBall();
        panel = buildPanel();

        root.addView(panel, frameLp(dp(PANEL_W_DP), ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(ball, frameLp(dp(BALL_DP), dp(BALL_DP)));

        panel.setVisibility(View.GONE);

        // 默认位置：右侧偏上
        root.post(new Runnable() {
            @Override public void run() {
                if (ballX < 0) {
                    ballX = root.getWidth() - dp(BALL_DP) - dp(12);
                    ballY = root.getHeight() / 3;
                }
                moveBallTo(ballX, ballY);
            }
        });
    }

    private void detach() {
        try {
            if (root != null && root.getParent() instanceof ViewGroup) {
                ((ViewGroup) root.getParent()).removeView(root);
            }
        } catch (Throwable t) {
            Log.w(TAG, "overlay detach: " + t);
        }
        root = null; ball = null; panel = null;
        sInstance = null;
    }

    // ------------------------------------------------------------------ //
    // 折叠态：圆形按钮
    // ------------------------------------------------------------------ //

    private View buildBall() {
        TextView tv = new TextView(ctx);
        tv.setText("MC");
        tv.setTextColor(Color.WHITE);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setGravity(Gravity.CENTER);
        tv.setTypeface(null, 1); // bold

        GradientDrawable bg = new GradientDrawable();
        bg.setShape(GradientDrawable.OVAL);
        bg.setColor(0xCC1B6FE0);
        bg.setStroke(dp(1), C_ACCENT);
        tv.setBackground(bg);

        tv.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { toggle(); }
        });
        tv.setOnTouchListener(new DragTouch());
        return tv;
    }

    // ------------------------------------------------------------------ //
    // 展开态：功能面板
    // ------------------------------------------------------------------ //

    private View buildPanel() {
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(14));
        bg.setColor(C_BG);
        bg.setStroke(dp(1), 0xFF2E2E38);
        card.setBackground(bg);

        // ---- 标题栏 ----
        LinearLayout head = new LinearLayout(ctx);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(dp(12), dp(9), dp(8), dp(9));

        GradientDrawable hbg = new GradientDrawable();
        hbg.setCornerRadii(new float[]{ dp(14), dp(14), dp(14), dp(14), 0, 0, 0, 0 });
        hbg.setColor(C_HEAD);
        head.setBackground(hbg);

        TextView title = new TextView(ctx);
        title.setText("MobCtrl");
        title.setTextColor(C_TEXT);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        title.setTypeface(null, 1);
        head.addView(title, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView ver = new TextView(ctx);
        ver.setText("v1.1");
        ver.setTextColor(C_SUB);
        ver.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        ver.setPadding(0, 0, dp(8), 0);
        head.addView(ver);

        TextView fold = new TextView(ctx);
        fold.setText("—");
        fold.setTextColor(C_ACCENT);
        fold.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        fold.setGravity(Gravity.CENTER);
        fold.setPadding(dp(8), 0, dp(8), 0);
        fold.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { setExpanded(false); }
        });
        head.addView(fold);

        head.setOnTouchListener(new DragTouch());
        card.addView(head);

        // ---- 内容 ----
        ScrollView sv = new ScrollView(ctx);
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(12), dp(6), dp(12), dp(10));
        sv.addView(body);
        card.addView(sv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        SharedPreferences sp = Config.sp();

        // 开关组
        for (int i = 0; i < SW_LABEL.length; i++) {
            final int bit = SW_BIT[i];
            Switch sw = new Switch(ctx);
            sw.setText(SW_LABEL[i]);
            sw.setTextColor(C_TEXT);
            sw.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            sw.setChecked(Config.getBool(Config.keyForBit(bit), Config.defForBit(bit)));
            sw.setPadding(0, dp(4), 0, dp(4));
            sw.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                @Override public void onCheckedChanged(CompoundButton b, boolean on) {
                    Config.setBool(Config.keyForBit(bit), on);
                    push();
                }
            });
            body.addView(sw);
        }

        // 分隔线
        body.addView(divider());

        // 滑块组
        for (int i = 0; i < SL_LABEL.length; i++) {
            body.addView(buildSlider(SL_LABEL[i], SL_KEY[i], SL_MAX[i], SL_DEF[i], SL_BIT[i], sp));
        }

        // ---- 底部按钮 ----
        LinearLayout actions = new LinearLayout(ctx);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setPadding(dp(12), 0, dp(12), dp(12));

        final Button dump = new Button(ctx);
        dump.setText("Dump IL2CPP");
        dump.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        dump.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                dump.setEnabled(false);
                dump.setText("正在导出…");
                new Thread(new Runnable() {
                    @Override public void run() {
                        boolean success = false;
                        try {
                            success = NativeBridge.dumpNow();
                        } catch (Throwable t) {
                            Log.e(TAG, "dump failed", t);
                        }
                        final boolean result = success;
                        UI.post(new Runnable() {
                            @Override public void run() {
                                dump.setEnabled(true);
                                dump.setText("Dump IL2CPP");
                                Toast.makeText(ctx, result
                                        ? "导出完成：MobCtrl/il2cpp_dump.txt"
                                        : "导出失败，请先启动功能并查看运行日志",
                                        Toast.LENGTH_LONG).show();
                            }
                        });
                    }
                }, "MobCtrl-dump").start();
            }
        });
        actions.addView(dump, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button rerun = new Button(ctx);
        rerun.setText("重新应用");
        rerun.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        rerun.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { push(); }
        });
        actions.addView(rerun, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        card.addView(actions);

        Button start = new Button(ctx);
        start.setText("进入游戏后启动功能");
        start.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                boolean started = MainHook.startFeatures(activity);
                Toast.makeText(ctx, started ? "功能正在启动" : "功能启动失败，请查看日志",
                        Toast.LENGTH_SHORT).show();
            }
        });
        card.addView(start);

        Button copyLog = new Button(ctx);
        copyLog.setText("复制运行日志");
        copyLog.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                File dir = activity.getExternalFilesDir(null);
                File file = dir == null ? null : new File(dir, "MobCtrl/native.log");
                if (file == null || !file.isFile()) {
                    Toast.makeText(ctx, "运行日志尚未生成，请查看 LSPosed 日志", Toast.LENGTH_LONG).show();
                    return;
                }
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                        new FileInputStream(file), "UTF-8"))) {
                    ArrayDeque<String> lines = new ArrayDeque<>();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        if (lines.size() == 300) lines.removeFirst();
                        lines.addLast(line);
                    }
                    StringBuilder text = new StringBuilder();
                    for (String item : lines) text.append(item).append('\n');
                    ClipboardManager clipboard = (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                    clipboard.setPrimaryClip(ClipData.newPlainText("MobCtrl 日志", text.toString()));
                    Toast.makeText(ctx, "运行日志已复制", Toast.LENGTH_SHORT).show();
                } catch (Exception e) {
                    Log.e(TAG, "copy log failed", e);
                    Toast.makeText(ctx, "读取运行日志失败", Toast.LENGTH_SHORT).show();
                }
            }
        });
        card.addView(copyLog);

        return card;
    }

    private View buildSlider(String label, final String key, final int max,
                             final int def, final int bit, SharedPreferences sp) {
        LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, dp(6), 0, dp(2));

        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setTextColor(C_TEXT);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        row.addView(tv, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        final TextView val = new TextView(ctx);
        final int cur = sp.getInt(key, def);
        val.setText(fmt(cur));
        val.setTextColor(C_ACCENT);
        val.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        row.addView(val);

        box.addView(row);

        final SeekBar sb = new SeekBar(ctx);
        sb.setMax(max);
        sb.setProgress(Math.min(cur, max));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean fromUser) {
                val.setText(fmt(p));
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {
                Config.setInt(key, s.getProgress());
                Config.setBool(Config.keyForBit(bit), true);
                push();
            }
        });
        box.addView(sb);
        return box;
    }

    private View divider() {
        View v = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
        lp.topMargin = dp(6);
        lp.bottomMargin = dp(4);
        v.setLayoutParams(lp);
        v.setBackgroundColor(0xFF2A2A34);
        return v;
    }

    private static String fmt(int pct) {
        return (pct / 100f) + "x";
    }

    // ------------------------------------------------------------------ //

    private void toggle() { setExpanded(!expanded); }

    private void setExpanded(boolean on) {
        expanded = on;
        if (panel == null || ball == null) return;
        panel.setVisibility(on ? View.VISIBLE : View.GONE);
        ball.setVisibility(on ? View.GONE : View.VISIBLE);
        if (!on) {
            // 收起后把球摆回原处
            moveBallTo(ballX, ballY);
        }
    }

    private FrameLayout.LayoutParams frameLp(int w, int h) {
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.leftMargin = ballX < 0 ? 0 : ballX;
        lp.topMargin = ballY < 0 ? 0 : ballY;
        return lp;
    }

    private void moveBallTo(int x, int y) {
        ballX = x; ballY = y;
        if (ball == null) return;
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) ball.getLayoutParams();
        lp.leftMargin = x;
        lp.topMargin = y;
        ball.setLayoutParams(lp);
        if (panel != null) {
            FrameLayout.LayoutParams plp = (FrameLayout.LayoutParams) panel.getLayoutParams();
            plp.leftMargin = Math.max(0, Math.min(x, rootWidth() - dp(PANEL_W_DP)));
            plp.topMargin = y;
            panel.setLayoutParams(plp);
        }
    }

    private int rootWidth() {
        return root == null || root.getWidth() == 0 ? dp(PANEL_W_DP) : root.getWidth();
    }

    /** 把 UI 状态写进 SP，并通知 native 热更新。 */
    private void push() {
        try {
            int mask = Config.mask();
            NativeBridge.apply(mask,
                    Config.getInt("fire_rate_pct", 300),
                    Config.getInt("mob_multi_pct", 500),
                    Config.getInt("gate_boost_pct", 200),
                    Config.getInt("damage_pct", 1000),
                    (long) Config.getInt("currency_amount", 1000000),
                    Config.getInt("target_level", 100));
            Log.i(TAG, "push mask=0x" + Integer.toHexString(mask));
        } catch (Throwable t) {
            Log.e(TAG, "push failed", t);
        }
    }

    // ------------------------------------------------------------------ //

    /** 拖动：ball 与标题栏共用。 */
    private final class DragTouch implements View.OnTouchListener {
        private float downX, downY;
        private int startX, startY;
        private boolean moved;

        @Override public boolean onTouch(View v, MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    downX = e.getRawX(); downY = e.getRawY();
                    startX = v == ball ? ballX : panelLeft();
                    startY = v == ball ? ballY : panelTop();
                    moved = false;
                    return false;   // 交回给 View 处理 click
                case MotionEvent.ACTION_MOVE:
                    float dx = e.getRawX() - downX;
                    float dy = e.getRawY() - downY;
                    if (Math.abs(dx) > dp(4) || Math.abs(dy) > dp(4)) moved = true;
                    if (!moved) return false;
                    if (v == ball) {
                        clampMove((int) (startX + dx), (int) (startY + dy));
                    } else {
                        clampMovePanel((int) (startX + dx), (int) (startY + dy));
                    }
                    return true;
                default:
                    return false;
            }
        }
    }

    private int panelLeft() {
        if (panel == null) return 0;
        return ((FrameLayout.LayoutParams) panel.getLayoutParams()).leftMargin;
    }

    private int panelTop() {
        if (panel == null) return 0;
        return ((FrameLayout.LayoutParams) panel.getLayoutParams()).topMargin;
    }

    private void clampMove(int x, int y) {
        int w = root == null ? 0 : root.getWidth();
        int h = root == null ? 0 : root.getHeight();
        x = Math.max(0, Math.min(x, Math.max(0, w - dp(BALL_DP))));
        y = Math.max(0, Math.min(y, Math.max(0, h - dp(BALL_DP))));
        moveBallTo(x, y);
    }

    private void clampMovePanel(int x, int y) {
        int w = root == null ? 0 : root.getWidth();
        int h = root == null ? 0 : root.getHeight();
        int pw = panel == null ? 0 : panel.getWidth();
        int ph = panel == null ? 0 : panel.getHeight();
        x = Math.max(0, Math.min(x, Math.max(0, w - (pw > 0 ? pw : dp(PANEL_W_DP)))));
        y = Math.max(0, Math.min(y, Math.max(0, h - (ph > 0 ? ph : dp(300)))));
        if (panel == null) return;
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) panel.getLayoutParams();
        lp.leftMargin = x;
        lp.topMargin = y;
        panel.setLayoutParams(lp);
    }

    // ------------------------------------------------------------------ //

    private int dp(int v) {
        return (int) (v * ctx.getResources().getDisplayMetrics().density + 0.5f);
    }
}
