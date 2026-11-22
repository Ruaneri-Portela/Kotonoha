package org.kotonoha.android;

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

    public static void main(String[] args) {
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
        System.out.println("PASS 11 synthetic pack metadata cases");
    }
}
