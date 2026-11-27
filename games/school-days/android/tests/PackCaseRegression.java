package org.kotonoha.android;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.Map;

public final class PackCaseRegression {
    private static final DocumentTree.Entry FILE = new DocumentTree.Entry(null, false, 1);
    private static final DocumentTree.Entry EMPTY = new DocumentTree.Entry(null, false, 0);
    private static final DocumentTree.Entry DIRECTORY = new DocumentTree.Entry(null, true, -1);

    private static void check(String name, DocumentTree.Entry entry, boolean expected) {
        boolean found = !SchoolDaysFolder.missingPacks(Map.of(name, entry)).contains("System.GPK");
        if (found != expected) throw new AssertionError(name);
    }

    private static void checkEnableLxhIniReset() throws Exception {
        File tmp = Files.createTempDirectory("sd-config-store-test-").toFile();
        try {
            SchoolDaysConfigStore store = new SchoolDaysConfigStore(tmp);
            SchoolDaysConfigStore.Snapshot base = SchoolDaysConfigStore.Snapshot.defaults();

            // 1. Explicit EnableLxh=0 persists alongside other keys.
            SchoolDaysConfigStore.Snapshot off = base.copy();
            off.contentAspectPreference = 2;
            off.androidFrameLimit = 60;
            off.enableLxh = Boolean.FALSE;
            store.saveKotonohaIni(off);
            SchoolDaysConfigStore.Snapshot reloaded = SchoolDaysConfigStore.Snapshot.defaults();
            store.loadKotonohaIni(reloaded);
            if (!Boolean.FALSE.equals(reloaded.enableLxh)
                    || reloaded.contentAspectPreference != 2
                    || reloaded.androidFrameLimit != 60) {
                throw new AssertionError("Expected EnableLxh=0, aspect=2, fps=60 to persist");
            }

            // 2. Editing an unrelated setting preserves explicit EnableLxh=0.
            SchoolDaysConfigStore.Snapshot edited = reloaded.copy();
            edited.androidFrameLimit = 30;
            store.saveKotonohaIni(edited);
            reloaded = SchoolDaysConfigStore.Snapshot.defaults();
            store.loadKotonohaIni(reloaded);
            if (!Boolean.FALSE.equals(reloaded.enableLxh) || reloaded.androidFrameLimit != 30) {
                throw new AssertionError("Unrelated save must preserve explicit EnableLxh=0");
            }

            // 3. Reset defaults (enableLxh=null) + Apply must remove stale EnableLxh from Kotonoha.ini.
            SchoolDaysConfigStore.Snapshot reset = SchoolDaysConfigStore.Snapshot.defaults();
            store.saveKotonohaIni(reset);
            String iniText = new String(
                    Files.readAllBytes(store.kotonohaIniFile().toPath()), StandardCharsets.UTF_8);
            if (iniText.contains("EnableLxh=")) {
                throw new AssertionError("Reset defaults left stale EnableLxh key in Kotonoha.ini: " + iniText);
            }
            SchoolDaysConfigStore.Snapshot afterReset = SchoolDaysConfigStore.Snapshot.defaults();
            store.loadKotonohaIni(afterReset);
            if (afterReset.enableLxh != null) {
                throw new AssertionError("Expected enableLxh==null after reset defaults");
            }
        } finally {
            SchoolDaysFolder.discard(tmp);
        }
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.forLanguageTag("tr-TR"));
        check("System.gpk", FILE, true);
        check("System.GPK", FILE, true);
        check("sYsTeM.GpK", FILE, true);
        check("System.gpk", EMPTY, false);
        check("System.gpk", DIRECTORY, false);
        check("System.gpk.bak", FILE, false);
        if (!SchoolDaysFolder.missingPacks(Map.of()).contains("System.GPK")) {
            throw new AssertionError("An empty folder must report System.GPK as missing");
        }
        if (SchoolDaysFolder.missingPacks(Map.of("ini.gpk", FILE)).contains("Ini.GPK")) {
            throw new AssertionError("Pack matching must be independent of the device language");
        }
        if (!SchoolDaysFolder.missingPacks(Map.of("System.gpk", FILE, "SYSTEM.GPK", FILE))
                .contains("System.GPK")) throw new AssertionError("Ambiguous pack names");
        if (SchoolDaysFolder.missingPacks(Map.of()).contains("LxH.GPK")) {
            throw new AssertionError("Optional LxH.GPK must never be reported as a missing retail pack");
        }
        if (SchoolDaysFolder.missingPacks(Map.of()).size() != 29) {
            throw new AssertionError("Expected exactly 29 mandatory retail packs");
        }
        checkEnableLxhIniReset();
        System.out.println("PASS 14 synthetic pack & Kotonoha.ini EnableLxh cases");
    }
}
