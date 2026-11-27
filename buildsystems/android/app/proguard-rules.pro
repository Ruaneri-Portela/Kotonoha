# SDL's native storage and display adapters look up these methods by name.
-keep class org.kotonoha.android.GameActivity {
    public *** getKotonoha*(...);
    public *** setKotonoha*(...);
    private static native void setKotonohaDiscordReady(boolean);
}
