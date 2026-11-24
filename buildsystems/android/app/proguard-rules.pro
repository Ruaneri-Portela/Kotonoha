# SDL's native storage and display adapters look up these methods by name.
-keep class me.hirameki.kotonoha.android.GameActivity {
    public *** getKotonoha*(...);
    public *** setKotonoha*(...);
}
