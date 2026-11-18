#!/usr/bin/env node
// Runtime exercise of the actual Kotonoha JS cache with deterministic fake
// IndexedDB and HTTP responses. The Playwright test exercises real WASM too.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "..",
    "lib", "web", "KotonohaPreloadCache.js"), "utf8");

function compare(a, b) {
    if (!Array.isArray(a)) return a < b ? -1 : a > b ? 1 : 0;
    for (let i = 0; i < Math.min(a.length, b.length); i++) {
        const v = compare(a[i], b[i]);
        if (v) return v;
    }
    return a.length - b.length;
}
const idbRange = {
    only(value) { return {only: value}; },
    bound(low, high) { return {low, high}; }
};
const tables = new Map();
const indexes = new Map();
const storeNames = {
    contains(name) {return tables.has(name);}
};
function keysForIndex(name, indexName) {
    const items = [...tables.get(name).values()];
    const fields = indexes.get(name).get(indexName);
    return items.map(value => ({
        value,
        indexed: Array.isArray(fields) ? fields.map(f => value[f]) : value[fields]
    })).sort((a, b) => compare(a.indexed, b.indexed));
}
function readCursor(rows, range, direction) {
    let items = rows.filter(row => {
        if (range.only !== undefined)
            return compare(row.indexed, range.only) === 0;
        return compare(row.indexed, range.low) >= 0 &&
            compare(row.indexed, range.high) <= 0;
    });
    if (direction === "prev") items = items.reverse();
    let index = 0;
    const req = {};
    const fire = () => queueMicrotask(() => {
        const current = items[index];
        req.result = current ? {
            value: current.value,
            continue() {index++; fire();}
        } : null;
        req.onsuccess?.();
    });
    fire();
    return req;
}
const database = {
    objectStoreNames: storeNames,
    createObjectStore(name) {
        tables.set(name, new Map());
        indexes.set(name, new Map());
        return {
            createIndex(indexName, fields) {
                indexes.get(name).set(indexName, fields);
            }
        };
    },
    transaction(names) {
        const tx = {
            objectStore(name) {
                assert(tables.has(name));
                return {
                    put(value) {
                        tables.get(name).set(value.key, value);
                        queueMicrotask(() => tx.oncomplete?.());
                    },
                    get(key) {
                        const req = {};
                        queueMicrotask(() => {
                            req.result = tables.get(name).get(key);
                            req.onsuccess?.();
                        });
                        return req;
                    },
                    index(indexName) {
                        return {
                            openCursor(range, direction) {
                                return readCursor(keysForIndex(name, indexName),
                                    range, direction);
                            }
                        };
                    }
                };
            }
        };
        return tx;
    }
};
const indexedDB = {
    open() {
        const req = {result: database};
        queueMicrotask(() => {
            req.onupgradeneeded?.();
            req.onsuccess?.();
        });
        return req;
    }
};
let online = true;
let networkFetches = 0;
let guide = null;
let malformed = false;
let withhold = null;
const base = "https://example.invalid/RealLive/game-assets/";
const files = new Map([
    ["opening.bin", new TextEncoder().encode("Kotonoha opening!")],
    ["later.bin", new TextEncoder().encode("Next story scene")],
    ["oversized.bin", new Uint8Array(10 * 1024 * 1024)]
]);
async function fetchNetwork(url, options = {}) {
    if (!online) throw new TypeError("Offline");
    const relative = new URL(url).href.slice(base.length);
    if (relative === "preload.json") {
        if (!guide) return new Response("", {status: 404});
        return new Response(guide, {status: 200});
    }
    const sourceBytes = files.get(relative);
    if (!sourceBytes) return new Response("", {status: 404});
    networkFetches++;
    if (withhold) await withhold;
    const range = /^bytes=(\d+)-(\d+)$/.exec(options.headers?.Range || "");
    if (!range) {
        return new Response(sourceBytes, {status: 200,
            headers: {"Content-Length": sourceBytes.length,
                "Accept-Ranges": "bytes"}});
    }
    const from = Number(range[1]), to = Number(range[2]);
    if (malformed)
        return new Response(sourceBytes, {status: 200});
    const data = sourceBytes.slice(from, to + 1);
    return new Response(data, {
        status: 206,
        headers: {"Content-Length": data.length,
            "Content-Range": `bytes ${from}-${to}/${sourceBytes.length}`}
    });
}
const browserWindow = {indexedDB};
vm.runInNewContext(source, {
    window: browserWindow, indexedDB, IDBKeyRange: idbRange,
    URL, TextEncoder, Uint8Array, Blob, Response, console,
    navigator: {onLine: true}, fetch: fetchNetwork,
    setTimeout, Date, Math
}, {filename: "KotonohaPreloadCache.js"});
const manager = browserWindow.KotonohaAssetCache;

(async () => {
    const manifest = JSON.stringify({entries: [
        {path: "opening.bin", size: 17},
        {path: "later.bin", size: 16}
    ]});
    manager.configure(base, manifest);
    const file = files.get("opening.bin");
    let reply = await manager.fetchJob({
        url: base + "opening.bin", path: "opening.bin",
        remotePath: "opening.bin", fileSize: file.length,
        start: 0, length: file.length, full: false, discovery: false
    });
    assert.equal(reply.status, 206);
    assert.equal(Buffer.from(reply.bytes).toString(), "Kotonoha opening!");
    assert.equal(networkFetches, 1);
    reply = await manager.fetchJob({
        url: base + "opening.bin", path: "opening.bin",
        remotePath: "opening.bin", fileSize: file.length,
        start: 0, length: file.length, full: false, discovery: false
    });
    assert.equal(reply.status, 206);
    assert.equal(networkFetches, 1,
        "second read must be fully served from IndexedDB");

    const dump = await manager.buildPreload();
    assert.equal(dump.entries.length, 1);
    assert.equal(dump.entries[0].path, "opening.bin");
    assert.equal(dump.entries[0].ranges[0][1], file.length);
    guide = JSON.stringify({...dump, entries: [
        ...dump.entries, {path: "later.bin", ranges: [[0, 16]]}
    ]});
    let resolveHold;
    withhold = new Promise(resolve => {resolveHold = resolve;});
    let finished = false;
    const boot = manager.preloadIfPresent().then(() => {finished = true;});
    await new Promise(resolve => setTimeout(resolve, 0));
    assert(!finished, "engine boot cannot release before the required range");
    resolveHold();
    await boot;
    withhold = null;
    assert(finished);
    assert.equal(Buffer.from(await manager.readRange(
        "later.bin", 0, 16)).toString(), "Next story scene");
    assert.equal(networkFetches, 2,
        "preload should fetch only missing chunks");

    online = false;
    manager.configure(base, manifest);
    assert.equal(await manager.preloadIfPresent(), true,
        "offline boot must reuse the cached guide and all chunks");
    assert.equal(networkFetches, 2,
        "offline restore must not fetch any asset again");

    // If the cache has no guide/manifest and there is no network,
    // startup cannot falsely report successful mandatory preload.
    manager.configure("https://example.invalid/Other/game-assets/", manifest);
    await assert.rejects(manager.preloadIfPresent());

    online = true;
    manager.configure(base, manifest);
    malformed = true;
    await assert.rejects(manager.fetchJob({
        url: base + "oversized.bin", path: "oversized.bin",
        remotePath: "oversized.bin", fileSize: 10 * 1024 * 1024,
        start: 0, length: 64, full: false, discovery: false
    }), /exceeds|Range/);
    console.log("Kotonoha persistent IndexedDB/preload runtime tests passed.");
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
