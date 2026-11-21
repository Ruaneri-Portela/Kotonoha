#!/usr/bin/env node
"use strict";
// The inline Fetch/IndexedDB probe is stringized by the C++ preprocessor.
// In particular, formatting the JS '===' operator as '==\n=' makes the
// generated JavaScript invalid at the Acorn/JSDCE stage of Emscripten 7.
const fs = require("node:fs");
const path = require("node:path");
const assert = require("node:assert/strict");
const vm = require("node:vm");
const {spawnSync} = require("node:child_process");

const source = fs.readFileSync(path.join(__dirname, "..", "lib", "src",
    "Filesystem", "Sources", "FetchAssetProvider.cpp"), "utf8");
const block = source.match(
    /if\s*\(\s*EM_ASM_INT\s*\(\s*(\{[\s\S]*?\})\s*\)\s*\)/);
assert(block, "Missing Fetch/IndexedDB EM_ASM_INT availability probe");
assert(block[1].includes("typeof window.KotonohaAssetCache.fetchJob"),
    "Unexpected EM_ASM_INT probe");

function preprocess(inline) {
    const cpp = "#define EM_ASM_INT(...) #__VA_ARGS__\n" +
        "const char* js = EM_ASM_INT(" + inline + ");\n";
    const output = spawnSync("c++", ["-E", "-P", "-x", "c++", "-"], {
        input: cpp, encoding: "utf8", maxBuffer: 1024 * 1024
    });
    assert.equal(output.status, 0, output.stderr);
    const literal = output.stdout.match(
        /const char\s*\*\s*js\s*=\s*("(?:\\.|[^"\\])*")\s*;/);
    assert(literal, "C preprocessor did not stringize inline JavaScript");
    return JSON.parse(literal[1]);
}

const generated = preprocess(block[1]);
const expression = new vm.Script("(() => " + generated + ")()");
assert.equal(expression.runInNewContext({
    window: {KotonohaAssetCache: {fetchJob() {}}}
}), 1, "valid browser cache must be detected");
assert.equal(expression.runInNewContext({
    window: {KotonohaAssetCache: {fetchJob: 123}}
}), 0, "non-function cache must be rejected");
assert.equal(expression.runInNewContext({window: {}}), 0,
    "missing browser cache must fall back to emscripten_fetch");
assert(!generated.includes("== ="), "Unexpected split equality token");

// Negative control: the exact spacing from the original failing source.
const broken = block[1].replace(/==\s*"function"/,
    '==\n    = "function"');
assert.notEqual(broken, block[1], "The probe must use C++-safe JS '=='");
const invalid = preprocess(broken);
assert.match(invalid, /==\s+=/);
assert.throws(() => new vm.Script("(() => " + invalid + ")()"),
    SyntaxError, "the original Emscripten regression must be detected");
// Second regression: the shared native Runtime scene-transition callback
// also lives in EM_ASM, and must survive C++ macro stringization.
const runtime = fs.readFileSync(path.join(__dirname, "..", "lib",
    "src", "core", "Runtime.cpp"), "utf8");
const scene = runtime.match(
    /EM_ASM\s*\(\s*(\{[\s\S]*?KotonohaSceneEntered[\s\S]*?\})\s*,\s*node\.c_str\(\)\s*\)/);
assert(scene, "Missing native Runtime scene notification EM_ASM bridge");
const sceneJs = preprocess(scene[1]);
assert(!sceneJs.includes("== ="),
    "Scene notifications must not contain macro-split equality");
const notify = new vm.Script("(function($0) " + sceneJs + ")");
let received = null;
const callback = notify.runInNewContext({
    window: {KotonohaSceneEntered(node) { received = node; }},
    UTF8ToString: pointer => "node:" + String(pointer)
});
callback(12);
assert.equal(received, "node:12",
    "Native scene notifications must reach the browser asset cache");
const missingCallback = notify.runInNewContext({
    window: {}, UTF8ToString: pointer => String(pointer)
});
assert.doesNotThrow(() => missingCallback(12),
    "Scene callback remains optional for older web shells");
console.log("Kotonoha EM_ASM Fetch/IndexedDB and scene notification regressions passed");
