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
        if (fromEnv != null && !fromEnv.isEmpty()) return fromEnv;
        // launcher/examples/java -> raiz do repositorio
        return new File(System.getProperty("user.dir"), "../../..")
                .getAbsoluteFile().toPath().normalize().toString();
    }

    /** Absolute path of the native core used by this example. */
    public static String corePath() {
        String override = System.getProperty("kotonoha.coreLib");
        if (override != null && !override.isEmpty()) return canonical(override);
        String nativeDir = System.getenv("KOTONOHA_NATIVE_DIR");
        if (nativeDir != null && !nativeDir.isEmpty())
            return canonical(new File(nativeDir, coreFileName()).getPath());
        String build = isWindows() ? "windows-x64-msvc-vcpkg"
                : isLinux() ? "linux-x64-vcpkg" : "macos-arm64-vcpkg";
        File core = new File(repo(), "out/build/" + build + "/lib/" + coreFileName());
        if (core.isFile()) return canonical(core.getPath());
        return canonical(new File(repo(), "buildsystems/visualstudio/out/x64/Release/"
                + coreFileName()).getPath());
    }

    private static boolean isWindows() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("win");
    }

    private static boolean isLinux() {
        return System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("linux");
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

    /** vcpkg triplet install root belonging to the selected core build. */
    private static String findVcpkgLib(String build) {
        File installed = new File(build, "vcpkg_installed");
        File[] triplets = installed.listFiles(
                file -> file.isDirectory() && !file.getName().equals("debug"));
        if (triplets != null) {
            for (File triplet : triplets) {
                File lib = new File(triplet, "lib");
                if (lib.isDirectory())
                    return canonical(lib.getPath());
            }
        }
        return installed.getPath() + "/unknown/lib";
    }

    /** Preloads @rpath dependencies, then the core with the JNI exports. */
    public static synchronized void loadCore() {
        File core = new File(corePath());
        String lib = core.getParent();
        String build = new File(lib).getParent();
        String vcpkg = findVcpkgLib(build);
        String[] dependencies;
        if (isWindows() || isLinux()) {
            // O carregador nativo resolve DLLs/so vizinhas ao core; nao ha
            // @rpath e nao e preciso pre-carregar dependencias por caminho.
            dependencies = new String[0];
        } else {
            dependencies = new String[] {
                    "/Applications/Xcode.app/Contents/Developer/Library/Frameworks/Python3.framework/Versions/3.9/Python3",
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
        }
        for (String dependency : dependencies) {
            File file = new File(dependency);
            if (file.isFile())
                System.load(canonical(dependency));
        }
        System.load(corePath());
    }
}
