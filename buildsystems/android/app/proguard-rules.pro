# SDL's native storage and display adapters look up these methods by name.
-keepclassmembers class org.kotonoha.android.GameActivity {
    public *** getKotonoha*(...);
    public *** setKotonoha*(...);
}
