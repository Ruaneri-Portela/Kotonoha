// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
package me.hirameki.kotonoha.launcher;

import java.io.File;
import java.util.Locale;

/**
 * Loads the real KotonohaLauncherCore dylib from a local CMake build.
 *
 * <p>The macOS core references its dependencies via @rpath but has no
 * LC_RPATH, and the JVM ignores DYLD_LIBRARY_PATH under hardened runtime.
 * Preloading each dependency by absolute path lets dyld reuse it when the
 * core is loaded, without patching or copying the binary.</p>
 */
public final class NativeLibraries {
    private NativeLibraries() {}

    public static String repo() {
        String fromEnv = System.getenv("KOTONOHA_REPO");
        return fromEnv != null && !fromEnv.isEmpty()
                ? fromEnv
                : "/Users/nero/Codes/Kotonoha";
    }

    /** Absolute path of the native core used by this example. */
    public static String corePath() {
        String override = System.getProperty("kotonoha.coreLib");
        if (override != null && !override.isEmpty())
            return canonical(override);
        return canonical(repo() + "/out/build/macos-arm64-vcpkg/lib/"
                + coreFileName());
    }

    private static String coreFileName() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (os.contains("win")) return "KotonohaLauncherCore.dll";
        if (os.contains("linux")) return "libKotonohaLauncherCore.so";
        return "libKotonohaLauncherCore.dylib";
    }

    private static String canonical(String path) {
        return new File(path).getAbsoluteFile().toPath().normalize().toString();
    }

    /** Preloads @rpath dependencies, then the core with the JNI exports. */
    public static synchronized void loadCore() {
        String build = repo() + "/out/build/macos-arm64-vcpkg";
        String lib = build + "/lib";
        String vcpkg = build + "/vcpkg_installed/macos-arm64-vcpkg/lib";
        String[] dependencies = {
                vcpkg + "/libSDL3.0.dylib",
                vcpkg + "/libz.1.3.2.dylib",
                lib + "/libKotonohaEngine.dylib",
                vcpkg + "/libSDL3_ttf.0.2.2.dylib",
                vcpkg + "/libavformat.63.1.102.dylib",
                vcpkg + "/libavcodec.63.1.102.dylib",
                vcpkg + "/libswscale.10.1.102.dylib",
                vcpkg + "/libswresample.7.1.102.dylib",
                vcpkg + "/libavutil.61.1.102.dylib",
                lib + "/libImgui.dylib",
        };
        for (String dependency : dependencies) {
            File file = new File(dependency);
            if (file.isFile())
                System.load(canonical(dependency));
        }
        System.load(corePath());
    }
}
