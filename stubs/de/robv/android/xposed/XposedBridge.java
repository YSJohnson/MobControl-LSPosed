package de.robv.android.xposed;

import java.lang.reflect.Member;

public final class XposedBridge {

    private XposedBridge() {}

    public static void log(String text) {}

    public static void log(Throwable t) {}

    public static void log(String text, Throwable t) {}

    public static <T extends Member> T setObjectClass(Member m, Class<?> c) { return null; }
}
