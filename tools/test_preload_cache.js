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
// Minimal asynchronous IndexedDB transaction model: upgrade migrations,
// cursor iteration, atomic delete/put, and completion ordering are exercised.
let installedVersion = 0;
let quotaCap = Infinity;
function copyTables() {
    return new Map([...tables].map(([name, rows]) =>
        [name, new Map(rows)]));
}
function copyIndexes() {
    return new Map([...indexes].map(([name, rows]) =>
        [name, new Map(rows)]));
}
const storeNames = {contains(name) {return tables.has(name);}};
function keysForIndex(name, indexName) {
    const fields = indexes.get(name).get(indexName);
    return [...tables.get(name).values()].map(value => ({
        value,
        indexed: Array.isArray(fields) ? fields.map(f => value[f]) : value[fields]
    })).filter(x => x.indexed !== undefined &&
        (!Array.isArray(x.indexed) || x.indexed.every(v => v !== undefined)))
      .sort((a, b) => compare(a.indexed, b.indexed));
}
function createTransaction(names, mode = "readonly") {
    const snapshot = mode === "readonly" ? null : copyTables();
    let pending = 0, completed = false, aborted = false;
    const tx = {
        error: null,
        abort() {
            if (completed || aborted) return;
            aborted = true;
            if (snapshot) {
                tables.clear();
                for (const [name, rows] of snapshot) tables.set(name, rows);
            }
            queueMicrotask(() => tx.onabort?.());
        },
        maybeFinish() {
            if (pending || completed || aborted) return;
            queueMicrotask(() => {
                if (!pending && !completed && !aborted) {
                    completed = true;
                    tx.oncomplete?.();
                }
            });
        },
        enqueue(fn) {
            pending++;
            const req = {result: null, error: null};
            queueMicrotask(() => {
                if (aborted) return;
                try {
                    fn(req);
                    req.onsuccess?.({target: req});
                } catch (error) {
                    req.error = error;
                    tx.error = error;
                    tx.abort();
                } finally {
                    pending--;
                    tx.maybeFinish();
                }
            });
            return req;
        },
        objectStore(name) {
            if (!tables.has(name))
                throw new Error("Missing IndexedDB object store: " + name);
            const store = {
                indexNames: {
                    contains(indexName) {
                        return indexes.get(name).has(indexName);
                    }
                },
                createIndex(indexName, fields) {
                    indexes.get(name).set(indexName, fields);
                },
                put(value) {
                    return tx.enqueue(() => {
                        if (name === "chunks") {
                            const rows = tables.get(name);
                            const existing = rows.get(value.key);
                            const total = [...rows.values()].reduce(
                                (sum, row) => sum + row.length, 0);
                            if (total - (existing?.length || 0) +
                                value.length > quotaCap)
                                throw Object.assign(new Error("Quota exceeded"),
                                    {name: "QuotaExceededError"});
                        }
                        tables.get(name).set(value.key, value);
                    });
                },
                delete(key) {
                    return tx.enqueue(() => tables.get(name).delete(key));
                },
                get(key) {
                    return tx.enqueue(req => {
                        req.result = tables.get(name).get(key);
                    });
                },
                openCursor(range, direction) {
                    return cursorRequest([...tables.get(name).values()]
                        .map(value => ({value, indexed: value.key}))
                        .sort((a, b) => compare(a.indexed, b.indexed)),
                        range, direction, name);
                },
                index(indexName) {
                    return {
                        openCursor(range, direction) {
                            return cursorRequest(
                                keysForIndex(name, indexName),
                                range, direction, name);
                        }
                    };
                }
            };
            return store;
        }
    };
    function cursorRequest(rows, range, direction, name) {
        const filtered = range ? rows.filter(row => {
            if (range.only !== undefined)
                return compare(row.indexed, range.only) === 0;
            return compare(row.indexed, range.low) >= 0 &&
                compare(row.indexed, range.high) <= 0;
        }) : rows;
        if (direction === "prev") filtered.reverse();
        const req = {};
        let position = 0;
        pending++;
        function fire() {
            queueMicrotask(() => {
                if (aborted) return;
                const row = filtered[position];
                if (row) {
                    req.result = {
                        value: row.value,
                        key: row.value.key,
                        update(value) {
                            return tx.enqueue(() =>
                                tables.get(name).set(value.key, value));
                        },
                        continue() {position++; fire();}
                    };
                    req.onsuccess?.({target: req});
                } else {
                    req.result = null;
                    req.onsuccess?.({target: req});
                    pending--;
                    tx.maybeFinish();
                }
            });
        }
        fire();
        return req;
    }
    tx.maybeFinish();
    return tx;
}
const database = {
    objectStoreNames: storeNames,
    createObjectStore(name) {
        tables.set(name, new Map());
        indexes.set(name, new Map());
        return createTransaction([name], "versionchange").objectStore(name);
    },
    transaction(names, mode) {
        return createTransaction(names, mode);
    }
};
const indexedDB = {
    open(name, version = 1) {
        const req = {result: database, transaction: null};
        queueMicrotask(() => {
            if (installedVersion < version) {
                const tx = createTransaction([], "versionchange");
                req.transaction = tx;
                req.onupgradeneeded?.();
                tx.oncomplete = () => {
                    installedVersion = version;
                    req.onsuccess?.();
                };
                tx.maybeFinish();
            } else {
                req.onsuccess?.();
            }
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
    ["future.bin", new TextEncoder().encode("Future scene asset")],
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
    URL, TextEncoder, Uint8Array, Blob, Response, AbortController, console,
    navigator: {onLine: true}, fetch: fetchNetwork,
    setTimeout, Date, Math
}, {filename: "KotonohaPreloadCache.js"});
const manager = browserWindow.KotonohaAssetCache;

(async () => {
    const manifest = JSON.stringify({entries: [
        {path: "opening.bin", size: 17},
        {path: "later.bin", size: 16},
        {path: "future.bin", size: 18},
        {path: "oversized.bin", size: 10 * 1024 * 1024},
        {path: "fresh.bin", size: new TextEncoder().encode("Fresh cache item of 24b!").length},
        {path: "quota.bin", size: new TextEncoder().encode("Quota fallback payload...").length}
    ]});
    assert.throws(() => manager.configure(base, '{"entries":[]}'),
        /manifest/i, "empty HTTP index must be rejected");
    manager.configure(base, manifest);
    const unknownFetches = networkFetches;
    await assert.rejects(manager.fetchJob({
        url: base + "missing.bin", path: "missing.bin",
        fileSize: 1, start: 0, length: 1, full: false
    }), /not listed in manifest/);
    assert.equal(networkFetches, unknownFetches,
        "unindexed file must not initiate HTTP GET");
    await manager.sceneEntered("node-A");
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
    assert(dump.nodes["node-A"].assets.some(x => x.path === "opening.bin"),
        "console dump should attribute fetched ranges to the active node");
    assert.equal(dump.entries[0].ranges[0][1], file.length);
    guide = JSON.stringify({...dump, entries: [
        ...dump.entries, {path: "later.bin", ranges: [[0, 16]]}
    ], nodes: {
        "node-A": {assets: [{path: "opening.bin"}],
            next: [{id: "node-B", weight: 0.8}]},
        "node-B": {assets: [{path: "future.bin"}], next: []}
    }});
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

    // Starting node-A should predict node-B's assets, but not block gameplay.
    await manager.sceneEntered("node-A");
    assert.equal(Buffer.from(await manager.readRange(
        "future.bin", 0, 18)).toString(), "Future scene asset");
    assert.equal(networkFetches, 3,
        "predictive scheduler must fetch neighboring node assets");
    await manager.sceneEntered("node-B");
    const graphDump = await manager.buildPreload();
    assert(graphDump.nodes["node-A"].next.includes("node-B"),
        "console dump should record observed story-node transitions");

    online = false;
    manager.configure(base, manifest);
    assert.equal(await manager.preloadIfPresent(), true,
        "offline boot must reuse the cached guide and all chunks");
    assert.equal(networkFetches, 3,
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
    }), /exceed|Range/i);
    malformed = false;

    // LRU: the most recently accessed chunk survives while old blocks
    // are removed to satisfy a configurable, cross-game byte limit.
    manager.configure(base, manifest); // Releases boot pins from old session.
    browserWindow.KotonohaCacheMaxBytes = 51;
    await manager.readRange("later.bin", 0, 16);
    await new Promise(resolve => setTimeout(resolve, 12));
    const freshData = new TextEncoder().encode("Fresh cache item of 24b!");
    files.set("fresh.bin", freshData);
    await manager.fetchJob({
        url: base + "fresh.bin", path: "fresh.bin",
        remotePath: "fresh.bin", fileSize: freshData.length,
        start: 0, length: freshData.length, full: false, discovery: false
    });
    let stats = await manager.getCacheStats();
    assert(stats.usageBytes <= 51, "cache cannot exceed its configured limit");
    assert.equal(await manager.readRange("opening.bin", 0, 17), null,
        "oldest chunk should have been evicted");
    assert.equal(Buffer.from(await manager.readRange("later.bin", 0, 16))
        .toString(), "Next story scene",
        "recently read chunk should survive oldest-first eviction");

    // Browser quota can be lower than the configured limit or estimate.
    // A quota rejection must evict more old chunks and retry the write.
    browserWindow.KotonohaCacheMaxBytes = 256;
    quotaCap = 48;
    const quotaData = new TextEncoder().encode("Quota fallback payload...");
    files.set("quota.bin", quotaData);
    await manager.fetchJob({
        url: base + "quota.bin", path: "quota.bin",
        remotePath: "quota.bin", fileSize: quotaData.length,
        start: 0, length: quotaData.length, full: false, discovery: false
    });
    stats = await manager.getCacheStats();
    assert(stats.usageBytes <= 48, "quota retry must free oldest chunks");
    assert.equal(Buffer.from(await manager.readRange(
        "quota.bin", 0, quotaData.length)).toString(),
        "Quota fallback payload...");

    // The mandatory guide must pin all boot chunks while filling them.
    quotaCap = Infinity;
    browserWindow.KotonohaCacheMaxBytes = 35;
    manager.configure(base, manifest);
    assert.equal(await manager.preloadIfPresent(), true);
    assert.equal(Buffer.from(await manager.readRange(
        "opening.bin", 0, 17)).toString(), "Kotonoha opening!");
    assert.equal(Buffer.from(await manager.readRange(
        "later.bin", 0, 16)).toString(), "Next story scene");
    stats = await manager.getCacheStats();
    assert(stats.usageBytes <= 35);
    browserWindow.KotonohaCacheMaxBytes = 20;
    await assert.rejects(manager.preloadIfPresent(),
        /Mandatory preload exceeds cache limit/,
        "boot must fail if the required assets cannot fit in cache");

    console.log("Kotonoha persistent IndexedDB/preload/LRU runtime tests passed.");
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
