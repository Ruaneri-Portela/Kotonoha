#!/usr/bin/env node
// Verify a single versioned libKotonoha.wasm cache is shared by independent
// game pages, without intercepting asset Range requests or other resources.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const js = fs.readFileSync(path.join(__dirname, "..", "lib/web",
    "KotonohaSharedWasmCache.js"), "utf8");
const sharedUrl = "https://example.invalid/libs/libkotonoha-v1.wasm";
const stored = new Map();
const cacheAPI = {
    async open(name) {
        assert.equal(name, "kotonoha:shared-wasm:v1");
        return {
            async put(request, response) {
                stored.set(request.url, response.clone());
            },
            async match(request) {
                return stored.get(request.url)?.clone() || undefined;
            }
        };
    }
};
let online = true;
const calls = [];
async function networkFetch(input, init) {
    const request = new Request(input, init);
    calls.push(request.url);
    if (!online) throw new Error("offline");
    return new Response("wasm-payload", {status: 200});
}
function startGame(game) {
    const window = {
        KotonohaSharedWasmURL: sharedUrl,
        fetch: networkFetch,
        caches: cacheAPI
    };
    vm.runInNewContext(js, {window, Request, Response, console},
        {filename: game + "-shared-cache.js"});
    return window.fetch;
}

(async () => {
    const a = startGame("RealLive");
    const b = startGame("Kirikiri");
    assert.equal(await (await a(sharedUrl)).text(), "wasm-payload");
    assert.equal(stored.size, 1);
    assert.equal(await (await b(sharedUrl)).text(), "wasm-payload");
    assert.equal(calls.length, 1, "second game must reuse the same engine");

    online = false;
    assert.equal(await (await a(sharedUrl)).text(), "wasm-payload",
        "shared engine is available offline");
    assert.equal(calls.length, 1);
    await assert.rejects(() => a("https://example.invalid/RealLive/game-assets/a.arc"),
        /offline/, "game assets never pass through the engine cache");
    await assert.rejects(() => a(sharedUrl, {
        headers: {Range: "bytes=0-63"}
    }), /offline/, "HTTP Range is never cached as a full engine response");
    assert.equal(calls.length, 3);

    console.log("Kotonoha origin-wide shared WASM cache tests passed.");
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
