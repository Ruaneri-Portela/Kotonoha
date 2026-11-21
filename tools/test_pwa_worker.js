#!/usr/bin/env node
// Execute the actual PWA Service Worker template using isolated mock scopes.
// No external npm packages or WASM build are needed.
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");

const source = fs.readFileSync(path.join(__dirname, "..",
    "buildsystems", "cmake", "packaging", "service-worker.js.in"), "utf8");
const bucket = new Map();
let online = true;
const networkCalls = [];

function getURL(request) {
    return typeof request === "string" ? request : request.url;
}
function newResponse(url) {
    return {
        ok: true, status: 200, type: "basic",
        url, clone() { return newResponse(url); }
    };
}
const cacheAPI = {
    async open(name) {
        if (!bucket.has(name)) bucket.set(name, new Map());
        const store = bucket.get(name);
        return {
            async put(request, reply) { store.set(getURL(request), reply); },
            async match(request) { return store.get(getURL(request)); }
        };
    },
    async keys() { return [...bucket.keys()]; },
    async delete(name) { return bucket.delete(name); }
};
async function fetchNetwork(request) {
    const url = getURL(request);
    networkCalls.push(url);
    if (!online) throw new TypeError("Network unavailable");
    return newResponse(url);
}
function launchWorker(game) {
    const prefix = "https://example.invalid/" + game + "/";
    const handlers = new Map();
    const self = {
        registration: {scope: prefix},
        addEventListener(type, callback) { handlers.set(type, callback); },
        async skipWaiting() {},
        clients: {async claim() {}}
    };
    const rendered = source
        .replaceAll("@PWA_CACHE_KEY@", "version1")
        .replaceAll("@TARGET_NAME@", game);
    vm.runInNewContext(rendered, {
        self, caches: cacheAPI, fetch: fetchNetwork, console,
        URL, Request, Response
    }, {filename: game + "/service-worker.js"});

    async function dispatch(type, data = {}) {
        const awaits = [];
        let reply = null;
        const event = {
            ...data,
            waitUntil(promise) { awaits.push(Promise.resolve(promise)); },
            respondWith(promise) { reply = Promise.resolve(promise); }
        };
        handlers.get(type)(event);
        await Promise.all(awaits);
        return reply === null ? null : await reply;
    }
    return {prefix, dispatch};
}
(async () => {
    const a = launchWorker("RealLive");
    const b = launchWorker("Kirikiri");
    await a.dispatch("install");
    await a.dispatch("activate");
    await b.dispatch("install");
    await b.dispatch("activate");

    // Cache namespaces must not evict another installed game's data.
    const names = await cacheAPI.keys();
    assert.equal(names.length, 2);
    assert(names.some(name => name.includes("/RealLive/")));
    assert(names.some(name => name.includes("/Kirikiri/")));

    const get = (url, extras = {}) => ({
        url, method: "GET", mode: "same-origin",
        headers: new Headers(), ...extras
    });
    const shellA = await a.dispatch("fetch",
        {request: get(a.prefix + "index.html")});
    assert(shellA && shellA.ok);

    await a.dispatch("message",
        {data: {type: "KOTONOHA_CACHE_RUNTIME"}});
    const keys = await cacheAPI.keys();
    const realCache = await cacheAPI.open(
        keys.find(name => name.includes("/RealLive/")));
    assert(await realCache.match(a.prefix + "RealLive.wasm"));
    assert(await realCache.match(a.prefix + "RealLive.js"));

    online = false;
    const before = networkCalls.length;
    const offline = await a.dispatch("fetch", {
        request: get(a.prefix + "index.html", {mode: "navigate"})
    });
    assert(offline && offline.ok, "offline installed shell must be reusable");
    assert.equal(networkCalls.length, before + 1,
        "offline shell uses cache only after network fails");

    // No asset request, Range or HEAD may be answered by the PWA worker:
    // they must remain available to the IndexedDB HTTP transport.
    assert.equal(await a.dispatch("fetch",
        {request: get(a.prefix + "game-assets/archive.xp3")}), null);
    assert.equal(await a.dispatch("fetch",
        {request: get(a.prefix + "RealLive.wasm",
            {headers: new Headers({Range: "bytes=0-63"})})}), null);
    assert.equal(await a.dispatch("fetch",
        {request: {...get(a.prefix + "game-assets/archive.xp3"),
            method: "HEAD"}}), null);
    assert.equal(await a.dispatch("fetch",
        {request: get(b.prefix + "index.html")}), null,
        "PWA worker must not intercept another game's scope");
    assert.equal(networkCalls.length, before + 1,
        "ignored requests should not be handled by the worker");

    // Versioned updates may delete only caches for their own game.
    const c = launchWorker("RealLive");
    await c.dispatch("install");
    await c.dispatch("activate");
    assert((await cacheAPI.keys()).some(name => name.includes("/Kirikiri/")),
        "a PWA update must preserve the other game's cache");
    console.log("Kotonoha per-game PWA worker runtime tests passed.");
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
