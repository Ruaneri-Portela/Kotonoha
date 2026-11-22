package org.kotonoha.android;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.view.Display;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * Portrait Android launcher for School Days HQ v1.02 with functional parity
 * to the Windows SchoolDaysLauncher, writing the exact shared settings.json,
 * save/Config.DAT, and save/Kotonoha.ini consumed by the shared C++ runtime.
 */
public final class SchoolDaysSetup extends Activity {
    private static final int PICK_FOLDER = 1;
    private static final int PLAY = 2;

    private static final int TAB_DISPLAY = 0;
    private static final int TAB_GRAPHICS = 1;
    private static final int TAB_AUDIO = 2;
    private static final int TAB_GENERAL = 3;
    private static final int TAB_LANGUAGE = 4;
    private static final int TAB_FILES = 5;

    private static final int[] UPSCALER_VALUES = {
            SchoolDaysConfigStore.UPSCALE_AUTO,
            SchoolDaysConfigStore.UPSCALE_NONE,
            SchoolDaysConfigStore.UPSCALE_LINEAR,
            SchoolDaysConfigStore.UPSCALE_MPV
    };
    private static final String[] UPSCALER_LABELS = {"Auto", "None", "Linear", "MPV"};
    private static final int[] FPS_VALUES = {0, 30, 60};

    private static final class DisplayModeItem {
        final int width;
        final int height;
        final float hz;
        final String label;

        DisplayModeItem(int width, int height, float hz, String label) {
            this.width = width;
            this.height = height;
            this.hz = hz;
            this.label = label;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final SchoolDaysTitleBgmPlayer titleBgm = new SchoolDaysTitleBgmPlayer();

    private DocumentTree source;
    private SchoolDaysConfigStore configStore;
    private StartupNotice notice;
    private Future<?> scan;
    private int generation;

    private boolean optionsOpen;
    private int activeTab = TAB_DISPLAY;
    private boolean gameFailed;
    private int failedStage;
    private boolean privateReady;
    private boolean preparing;

    private SchoolDaysFolder folder;
    private File session;
    private Bitmap packagedBannerArtwork;

    private String savedLanguageSelection;
    private String draftLanguageSelection;
    private SchoolDaysConfigStore.Snapshot savedConfig;
    private SchoolDaysConfigStore.Snapshot draftConfig;
    private boolean bindingUi;

    private final List<DisplayModeItem> displayModes = new ArrayList<>();

    private Button[] tabButtons;
    private View[] tabPages;

    private Spinner spinDisplayMode;
    private Switch swVsync;
    private Spinner spinAspect;
    private Spinner spinFpsLimit;

    private TextView lblRenderScale;
    private SeekBar seekRenderScale;
    private Spinner spinUpscaler;
    private TextView lblUpscaleFactor;
    private SeekBar seekUpscaleFactor;
    private Spinner spinPreset;

    private TextView lblBgmVol;
    private SeekBar seekBgmVol;
    private TextView lblSeVol;
    private SeekBar seekSeVol;
    private TextView lblVoiceVol;
    private SeekBar seekVoiceVol;
    private Switch swMenVoice;
    private Switch swMute;
    private TextView lblMasterVol;
    private SeekBar seekMasterVol;
    private TextView lblMovieVol;
    private SeekBar seekMovieVol;
    private Switch swLauncherMusic;

    private Switch swTextView;
    private Switch swSkip;
    private Switch swSuperSkip;
    private Switch swAutoDraw;
    private Switch swAutoMode;
    private Switch swSwapAb;
    private Switch swForcedSubs;
    private Switch swEnableLxh;
    private TextView lblLxhStatus;

    private Button homeMusicToggle;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!isTaskRoot() && getIntent() != null
                && Intent.ACTION_MAIN.equals(getIntent().getAction())
                && getIntent().hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        setContentView(R.layout.school_days_setup);

        source = new DocumentTree(this);
        configStore = new SchoolDaysConfigStore(this);
        notice = new StartupNotice(findViewById(R.id.status));

        String rawLocale = getPreferences(MODE_PRIVATE).getString("languageSelection", null);
        if (rawLocale == null) {
            String legacy = getPreferences(MODE_PRIVATE).getString("locale", "en");
            rawLocale = SchoolDaysFolder.formatLanguageSelection("json", legacy);
        }
        savedLanguageSelection = rawLocale;
        draftLanguageSelection = rawLocale;

        boolean launcherMusicPref = getPreferences(MODE_PRIVATE).getBoolean("launcherMusic", true);
        savedConfig = configStore.load(launcherMusicPref);
        draftConfig = savedConfig.copy();

        if (state != null && state.getString("session") != null) {
            session = new File(getCacheDir(), state.getString("session"));
        }

        String versionText = getString(R.string.sd_info, BuildConfig.VERSION_NAME);
        ((TextView) findViewById(R.id.version)).setText(versionText);
        ((TextView) findViewById(R.id.home_version)).setText(versionText);

        packagedBannerArtwork = loadPackagedBannerArtwork();
        showArt(null);

        bindControls();
        populateStaticSpinners();
        bindListeners();
        syncUiFromDraft();

        gameFailed = state != null && state.getBoolean("gameFailed");
        failedStage = state == null ? 0 : state.getInt("failedStage");
        activeTab = state == null ? TAB_DISPLAY : state.getInt("activeTab", TAB_DISPLAY);
        selectTab(activeTab);
        panel(state != null && state.getBoolean("options"));
    }

    private void bindControls() {
        tabButtons = new Button[]{
                findViewById(R.id.tab_display),
                findViewById(R.id.tab_graphics),
                findViewById(R.id.tab_audio),
                findViewById(R.id.tab_general),
                findViewById(R.id.tab_language),
                findViewById(R.id.tab_files)
        };
        tabPages = new View[]{
                findViewById(R.id.page_display),
                findViewById(R.id.page_graphics),
                findViewById(R.id.page_audio),
                findViewById(R.id.page_general),
                findViewById(R.id.page_language),
                findViewById(R.id.page_files)
        };

        spinDisplayMode = findViewById(R.id.spin_display_mode);
        swVsync = findViewById(R.id.sw_vsync);
        spinAspect = findViewById(R.id.spin_aspect);
        spinFpsLimit = findViewById(R.id.spin_fps_limit);

        lblRenderScale = findViewById(R.id.lbl_render_scale);
        seekRenderScale = findViewById(R.id.seek_render_scale);
        spinUpscaler = findViewById(R.id.spin_upscaler);
        lblUpscaleFactor = findViewById(R.id.lbl_upscale_factor);
        seekUpscaleFactor = findViewById(R.id.seek_upscale_factor);
        spinPreset = findViewById(R.id.spin_preset);

        lblBgmVol = findViewById(R.id.lbl_bgm_vol);
        seekBgmVol = findViewById(R.id.seek_bgm_vol);
        lblSeVol = findViewById(R.id.lbl_se_vol);
        seekSeVol = findViewById(R.id.seek_se_vol);
        lblVoiceVol = findViewById(R.id.lbl_voice_vol);
        seekVoiceVol = findViewById(R.id.seek_voice_vol);
        swMenVoice = findViewById(R.id.sw_men_voice);
        swMute = findViewById(R.id.sw_mute);
        lblMasterVol = findViewById(R.id.lbl_master_vol);
        seekMasterVol = findViewById(R.id.seek_master_vol);
        lblMovieVol = findViewById(R.id.lbl_movie_vol);
        seekMovieVol = findViewById(R.id.seek_movie_vol);
        swLauncherMusic = findViewById(R.id.sw_launcher_music);

        swTextView = findViewById(R.id.sw_text_view);
        swSkip = findViewById(R.id.sw_skip);
        swSuperSkip = findViewById(R.id.sw_super_skip);
        swAutoDraw = findViewById(R.id.sw_auto_draw);
        swAutoMode = findViewById(R.id.sw_auto_mode);
        swSwapAb = findViewById(R.id.sw_swap_ab);
        swForcedSubs = findViewById(R.id.sw_forced_subs);
        swEnableLxh = findViewById(R.id.sw_enable_lxh);
        lblLxhStatus = findViewById(R.id.lbl_lxh_status);

        homeMusicToggle = findViewById(R.id.home_music_toggle);
    }

    private void populateStaticSpinners() {
        displayModes.clear();
        displayModes.add(new DisplayModeItem(0, 0, 0.0f, getString(R.string.sd_device_default)));
        try {
            Display display = getWindowManager().getDefaultDisplay();
            if (display != null) {
                Map<String, DisplayModeItem> unique = new LinkedHashMap<>();
                for (Display.Mode m : display.getSupportedModes()) {
                    // Normalize to landscape dimensions so portrait launcher and
                    // landscape GameActivity match the same fullscreenWidth x fullscreenHeight.
                    int rawW = m.getPhysicalWidth();
                    int rawH = m.getPhysicalHeight();
                    int w = Math.max(rawW, rawH);
                    int h = Math.min(rawW, rawH);
                    float hz = m.getRefreshRate();
                    int roundedHz = Math.round(hz);
                    String key = w + "x" + h + "@" + roundedHz;
                    if (!unique.containsKey(key)) {
                        String label = String.format(Locale.US, "%d x %d @ %d Hz", w, h, roundedHz);
                        unique.put(key, new DisplayModeItem(w, h, hz, label));
                    }
                }
                displayModes.addAll(unique.values());
            }
        } catch (RuntimeException ignored) {
        }
        setSpinnerItems(spinDisplayMode, displayModes);

        setSpinnerItems(spinAspect, new String[]{
                getString(R.string.sd_aspect_auto),
                getString(R.string.sd_aspect_43),
                getString(R.string.sd_aspect_169)
        });

        setSpinnerItems(spinFpsLimit, new String[]{
                getString(R.string.sd_fps_unlimited),
                getString(R.string.sd_fps_30),
                getString(R.string.sd_fps_60)
        });

        setSpinnerItems(spinUpscaler, UPSCALER_LABELS);

        String[] presetLabels = new String[SchoolDaysConfigStore.PRESET_OPTIONS.length];
        for (int i = 0; i < SchoolDaysConfigStore.PRESET_OPTIONS.length; i++) {
            presetLabels[i] = SchoolDaysConfigStore.PRESET_OPTIONS[i].label;
        }
        setSpinnerItems(spinPreset, presetLabels);
    }

    private <T> void setSpinnerItems(Spinner spinner, T[] items) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
    }

    private <T> void setSpinnerItems(Spinner spinner, List<T> items) {
        ArrayAdapter<T> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
    }

    private void bindListeners() {
        for (int i = 0; i < tabButtons.length; i++) {
            final int index = i;
            tabButtons[i].setOnClickListener(v -> selectTab(index));
        }

        findViewById(R.id.play).setOnClickListener(v -> onPlayClicked());
        findViewById(R.id.options).setOnClickListener(v -> openOptions(activeTab));
        findViewById(R.id.quit).setOnClickListener(v -> {
            titleBgm.stop();
            finish();
        });
        findViewById(R.id.pick_home).setOnClickListener(v -> {
            openOptions(TAB_FILES);
            launchPicker();
        });
        homeMusicToggle.setOnClickListener(v -> {
            boolean next = !savedConfig.launcherMusic;
            savedConfig.launcherMusic = next;
            draftConfig.launcherMusic = next;
            getPreferences(MODE_PRIVATE).edit().putBoolean("launcherMusic", next).apply();
            updateHomeMusicButton();
            syncUiFromDraft();
            updateLauncherMusicState();
        });

        findViewById(R.id.pick).setOnClickListener(v -> launchPicker());
        findViewById(R.id.retry).setOnClickListener(v -> {
            gameFailed = false;
            refresh();
        });
        findViewById(R.id.forget).setOnClickListener(v -> {
            try {
                source.forget();
                titleBgm.release();
                gameFailed = false;
                refresh();
            } catch (SecurityException error) {
                notice.show(getText(R.string.sd_denied));
            }
        });

        findViewById(R.id.btn_ok).setOnClickListener(v -> applyOptions());
        findViewById(R.id.btn_cancel).setOnClickListener(v -> cancelOptions());
        findViewById(R.id.btn_reset).setOnClickListener(v -> resetDraftOptions());

        // Display controls
        spinDisplayMode.setOnItemSelectedListener(onSelected(pos -> {
            DisplayModeItem item = displayModes.get(pos);
            draftConfig.fullscreenWidth = item.width;
            draftConfig.fullscreenHeight = item.height;
            draftConfig.refreshRate = item.hz;
        }));
        swVsync.setOnCheckedChangeListener((b, checked) -> {
            if (!bindingUi) draftConfig.vsync = checked;
        });
        spinAspect.setOnItemSelectedListener(onSelected(pos -> draftConfig.contentAspectPreference = pos));
        spinFpsLimit.setOnItemSelectedListener(onSelected(pos -> draftConfig.androidFrameLimit = FPS_VALUES[pos]));

        // Graphics controls
        seekRenderScale.setOnSeekBarChangeListener(onSeek(progress -> {
            draftConfig.renderScale = 0.25f + progress * 0.05f;
            lblRenderScale.setText(getString(R.string.sd_render_scale,
                    String.format(Locale.US, "%.2fx", draftConfig.renderScale)));
        }));
        spinUpscaler.setOnItemSelectedListener(onSelected(pos -> draftConfig.upscaleBackend = UPSCALER_VALUES[pos]));
        seekUpscaleFactor.setOnSeekBarChangeListener(onSeek(progress -> {
            draftConfig.upscaleFactor = 1.0f + progress * 0.25f;
            lblUpscaleFactor.setText(getString(R.string.sd_upscale_factor,
                    String.format(Locale.US, "%.2fx", draftConfig.upscaleFactor)));
        }));
        spinPreset.setOnItemSelectedListener(onSelected(pos -> {
            int chosenPreset = SchoolDaysConfigStore.PRESET_OPTIONS[pos].id;
            if (draftConfig.upscalePreset != chosenPreset) {
                draftConfig.upscalePreset = chosenPreset;
                // Parity with SchoolDaysUpscalingPolicy::SelectPreset
                draftConfig.upscaleBackend = SchoolDaysConfigStore.UPSCALE_MPV;
                if (draftConfig.upscaleFactor < 2.0f) {
                    draftConfig.upscaleFactor = 2.0f;
                }
                syncUiFromDraft();
            }
        }));

        // Audio controls
        seekBgmVol.setOnSeekBarChangeListener(onSeek(p -> {
            draftConfig.bgmVolume = p;
            lblBgmVol.setText(getString(R.string.sd_bgm_vol, p));
        }));
        seekSeVol.setOnSeekBarChangeListener(onSeek(p -> {
            draftConfig.seVolume = p;
            lblSeVol.setText(getString(R.string.sd_se_vol, p));
        }));
        seekVoiceVol.setOnSeekBarChangeListener(onSeek(p -> {
            draftConfig.voiceVolume = p;
            lblVoiceVol.setText(getString(R.string.sd_voice_vol, p));
        }));
        swMenVoice.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.menVoice = c;
        });
        swMute.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.mute = c;
        });
        seekMasterVol.setOnSeekBarChangeListener(onSeek(p -> {
            draftConfig.masterVolume = p / 100.0f;
            lblMasterVol.setText(getString(R.string.sd_master_vol, p));
        }));
        seekMovieVol.setOnSeekBarChangeListener(onSeek(p -> {
            draftConfig.movieVolume = p / 100.0f;
            lblMovieVol.setText(getString(R.string.sd_movie_vol, p));
        }));
        swLauncherMusic.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) {
                draftConfig.launcherMusic = c;
                // Live preview launcher music immediately (reverted if Cancel is tapped)
                if (c) startLauncherMusicIfReady();
                else titleBgm.pause();
            }
        });

        // General controls
        swTextView.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.textView = c;
        });
        swSkip.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.skip = c;
        });
        swSuperSkip.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.superSkip = c;
        });
        swAutoDraw.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.autoDraw = c;
        });
        swAutoMode.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.autoMode = c;
        });
        swSwapAb.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.swapControllerAB = c;
        });
        swForcedSubs.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) draftConfig.enableForcedSubtitles = c;
        });
        swEnableLxh.setOnCheckedChangeListener((b, c) -> {
            if (!bindingUi) {
                draftConfig.enableLxh = c;
                updateLxhStatusLabel();
                rebuildLanguageChoices();
            }
        });
    }

    private boolean wantsLxh(SchoolDaysConfigStore.Snapshot cfg) {
        if (cfg != null && cfg.enableLxh != null) {
            return cfg.enableLxh;
        }
        return folder != null && folder.hasLxhPack;
    }

    private boolean effectiveLxh(SchoolDaysConfigStore.Snapshot cfg, String selection) {
        return wantsLxh(cfg) && folder != null && folder.supportsLxhForSelection(selection);
    }

    private String lxhStateLabel(SchoolDaysConfigStore.Snapshot cfg, String selection) {
        if (!wantsLxh(cfg)) {
            return getString(R.string.sd_lxh_state_off);
        }
        if (folder == null || !folder.hasLxhPack) {
            return getString(R.string.sd_lxh_state_missing_pack);
        }
        if (!folder.supportsLxhForSelection(selection)) {
            return getString(R.string.sd_lxh_state_missing_lang);
        }
        return getString(R.string.sd_lxh_state_active);
    }

    private void updateLxhStatusLabel() {
        if (lblLxhStatus != null) {
            lblLxhStatus.setText(lxhStateLabel(draftConfig, draftLanguageSelection));
        }
    }

    private interface IntConsumer {
        void accept(int value);
    }

    private AdapterView.OnItemSelectedListener onSelected(IntConsumer action) {
        return new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
                if (!bindingUi) action.accept(pos);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        };
    }

    private SeekBar.OnSeekBarChangeListener onSeek(IntConsumer action) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (!bindingUi) action.accept(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        };
    }

    private void syncUiFromDraft() {
        bindingUi = true;
        try {
            int modeIdx = 0;
            if (draftConfig.fullscreenWidth > 0 && draftConfig.fullscreenHeight > 0) {
                int targetW = Math.max(draftConfig.fullscreenWidth, draftConfig.fullscreenHeight);
                int targetH = Math.min(draftConfig.fullscreenWidth, draftConfig.fullscreenHeight);
                for (int i = 1; i < displayModes.size(); i++) {
                    DisplayModeItem item = displayModes.get(i);
                    if (item.width == targetW
                            && item.height == targetH
                            && Math.abs(item.hz - draftConfig.refreshRate) < 0.5f) {
                        modeIdx = i;
                        break;
                    }
                }
            }
            spinDisplayMode.setSelection(modeIdx);
            swVsync.setChecked(draftConfig.vsync);
            spinAspect.setSelection(Math.max(0, Math.min(2, draftConfig.contentAspectPreference)));
            int fpsIdx = draftConfig.androidFrameLimit == 30 ? 1
                    : (draftConfig.androidFrameLimit == 60 ? 2 : 0);
            spinFpsLimit.setSelection(fpsIdx);

            int rsStep = Math.max(0, Math.min(15, Math.round((draftConfig.renderScale - 0.25f) / 0.05f)));
            seekRenderScale.setProgress(rsStep);
            lblRenderScale.setText(getString(R.string.sd_render_scale,
                    String.format(Locale.US, "%.2fx", draftConfig.renderScale)));

            int upscalerIdx = 2; // Linear default
            for (int i = 0; i < UPSCALER_VALUES.length; i++) {
                if (UPSCALER_VALUES[i] == draftConfig.upscaleBackend) {
                    upscalerIdx = i;
                    break;
                }
            }
            spinUpscaler.setSelection(upscalerIdx);

            int ufStep = Math.max(0, Math.min(12, Math.round((draftConfig.upscaleFactor - 1.0f) / 0.25f)));
            seekUpscaleFactor.setProgress(ufStep);
            lblUpscaleFactor.setText(getString(R.string.sd_upscale_factor,
                    String.format(Locale.US, "%.2fx", draftConfig.upscaleFactor)));

            int presetIdx = 0;
            for (int i = 0; i < SchoolDaysConfigStore.PRESET_OPTIONS.length; i++) {
                if (SchoolDaysConfigStore.PRESET_OPTIONS[i].id == draftConfig.upscalePreset) {
                    presetIdx = i;
                    break;
                }
            }
            spinPreset.setSelection(presetIdx);

            seekBgmVol.setProgress(draftConfig.bgmVolume);
            lblBgmVol.setText(getString(R.string.sd_bgm_vol, draftConfig.bgmVolume));
            seekSeVol.setProgress(draftConfig.seVolume);
            lblSeVol.setText(getString(R.string.sd_se_vol, draftConfig.seVolume));
            seekVoiceVol.setProgress(draftConfig.voiceVolume);
            lblVoiceVol.setText(getString(R.string.sd_voice_vol, draftConfig.voiceVolume));
            swMenVoice.setChecked(draftConfig.menVoice);
            swMute.setChecked(draftConfig.mute);

            int masterPct = Math.max(0, Math.min(100, Math.round(draftConfig.masterVolume * 100.0f)));
            seekMasterVol.setProgress(masterPct);
            lblMasterVol.setText(getString(R.string.sd_master_vol, masterPct));
            int moviePct = Math.max(0, Math.min(100, Math.round(draftConfig.movieVolume * 100.0f)));
            seekMovieVol.setProgress(moviePct);
            lblMovieVol.setText(getString(R.string.sd_movie_vol, moviePct));
            swLauncherMusic.setChecked(draftConfig.launcherMusic);

            swTextView.setChecked(draftConfig.textView);
            swSkip.setChecked(draftConfig.skip);
            swSuperSkip.setChecked(draftConfig.superSkip);
            swAutoDraw.setChecked(draftConfig.autoDraw);
            swAutoMode.setChecked(draftConfig.autoMode);
            swSwapAb.setChecked(draftConfig.swapControllerAB);
            swForcedSubs.setChecked(draftConfig.enableForcedSubtitles);
            swEnableLxh.setChecked(wantsLxh(draftConfig));
            updateLxhStatusLabel();

            updateHomeMusicButton();
            rebuildLanguageChoices();
        } finally {
            bindingUi = false;
        }
    }

    private void updateHomeMusicButton() {
        homeMusicToggle.setText(savedConfig.launcherMusic
                ? R.string.sd_launcher_music_btn_on
                : R.string.sd_launcher_music_btn_off);
    }

    private void selectTab(int tab) {
        activeTab = Math.max(0, Math.min(tabPages.length - 1, tab));
        for (int i = 0; i < tabPages.length; i++) {
            boolean selected = (i == activeTab);
            tabPages[i].setVisibility(selected ? View.VISIBLE : View.GONE);
            tabButtons[i].setBackgroundTintList(ColorStateList.valueOf(selected ? 0xFF18517E : 0xFFDCEBF2));
            tabButtons[i].setTextColor(selected ? 0xFFFFFFFF : 0xFF123454);
        }
    }

    private void openOptions(int tab) {
        savedConfig = configStore.load(getPreferences(MODE_PRIVATE).getBoolean("launcherMusic", true));
        draftConfig = savedConfig.copy();
        draftLanguageSelection = savedLanguageSelection;
        syncUiFromDraft();
        selectTab(tab);
        panel(true);
    }

    private void applyOptions() {
        try {
            boolean clearJson = SchoolDaysFolder.isGpkSelection(draftLanguageSelection);
            configStore.save(draftConfig, savedConfig, clearJson);
            savedConfig = draftConfig.copy();
            savedLanguageSelection = draftLanguageSelection;
            getPreferences(MODE_PRIVATE).edit()
                    .putString("languageSelection", savedLanguageSelection)
                    .putString("locale", SchoolDaysFolder.selectionLocale(savedLanguageSelection))
                    .putBoolean("launcherMusic", savedConfig.launcherMusic)
                    .apply();
            gameFailed = false;
            updateHomeMusicButton();
            updateLauncherMusicState();
            Toast.makeText(this, R.string.sd_saved_ok, Toast.LENGTH_SHORT).show();
            panel(false);
            updateStatusBanner();
        } catch (IOException error) {
            Toast.makeText(this, R.string.sd_save_error, Toast.LENGTH_LONG).show();
        }
    }

    private void cancelOptions() {
        draftConfig = savedConfig.copy();
        draftLanguageSelection = savedLanguageSelection;
        syncUiFromDraft();
        updateLauncherMusicState();
        panel(false);
    }

    private void resetDraftOptions() {
        draftConfig = SchoolDaysConfigStore.Snapshot.defaults();
        syncUiFromDraft();
        Toast.makeText(this, R.string.sd_reset_done, Toast.LENGTH_SHORT).show();
    }

    private void onPlayClicked() {
        if (preparing) return;
        if (folder != null && folder.missing.isEmpty()) {
            if (!folder.supportsSelection(savedLanguageSelection)) {
                openOptions(TAB_LANGUAGE);
                return;
            }
            launchFolder();
        } else if (privateReady && source.selected() == null) {
            titleBgm.stop();
            gameFailed = false;
            startActivityForResult(new Intent(this, BootstrapActivity.class), PLAY);
        } else {
            openOptions(TAB_FILES);
            refresh();
        }
    }

    private void launchPicker() {
        try {
            startActivityForResult(DocumentTree.picker(), PICK_FOLDER);
        } catch (ActivityNotFoundException e) {
            notice.show(getText(R.string.sd_picker_error));
        }
    }

    private int failureMessage() {
        switch (failedStage) {
            case 1: return R.string.sd_failed_packs;
            case 2: return R.string.sd_failed_route;
            case 4: return R.string.sd_failed_language;
            default: return R.string.sd_game_failed;
        }
    }

    private static String localeDisplayName(String locale) {
        if ("en".equals(locale)) return "English";
        if ("pt-BR".equals(locale)) return "Português (Brasil)";
        if ("es".equals(locale)) return "Español";
        if ("ru".equals(locale)) return "Russian";
        if ("zh-CN".equals(locale)) return "Chinese - Simplified";
        return locale;
    }

    private String formatSelectionLabel(String selection) {
        String loc = SchoolDaysFolder.selectionLocale(selection);
        String backend = SchoolDaysFolder.isGpkSelection(selection) ? "GPK" : "JSON";
        return localeDisplayName(loc) + " [" + backend + "]";
    }

    private void rebuildLanguageChoices() {
        TextView active = findViewById(R.id.lang_active);
        active.setText(getString(R.string.sd_lang_active, formatSelectionLabel(draftLanguageSelection)));

        TextView incomplete = findViewById(R.id.lang_incomplete);
        if (folder != null && !folder.incompleteJsonLanguages.isEmpty()) {
            incomplete.setVisibility(View.VISIBLE);
            incomplete.setText(getString(R.string.sd_incomplete_json,
                    String.join(", ", folder.incompleteJsonLanguages)));
        } else {
            incomplete.setVisibility(View.GONE);
        }

        LinearLayout gpkList = findViewById(R.id.lang_gpk_list);
        LinearLayout jsonList = findViewById(R.id.lang_json_list);
        gpkList.removeAllViews();
        jsonList.removeAllViews();

        if (folder == null) return;

        for (String loc : SchoolDaysFolder.SUPPORTED_LOCALES) {
            boolean gpkReady = folder.baseGpkLanguages.contains(loc);
            boolean jsonReady = folder.baseJsonLanguages.contains(loc);
            String gpkKey = SchoolDaysFolder.formatLanguageSelection("gpk", loc);
            Button gpkBtn = new Button(this, null, 0, R.style.SdButton);
            boolean gpkSelected = gpkKey.equals(draftLanguageSelection);
            gpkBtn.setText((gpkSelected ? "✓ " : "") + localeDisplayName(loc) + " (GPK)");
            gpkBtn.setEnabled(gpkReady);
            gpkBtn.setAlpha(gpkReady ? 1.0f : 0.45f);
            gpkBtn.setBackgroundTintList(ColorStateList.valueOf(gpkSelected ? 0xFF18517E : 0xFFDCEBF2));
            gpkBtn.setTextColor(gpkSelected ? 0xFFFFFFFF : 0xFF123454);
            gpkBtn.setOnClickListener(v -> {
                draftLanguageSelection = gpkKey;
                updateLxhStatusLabel();
                rebuildLanguageChoices();
            });
            gpkList.addView(gpkBtn);

            if (!gpkReady) {
                TextView reason = new TextView(this);
                reason.setTextSize(12f);
                reason.setTextColor(0xFF566B7A);
                reason.setPadding(8, 0, 8, 12);
                if ("zh-CN".equals(loc) && folder.hasLegacyIncompatibleChineseGpk()) {
                    reason.setText(R.string.sd_chinese_legacy_gpk);
                } else {
                    List<String> absent = folder.missingGpkFiles(loc, false);
                    String joined = String.join(", ", absent);
                    reason.setText(getString(jsonReady
                            ? R.string.sd_missing_gpk_json_hint
                            : R.string.sd_missing_gpk_files, joined));
                }
                gpkList.addView(reason);
            }

            String jsonKey = SchoolDaysFolder.formatLanguageSelection("json", loc);
            Button jsonBtn = new Button(this, null, 0, R.style.SdButton);
            boolean jsonSelected = jsonKey.equals(draftLanguageSelection);
            jsonBtn.setText((jsonSelected ? "✓ " : "") + localeDisplayName(loc) + " (JSON)");
            jsonBtn.setEnabled(jsonReady);
            jsonBtn.setAlpha(jsonReady ? 1.0f : 0.45f);
            jsonBtn.setBackgroundTintList(ColorStateList.valueOf(jsonSelected ? 0xFF18517E : 0xFFDCEBF2));
            jsonBtn.setTextColor(jsonSelected ? 0xFFFFFFFF : 0xFF123454);
            jsonBtn.setOnClickListener(v -> {
                draftLanguageSelection = jsonKey;
                updateLxhStatusLabel();
                rebuildLanguageChoices();
            });
            jsonList.addView(jsonBtn);
        }
    }

    private void launchFolder() {
        final SchoolDaysFolder selected = folder;
        final Uri tree = source.selected();
        final String selection = savedLanguageSelection;
        final boolean launchLxh = effectiveLxh(savedConfig, selection);
        final int token = generation;
        preparing = true;
        gameFailed = false;
        titleBgm.stop();
        notice.show(getText(R.string.sd_preparing));
        worker.submit(() -> {
            try {
                if (tree == null || !source.canRead(tree)) throw new SecurityException();
                if (SchoolDaysFolder.isGpkSelection(selection)) {
                    configStore.clearStoredTranslationPath();
                }
                File ready = selected.prepare(this, source, selection, launchLxh);
                runOnUiThread(() -> {
                    preparing = false;
                    if (isFinishing() || isDestroyed() || generation != token) {
                        SchoolDaysFolder.discard(ready);
                        return;
                    }
                    session = ready;
                    Intent start = new Intent(this, BootstrapActivity.class);
                    start.putExtra(GameActivity.LAUNCH_ARGUMENTS,
                            SchoolDaysFolder.launchArguments(session, selection, launchLxh));
                    startActivityForResult(start, PLAY);
                });
            } catch (IOException | RuntimeException error) {
                runOnUiThread(() -> {
                    preparing = false;
                    if (generation == token) {
                        notice.show(getText(error instanceof SecurityException
                                ? R.string.sd_denied : R.string.sd_unreadable));
                    }
                });
            }
        });
    }

    private void panel(boolean open) {
        optionsOpen = open;
        findViewById(R.id.home).setVisibility(open ? View.GONE : View.VISIBLE);
        findViewById(R.id.settings).setVisibility(open ? View.VISIBLE : View.GONE);
        findViewById(R.id.options_footer).setVisibility(open ? View.VISIBLE : View.GONE);
        TextView subtitle = findViewById(R.id.title_subtitle);
        if (subtitle != null) {
            subtitle.setText(open ? "SETUP / OPTIONS" : "LAUNCHER");
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!optionsOpen) {
            savedConfig = configStore.load(getPreferences(MODE_PRIVATE).getBoolean("launcherMusic", true));
            draftConfig = savedConfig.copy();
            syncUiFromDraft();
        }
        refresh();
    }

    @Override
    protected void onPause() {
        titleBgm.pause();
        super.onPause();
    }

    private void updateLauncherMusicState() {
        boolean enabled = optionsOpen ? draftConfig.launcherMusic : savedConfig.launcherMusic;
        if (enabled) startLauncherMusicIfReady();
        else titleBgm.pause();
    }

    private void startLauncherMusicIfReady() {
        if (preparing || isFinishing() || isDestroyed()) return;
        SchoolDaysFolder current = folder;
        if (current == null) return;
        DocumentTree.Entry ini = current.archiveEntry("Ini.GPK");
        DocumentTree.Entry bgm = current.archiveEntry("BGM.GPK");
        if (ini == null || bgm == null) return;
        final Uri iniUri = ini.uri;
        final Uri bgmUri = bgm.uri;
        worker.submit(() -> {
            if (titleBgm.prepareFromSaf(this, iniUri, bgmUri)) {
                runOnUiThread(() -> {
                    boolean stillEnabled = optionsOpen ? draftConfig.launcherMusic : savedConfig.launcherMusic;
                    if (!preparing && !isFinishing() && !isDestroyed() && stillEnabled) {
                        titleBgm.start();
                    }
                });
            }
        });
    }

    private void updateStatusBanner() {
        if (folder == null) return;
        if (folder.selectedPacksDirectly) {
            notice.show(getText(R.string.sd_selected_packs_directly));
        } else if (!folder.missing.isEmpty()) {
            notice.show(getString(R.string.sd_absent, String.join(", ", folder.missing)));
        } else if (gameFailed) {
            notice.show(getText(failureMessage()));
        } else if (folder.baseGpkLanguages.isEmpty() && folder.baseJsonLanguages.isEmpty()) {
            notice.show(getText(R.string.sd_no_translations));
        } else if (!folder.supportsSelection(savedLanguageSelection)) {
            notice.show(getText(R.string.sd_choose));
        } else {
            String loc = SchoolDaysFolder.selectionLocale(savedLanguageSelection);
            String backend = SchoolDaysFolder.isGpkSelection(savedLanguageSelection) ? "GPK" : "JSON";
            notice.show(getString(R.string.sd_ready, localeDisplayName(loc), backend,
                    lxhStateLabel(savedConfig, savedLanguageSelection)));
        }
    }

    private void refresh() {
        int token = ++generation;
        if (scan != null) scan.cancel(true);
        privateReady = false;
        folder = null;
        showArt(null);
        TextView languagesView = findViewById(R.id.languages);
        languagesView.setText(R.string.sd_no_translations);
        TextView filesSummary = findViewById(R.id.files_summary);
        filesSummary.setText("");

        Uri tree = source.selected();
        findViewById(R.id.pick_home).setVisibility(tree == null ? View.VISIBLE : View.GONE);
        findViewById(R.id.forget).setVisibility(tree != null ? View.VISIBLE : View.GONE);
        notice.show(getText(tree == null ? R.string.sd_missing : R.string.sd_checking));

        scan = worker.submit(() -> {
            if (tree == null) {
                boolean ready = SchoolDaysFolder.hasPrivateInstallation(getDataDir());
                runOnUiThread(() -> {
                    if (token != generation || !ready) return;
                    privateReady = true;
                    notice.show(getText(gameFailed ? R.string.sd_game_failed : R.string.sd_private_ready));
                });
                return;
            }
            try {
                if (!source.canRead(tree)) throw new SecurityException();
                SchoolDaysFolder found = new SchoolDaysFolder(this, source, tree);
                runOnUiThread(() -> {
                    if (token != generation) return;
                    folder = found;
                    if (!found.supportsSelection(savedLanguageSelection)) {
                        String fallback = found.defaultSelection();
                        if (fallback != null) {
                            savedLanguageSelection = fallback;
                            draftLanguageSelection = fallback;
                            getPreferences(MODE_PRIVATE).edit()
                                    .putString("languageSelection", savedLanguageSelection)
                                    .putString("locale", SchoolDaysFolder.selectionLocale(savedLanguageSelection))
                                    .apply();
                        }
                    }

                    String ok = getString(R.string.sd_status_ok);
                    String miss = getString(R.string.sd_status_missing);
                    String opt = getString(R.string.sd_status_optional);
                    filesSummary.setText(getString(
                            R.string.sd_files_summary,
                            found.retailPackCount,
                            found.hasLxhPack ? ok : opt,
                            found.missing.contains("game-assets/school-days-hq.ktnroute") ? miss : ok,
                            found.hasStylesAss ? ok : opt,
                            found.hasForcedSubtitles ? ok : opt,
                            found.hasOverlay ? ok : opt));

                    String gpkText = found.baseGpkLanguages.isEmpty()
                            ? getString(R.string.sd_none) : String.join(", ", found.baseGpkLanguages);
                    String jsonText = found.baseJsonLanguages.isEmpty()
                            ? getString(R.string.sd_none) : String.join(", ", found.baseJsonLanguages);
                    languagesView.setText(
                            getString(R.string.sd_detected_gpk, gpkText) + "\n"
                                    + getString(R.string.sd_detected_json, jsonText));

                    syncUiFromDraft();
                    updateStatusBanner();
                    rebuildLanguageChoices();
                    showArt(found);
                    updateLauncherMusicState();
                });
            } catch (IOException | RuntimeException e) {
                runOnUiThread(() -> {
                    if (token == generation) {
                        notice.show(getText(e instanceof SecurityException
                                ? R.string.sd_denied : R.string.sd_unreadable));
                    }
                });
            }
        });
    }

    private Bitmap loadPackagedBannerArtwork() {
        try (InputStream stream = getAssets().open("assets/SchoolDaysHQ-Launcher.png")) {
            return BitmapFactory.decodeStream(stream);
        } catch (IOException | RuntimeException ignored) {
            return null;
        }
    }

    private void showArt(SchoolDaysFolder f) {
        SchoolDaysBannerView banner = findViewById(R.id.art);
        Bitmap bmp = (f != null && f.artwork != null) ? f.artwork : packagedBannerArtwork;
        banner.setArtwork(bmp);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PLAY) {
            SchoolDaysFolder.discard(session);
            session = null;
            gameFailed = result != RESULT_OK;
            failedStage = data == null ? 0 : data.getIntExtra(GameActivity.STARTUP_STAGE, 0);
            if (gameFailed) {
                openOptions(failedStage == 4 ? TAB_LANGUAGE : TAB_FILES);
            }
        }
        if (request == PICK_FOLDER && result == RESULT_OK && data != null) {
            try {
                source.remember(data);
                titleBgm.release();
                gameFailed = false;
            } catch (IOException | RuntimeException e) {
                Toast.makeText(this, R.string.sd_denied, Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (optionsOpen) cancelOptions();
        else super.onBackPressed();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean("options", optionsOpen);
        state.putInt("activeTab", activeTab);
        if (session != null) state.putString("session", session.getName());
        state.putBoolean("gameFailed", gameFailed);
        state.putInt("failedStage", failedStage);
    }

    @Override
    protected void onDestroy() {
        ++generation;
        titleBgm.release();
        worker.shutdownNow();
        super.onDestroy();
    }
}
