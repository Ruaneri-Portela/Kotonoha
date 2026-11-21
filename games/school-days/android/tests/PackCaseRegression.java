package org.kotonoha.android;

import java.util.Locale;
import java.util.Map;

public final class PackCaseRegression {
    private static final DocumentTree.Entry FILE = new DocumentTree.Entry(null, false, false);
    private static final DocumentTree.Entry EMPTY = new DocumentTree.Entry(null, false, true);
    private static final DocumentTree.Entry DIRECTORY = new DocumentTree.Entry(null, true, false);

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
        System.out.println("PASS 8 synthetic pack metadata cases");
    }
}
