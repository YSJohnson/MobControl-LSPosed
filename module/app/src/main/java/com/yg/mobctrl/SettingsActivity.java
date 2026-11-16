package com.yg.mobctrl;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * 模块说明页。
 *
 * 注意：这里**不是**配置界面。模块运行在游戏进程里，读写的是游戏的
 * SharedPreferences；而本 Activity 跑在模块自己的进程，两边 SP 不互通。
 * 所有功能开关都在游戏内的悬浮窗上调。
 */
public class SettingsActivity extends Activity {

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);

        ScrollView sv = new ScrollView(this);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int p = dp(20);
        box.setPadding(p, p, p, p);
        sv.addView(box);

        box.addView(title("MobCtrl  v1.1"));
        box.addView(body(
                "LSPosed 模块 · Mob Control 3.25.x\n\n"
                + "这个 App 本身没有界面功能 —— 所有开关都在**游戏内**的悬浮球上调。"));
        box.addView(gap());
        box.addView(title("用法"));
        box.addView(body(
                "1. 在 LSPosed 里启用本模块\n"
                + "2. 作用域只勾 Mob Control\n"
                + "3. 强制停止游戏后重新打开\n"
                + "4. 游戏画面右侧会出现一个圆形「MC」按钮\n"
                + "5. 点它展开面板，右上角「—」收起，按住可拖动"));
        box.addView(gap());
        box.addView(title("面板功能"));
        box.addView(body(
                "官方 Cheats 解锁 / 射速倍率 / 跳过插屏广告 /\n"
                + "无限金币 / 无限跳过卷 / Dump IL2CPP"));
        box.addView(gap());
        box.addView(title("日志"));
        box.addView(body(
                "adb logcat -s MobCtrl\n"
                + "文件：Android/data/com.vincentb.MobControl/files/MobCtrl/"));
        box.addView(gap());

        Button open = new Button(this);
        open.setText("打开 LSPosed");
        open.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                try {
                    Intent i = getPackageManager()
                            .getLaunchIntentForPackage("org.lsposed.manager");
                    if (i != null) startActivity(i);
                } catch (Throwable ignored) {}
            }
        });
        box.addView(open);

        setContentView(sv);
    }

    private TextView title(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        t.setTypeface(null, 1);
        t.setPadding(0, dp(10), 0, dp(6));
        return t;
    }

    private TextView body(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        t.setLineSpacing(dp(4), 1f);
        return t;
    }

    private View gap() {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(8)));
        return v;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }
}
