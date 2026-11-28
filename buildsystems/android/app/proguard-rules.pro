# SDL's native storage and display adapters look up these methods by name.
-keep class me.hirameki.kotonoha.android.GameActivity {
    public *** getKotonoha*(...);
    public *** setKotonoha*(...);
    private static native void setKotonohaDiscordReady(boolean);
}
# JNI reads native School Days launcher art before SDL initialization.
-keep class org.kotonoha.android.SchoolDaysEmbeddedAssetsBridge {
    private static native byte[] nativeRead(java.lang.String);
}
