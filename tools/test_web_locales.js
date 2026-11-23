#!/usr/bin/env node
// Test the real inline browser UI translator without a WebAssembly build.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const html = fs.readFileSync(path.join(__dirname, "..",
    "buildsystems", "cmake", "packaging", "index.html.in"), "utf8");
const marker = "// All browser startup UI text is localized";
const anchor = html.indexOf(marker);
assert(anchor >= 0, "startup language initializer must be present");
const start = html.lastIndexOf("<script>", anchor);
const end = html.indexOf("</script>", anchor);
assert(start >= 0 && end > anchor, "could not extract the locale script");
const script = html.slice(start + "<script>".length, end);
new vm.Script(script, {filename: "index.html.in:localization"});

const values = new Map();
const storage = {
    getItem(key) {return values.get(key) || null;},
    setItem(key, value) {values.set(key, value);}
};
const staticKeys = [...html.matchAll(/data-i18n="([^"]+)"/g)]
    .map(match => match[1]);
const titleKeys = [...html.matchAll(/data-i18n-title="([^"]+)"/g)]
    .map(match => match[1]);
const ariaKeys = [...html.matchAll(/data-i18n-aria-label="([^"]+)"/g)]
    .map(match => match[1]);
function createUI(language) {
    const elements = new Map();
    function node(id, dataset = {}) {
        const el = {
            id, dataset, textContent: "", title: "", attrs: {},
            setAttribute(key, value) {this.attrs[key] = value;},
            addEventListener(name, callback) {
                this.handlers ??= new Map();
                this.handlers.set(name, callback);
            },
            click() {this.handlers?.get("click")?.();}
        };
        elements.set(id, el);
        return el;
    }
    const status = node("loading-status", {i18n: "chooseSource"});
    const en = node("kotonoha-lang-en");
    const pt = node("kotonoha-lang-pt");
    const local = node("select-local-assets", {i18n: "localFolder"});
    const remote = node("use-server-assets", {i18n: "useHttp"});
    const title = node("add-browser-folder", {i18nTitle: "mountFolder"});
    const aria = node("browser-folder-tools", {i18nAriaLabel: "gameFolders"});
    const docs = new Map([
        ["[data-i18n]", [status, local, remote]],
        ["[data-i18n-title]", [title]],
        ["[data-i18n-aria-label]", [aria]]
    ]);
    const document = {
        documentElement: {lang: ""},
        querySelectorAll(query) {return docs.get(query) || [];},
        getElementById(id) {return elements.get(id);}
    };
    const window = {};
    vm.runInNewContext(script, {
        window, document, navigator: {language}, localStorage: storage,
        String, console
    }, {filename: "index.html.in:localization"});
    return {window, document, status, en, pt, local, remote, title, aria};
}
const first = createUI("en-US");
assert.equal(first.document.documentElement.lang, "en");
assert.equal(first.local.textContent, "Use local folder");
assert.equal(first.remote.textContent, "Use HTTP(S)");
first.window.KotonohaStatus("preloadProgress",
    {done: 1, total: 3, path: "menu.dat"});
assert.equal(first.status.textContent,
    "Required preload: 1/3 chunks — menu.dat");
first.pt.click();
assert.equal(first.document.documentElement.lang, "pt-BR");
assert.equal(first.local.textContent, "Usar pasta local");
assert.equal(first.status.textContent,
    "Preload obrigatório: 1/3 blocos — menu.dat");
assert.equal(first.title.title, "Montar uma pasta local no filesystem");
assert.equal(first.aria.attrs["aria-label"], "Pastas do jogo");
assert.equal(first.pt.attrs["aria-pressed"], "true");
assert.equal(values.get("kotonoha-language"), "pt");
const reloaded = createUI("en-US");
assert.equal(reloaded.document.documentElement.lang, "pt-BR",
    "stored preference must override browser's default language");
reloaded.en.click();
assert.equal(reloaded.document.documentElement.lang, "en");
assert.equal(reloaded.aria.attrs["aria-label"], "Game folders");
assert.equal(values.get("kotonoha-language"), "en");
for (const key of [...staticKeys, ...titleKeys, ...ariaKeys]) {
    assert.notEqual(reloaded.window.KotonohaText(key), key,
        "missing translation for HTML key " + key);
    reloaded.pt.click();
    assert.notEqual(reloaded.window.KotonohaText(key), key,
        "missing Portuguese translation for HTML key " + key);
    reloaded.en.click();
}
console.log("Kotonoha EN/PT browser startup localization tests passed.");
