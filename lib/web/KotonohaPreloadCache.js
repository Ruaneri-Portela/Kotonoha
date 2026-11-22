// Kotonoha HTTP asset cache. Never touches /save or local-folder assets.
// preload.json is a dump of successfully fetched ranges, not manifest contents.
(function() {
    "use strict";
    const DB_NAME = "KotonohaAssetCache";
    const DB_VERSION = 3;
    const MAX_PRELOAD_JSON = 2 * 1024 * 1024;
    const CHUNK_SIZE = 1024 * 1024;
    const pending = new Set();
    const observed = new Map();
    let baseURL = null;
    let scope = null;
    let manifestHash = null;
    let dbPromise = null;
    let indexedSizes = new Map();
    let indexedPaths = new Map();
    let activeNode = null;
    let observedNodes = new Map();
    let sceneGraph = new Map();
    let predictionController = null;
    const PREDICTION_BUDGET = 8 * CHUNK_SIZE;
    const DEFAULT_CACHE_LIMIT = 2 * 1024 * 1024 * 1024;
    const MIN_QUOTA_HEADROOM = 8 * CHUNK_SIZE;
    const protectedBootRanges = new Map();
    let cacheWriteTail = Promise.resolve();
    const cacheLimit = () => {
        const custom = Number(window.KotonohaCacheMaxBytes);
        return Number.isSafeInteger(custom) && custom > 0
            ? custom : DEFAULT_CACHE_LIMIT;
    };
    const isQuotaError = error =>
        error?.name === "QuotaExceededError" ||
        /quota|disk full/i.test(String(error?.message || ""));
    function serializeWrite(task) {
        const next = cacheWriteTail.then(task);
        cacheWriteTail = next.catch(() => {});
        return next;
    }
    function isBootProtected(row) {
        const ranges = protectedBootRanges.get(row.scope)?.get(row.path);
        return !!ranges && ranges.some(([start, length]) =>
            row.start < start + length &&
            row.start + row.length > start);
    }

    function validPath(path) {
        return typeof path === "string" && path.length > 0 &&
            path.length <= 4096 && !path.startsWith("/") &&
            !path.includes("\\") && !path.includes(":") &&
            !path.includes("\0") &&
            path.split("/").every(part => part && part !== "." && part !== "..");
    }
    function canonical(path) {
        return path.toLowerCase();
    }
    function fileURL(path) {
        if (!validPath(path)) throw new Error("Invalid asset path: " + path);
        return new URL(path.split("/").map(encodeURIComponent).join("/"), baseURL);
    }
    // Non-cryptographic change token; manifest metadata is versioned as a unit.
    // Future manifests should provide strong per-file content hashes.
    function fingerprint(value) {
        let a = 2166136261, b = 2246822519;
        for (let i = 0; i < value.length; ++i) {
            const c = value.charCodeAt(i);
            a = Math.imul(a ^ c, 16777619);
            b = Math.imul(b ^ c, 3266489917);
        }
        return (a >>> 0).toString(16).padStart(8, "0") +
            (b >>> 0).toString(16).padStart(8, "0");
    }
    function configure(url, manifest) {
        if (typeof manifest !== "string" || !manifest.length)
            throw new Error("HTTP(S) game assets require a non-empty manifest");
        const parsed = JSON.parse(manifest);
        if (!parsed || !Array.isArray(parsed.entries) || !parsed.entries.length)
            throw new Error("HTTP manifest must list at least one asset");
        const sizes = new Map();
        const paths = new Map();
        for (const entry of parsed.entries) {
            if (!entry || !validPath(entry.path) ||
                !Number.isSafeInteger(entry.size) || entry.size < 0 ||
                sizes.has(canonical(entry.path)))
                throw new Error("Invalid or duplicate HTTP manifest entry");
            sizes.set(canonical(entry.path), entry.size);
            paths.set(canonical(entry.path), entry.path);
        }
        baseURL = new URL(url).href;
        manifestHash = fingerprint(manifest);
        scope = baseURL + "#" + manifestHash;
        observed.clear();
        protectedBootRanges.clear();
        activeNode = null;
        observedNodes.clear();
        sceneGraph.clear();
        if (predictionController) predictionController.abort();
        predictionController = null;
        indexedSizes = sizes;
        indexedPaths = paths;
    }
    function openDB() {
        if (!dbPromise) {
            dbPromise = new Promise((resolve, reject) => {
                if (!window.indexedDB) {
                    reject(new Error("IndexedDB is not available"));
                    return;
                }
                const req = indexedDB.open(DB_NAME, DB_VERSION);
                req.onupgradeneeded = () => {
                    const db = req.result;
                    if (!db.objectStoreNames.contains("chunks")) {
                        const store = db.createObjectStore("chunks", { keyPath: "key" });
                        store.createIndex("byFileStart",
                            ["scope", "path", "start"]);
                    }
                    let records;
                    if (!db.objectStoreNames.contains("records")) {
                        records = db.createObjectStore("records", { keyPath: "key" });
                        records.createIndex("byScope", "scope");
                    } else {
                        records = req.transaction.objectStore("records");
                    }
                    if (!records.indexNames.contains("byLastAccess"))
                        records.createIndex("byLastAccess", ["lastAccess", "key"]);
                    if (!db.objectStoreNames.contains("documents"))
                        db.createObjectStore("documents", {keyPath: "key"});
                    if (!db.objectStoreNames.contains("cacheStats"))
                        db.createObjectStore("cacheStats", {keyPath: "key"});
                    // Migrate old records once. Only tiny metadata rows are
                    // enumerated; the binary chunks are never loaded here.
                    let total = 0;
                    const cursorRequest = records.openCursor();
                    cursorRequest.onsuccess = () => {
                        const cursor = cursorRequest.result;
                        if (cursor) {
                            const row = cursor.value;
                            total += Number(row.length) || 0;
                            if (!Number.isFinite(row.lastAccess)) {
                                row.lastAccess = 0;
                                cursor.update(row);
                            }
                            cursor.continue();
                        } else {
                            req.transaction.objectStore("cacheStats").put({
                                key: "total", bytes: total
                            });
                        }
                    };
                };
                req.onsuccess = () => resolve(req.result);
                req.onerror = () => reject(req.error ||
                    new Error("Unable to open IndexedDB"));
                req.onblocked = () => reject(new Error("IndexedDB upgrade blocked"));
            }).catch(error => {
                dbPromise = null;
                throw error;
            });
        }
        return dbPromise;
    }
    async function storeDocument(kind, url, text) {
        const db = await openDB();
        return new Promise((resolve, reject) => {
            const tx = db.transaction("documents", "readwrite");
            tx.objectStore("documents").put({
                key: kind + ":" + url,
                text,
                storedAt: Date.now()
            });
            tx.oncomplete = () => resolve();
            tx.onabort = () => reject(tx.error ||
                new Error("Metadata write aborted"));
            tx.onerror = () => reject(tx.error ||
                new Error("Metadata write failed"));
        });
    }
    async function loadDocument(kind, url) {
        try {
            const db = await openDB();
            return await new Promise((resolve, reject) => {
                const req = db.transaction("documents", "readonly")
                    .objectStore("documents").get(kind + ":" + url);
                req.onsuccess = () => resolve(req.result?.text || null);
                req.onerror = () => reject(req.error);
            });
        } catch (error) {
            console.warn("[Kotonoha] Cached metadata unavailable:", error);
            return null;
        }
    }
    // Indexed file listing remains available to the VFS when offline.
    async function storeManifest(url, text) {
        await storeDocument("manifest", url, text);
    }
    function cachedManifest(url) {
        return loadDocument("manifest", url);
    }
    function totalCacheBytes() {
        return openDB().then(db => new Promise((resolve, reject) => {
            const tx = db.transaction("cacheStats", "readonly");
            const req = tx.objectStore("cacheStats").get("total");
            req.onsuccess = () => resolve(Math.max(0, req.result?.bytes || 0));
            req.onerror = () => reject(req.error);
        }));
    }
    async function effectiveLimit() {
        let limit = cacheLimit();
        // navigator.storage.estimate is approximate, so leave headroom.
        // Only the Kotonoha chunks can be reclaimed; never touch saves.
        const estimate = await navigator.storage?.estimate?.().catch(() => null);
        if (estimate && Number.isFinite(estimate.quota) &&
            Number.isFinite(estimate.usage)) {
            const current = await totalCacheBytes();
            const available = Math.max(0, estimate.quota - estimate.usage -
                Math.min(MIN_QUOTA_HEADROOM, estimate.quota * 0.05));
            limit = Math.min(limit, Math.max(0, current + available));
        }
        return limit;
    }
    // Oldest-first transaction. Deleting the chunk and its metadata in the
    // same transaction avoids orphaned entries and broken accounting.
    async function evictLRU(bytesNeeded, exceptKey = null) {
        if (bytesNeeded <= 0) return 0;
        const db = await openDB();
        return new Promise((resolve, reject) => {
            const tx = db.transaction(["chunks", "records", "cacheStats"], "readwrite");
            const records = tx.objectStore("records");
            const chunks = tx.objectStore("chunks");
            const stats = tx.objectStore("cacheStats");
            let freed = 0;
            let baseline = 0;
            let failure = null;
            stats.get("total").onsuccess = event => {
                baseline = Math.max(0, event.target.result?.bytes || 0);
                const cursorReq = records.index("byLastAccess").openCursor();
                cursorReq.onsuccess = () => {
                    const cursor = cursorReq.result;
                    if (!cursor || freed >= bytesNeeded) {
                        stats.put({key: "total",
                            bytes: Math.max(0, baseline - freed)});
                        return;
                    }
                    const row = cursor.value;
                    if (row.key !== exceptKey && !isBootProtected(row)) {
                        chunks.delete(row.key);
                        records.delete(row.key);
                        freed += row.length || 0;
                    }
                    cursor.continue();
                };
            };
            tx.oncomplete = () => resolve(freed);
            tx.onabort = () => reject(failure || tx.error ||
                new Error("LRU eviction failed"));
            tx.onerror = () => {};
        });
    }
    function commitRange(path, start, bytes, limit) {
        const dbReady = openDB();
        return dbReady.then(db => new Promise((resolve, reject) => {
            const length = bytes.byteLength;
            const keyPath = canonical(path);
            const key = JSON.stringify([scope, keyPath, start, length]);
            const tx = db.transaction(["chunks", "records", "cacheStats"],
                "readwrite");
            const records = tx.objectStore("records");
            const chunks = tx.objectStore("chunks");
            const stats = tx.objectStore("cacheStats");
            let failure = null;
            const fail = reason => {
                failure = new Error(reason);
                tx.abort();
            };
            stats.get("total").onsuccess = event => {
                const baseline = Math.max(0, event.target.result?.bytes || 0);
                records.get(key).onsuccess = old => {
                    const previous = old.target.result?.length || 0;
                    const needed = Math.max(0, baseline - previous + length - limit);
                    let freed = 0;
                    const finish = () => {
                        if (baseline - previous + length - freed > limit) {
                            fail("Not enough space for mandatory cache data");
                            return;
                        }
                        const lastAccess = Date.now();
                        chunks.put({key, scope, path: keyPath, start, length, bytes});
                        records.put({key, scope, path, start, length, lastAccess});
                        stats.put({key: "total",
                            bytes: Math.max(0, baseline - previous + length - freed)});
                    };
                    if (!needed) {
                        finish();
                        return;
                    }
                    const cursorReq = records.index("byLastAccess").openCursor();
                    cursorReq.onsuccess = () => {
                        const cursor = cursorReq.result;
                        if (!cursor || freed >= needed) {
                            finish();
                            return;
                        }
                        const row = cursor.value;
                        if (row.key !== key && !isBootProtected(row)) {
                            chunks.delete(row.key);
                            records.delete(row.key);
                            freed += row.length || 0;
                        }
                        cursor.continue();
                    };
                };
            };
            tx.oncomplete = () => resolve();
            tx.onabort = () => reject(failure || tx.error ||
                new Error("Cache write aborted"));
            tx.onerror = () => {};
        }));
    }
    function storeRange(path, start, data) {
        if (!scope || !validPath(path)) return Promise.reject(
            new Error("Asset cache is not configured"));
        const length = data.byteLength;
        if (!Number.isSafeInteger(start) || start < 0 ||
            !length || length > 8 * CHUNK_SIZE ||
            start + length > Number.MAX_SAFE_INTEGER)
            return Promise.reject(new Error("Invalid cache range"));
        const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
        // Serialize writes and cleanups within this tab. IndexedDB readwrite
        // transactions serialize updates from other tabs.
        return serializeWrite(async () => {
            const key = JSON.stringify([scope, canonical(path), start, length]);
            const limit = await effectiveLimit();
            if (length > limit) throw new Error("Asset exceeds cache limit");
            for (let attempt = 0; attempt < 3; ++attempt) {
                try {
                    await commitRange(path, start, bytes, limit);
                    return;
                } catch (error) {
                    if (!isQuotaError(error) || attempt === 2) throw error;
                    // Quota may be lower than storage.estimate(). Reclaim
                    // extra old chunks and retry the atomic write.
                    const freed = await evictLRU(
                        Math.max(length * (attempt + 1), MIN_QUOTA_HEADROOM), key);
                    if (!freed) throw error;
                }
            }
        });
    }
    function touchChunk(key) {
        return openDB().then(db => new Promise((resolve, reject) => {
            const tx = db.transaction("records", "readwrite");
            const records = tx.objectStore("records");
            records.get(key).onsuccess = event => {
                const row = event.target.result;
                if (row) {
                    row.lastAccess = Date.now();
                    records.put(row);
                }
            };
            tx.oncomplete = () => resolve();
            tx.onabort = () => reject(tx.error);
            tx.onerror = () => {};
        }));
    }
    async function getCacheStats() {
        return {usageBytes: await totalCacheBytes(),
            maxBytes: cacheLimit()};
    }
    // Return the most recent slice covering the position. Older larger
    // slices may still cover it if a newer short probe overlaps their start.
    async function coveringRange(path, position) {
        const db = await openDB();
        return new Promise((resolve, reject) => {
            const tx = db.transaction("chunks", "readonly");
            const index = tx.objectStore("chunks").index("byFileStart");
            const limit = IDBKeyRange.bound(
                [scope, canonical(path), 0], [scope, canonical(path), position]);
            const request = index.openCursor(limit, "prev");
            request.onsuccess = () => {
                const cursor = request.result;
                if (!cursor) return resolve(null);
                const row = cursor.value;
                if (row.start + row.length > position)
                    return resolve(row);
                cursor.continue();
            };
            request.onerror = () => reject(request.error ||
                new Error("Cache read failed"));
        });
    }
    async function readRange(path, start, length) {
        if (!scope || !validPath(path) ||
            !Number.isSafeInteger(start) || start < 0 ||
            !Number.isSafeInteger(length) || length <= 0 ||
            length > CHUNK_SIZE || start + length > Number.MAX_SAFE_INTEGER)
            return null;
        const output = new Uint8Array(length);
        let written = 0;
        while (written < length) {
            const position = start + written;
            const row = await coveringRange(path, position);
            if (!row) return null;
            const bytes = row.bytes instanceof Blob ?
                new Uint8Array(await row.bytes.arrayBuffer()) :
                new Uint8Array(row.bytes);
            const within = position - row.start;
            const count = Math.min(length - written, row.length - within);
            if (count <= 0 || within + count > bytes.length) return null;
            output.set(bytes.subarray(within, within + count), written);
            written += count;
            // Reads refresh the recency of the accessed chunk.
            await touchChunk(row.key).catch(() => {});
        }
        return output;
    }
    function remember(path, start, length) {
        if (!observed.has(path)) observed.set(path, []);
        observed.get(path).push([start, length]);
        if (activeNode) {
            const node = observedNodes.get(activeNode);
            if (node) {
                if (!node.assets.has(path)) node.assets.set(path, []);
                node.assets.get(path).push([start, length]);
            }
        }
    }
    function recordFetch(path, offset, bytes) {
        if (!scope || !validPath(path) || !bytes || !bytes.byteLength)
            return;
        // Called only after C++ validates a completed HTTP response.
        const snapshot = bytes.slice();
        remember(path, offset, snapshot.byteLength);
        const task = storeRange(path, offset, snapshot)
            .catch(error => console.warn(
                "[Kotonoha] Cannot persist downloaded range:", error));
        pending.add(task);
        task.finally(() => pending.delete(task));
    }
    // Read a bounded response without accepting a server that ignores Range
    // and unexpectedly sends a multi-gigabyte archive. Content-Length alone
    // is insufficient: enforce the limit while consuming the stream.
    async function readBoundedResponse(response, limit) {
        const declared = Number(response.headers.get("content-length") || 0);
        if (declared > limit)
            throw new Error("HTTP response exceeds allowed range size");
        if (!response.body || typeof response.body.getReader !== "function") {
            if (declared <= 0)
                throw new Error("Cannot safely stream an unbounded HTTP response");
            const bytes = new Uint8Array(await response.arrayBuffer());
            if (bytes.byteLength > limit)
                throw new Error("HTTP response exceeded range size");
            return bytes;
        }
        const reader = response.body.getReader();
        const chunks = [];
        let consumed = 0;
        try {
            while (true) {
                const next = await reader.read();
                if (next.done) break;
                consumed += next.value.byteLength;
                if (consumed > limit)
                    throw new Error("HTTP response exceeded range size");
                chunks.push(next.value);
            }
        } catch (error) {
            await reader.cancel().catch(() => {});
            throw error;
        } finally {
            reader.releaseLock();
        }
        const bytes = new Uint8Array(consumed);
        let at = 0;
        for (const chunk of chunks) {
            bytes.set(chunk, at);
            at += chunk.byteLength;
        }
        return bytes;
    }

    // Fetch transport for FetchAssetProvider's queued HEAD and byte-range
    // jobs. The same IndexedDB cache serves demand reads and boot preloads.
    async function fetchJob(job) {
        if (!scope) throw new Error("HTTP asset cache is not configured");
        const {path, start, length, full, discovery} = job;
        const recordPath = validPath(job.remotePath) ? job.remotePath : path;
        const requestScope = scope;
        if (!validPath(path) || !indexedSizes.has(canonical(path)))
            throw new Error("Asset not listed in manifest: " + path);
        if (discovery)
            throw new Error("Manifest-only HTTP source cannot discover assets");
        if (!Number.isSafeInteger(job.fileSize) ||
            job.fileSize !== indexedSizes.get(canonical(path)))
            throw new Error("HTTP asset size differs from manifest");
        if (!full && (!Number.isSafeInteger(start) || start < 0 ||
            !Number.isSafeInteger(length) || length <= 0 ||
            start + length > Number.MAX_SAFE_INTEGER))
            throw new Error("Invalid fetch byte range");

        // Discovery must obtain authoritative remote metadata. Cache entries
        // cannot prove that an unknown file exists or determine its size.
        if (!discovery && !full && length <= CHUNK_SIZE) {
            try {
                const cached = await readRange(path, start, length);
                if (cached) {
                    // Synthesize exactly the Range response the C++ validator
                    // expects. The indexed manifest provides the total size.
                    const total = job.fileSize;
                    if (!Number.isSafeInteger(total) ||
                        start + length > total)
                        throw new Error("Missing indexed file size");
                    return {
                        status: 206,
                        bytes: cached,
                        headers: "Content-Range: bytes " + start + "-" +
                            (start + length - 1) + "/" + total + "\r\n"
                    };
                }
            } catch (error) {
                console.warn("[Kotonoha] IndexedDB cache read failed:", error);
            }
        }
        const headers = {};
        if (!full) headers.Range = `bytes=${start}-${start + length - 1}`;
        // No HTTP conditional caching here: persistent byte caching is
        // versioned by the manifest, and responses are Range-validated.
        const response = await fetch(job.url, {
            method: discovery && full ? "HEAD" : "GET",
            headers, cache: "no-store"
        });
        const responseHeaders = ["Content-Length", "Accept-Ranges",
            "Content-Range", "ETag"].map(name => {
                const value = response.headers.get(name);
                return value == null ? "" : name + ": " + value + "\r\n";
            }).join("");
        if (discovery && full)
            return {status: response.status, bytes: new Uint8Array(0),
                headers: responseHeaders};
        // Avoid buffering huge error bodies into the WASM bridge.
        if (response.status !== 200 && response.status !== 206)
            return {status: response.status, bytes: new Uint8Array(0),
                headers: responseHeaders};
        const bytes = await readBoundedResponse(response, 8 * CHUNK_SIZE);
        // Persist only validated ranges. C++ performs the definitive
        // Content-Range and byte-length validation before accepting data.
        const expected = "bytes " + start + "-" +
            (start + length - 1) + "/" + job.fileSize;
        const isValidRange = !full && !discovery &&
            response.status === 206 &&
            response.headers.get("Content-Range") === expected &&
            bytes.length === length;
        const fullSmall = !discovery && response.status === 200 &&
            bytes.length <= 8 * CHUNK_SIZE &&
            (full || (start === 0 && bytes.length === job.fileSize));
        if ((isValidRange || fullSmall) && requestScope === scope) {
            try {
                // Retain only real, verified bytes. Split larger payloads
                // into blocks, avoiding large IndexedDB records.
                const origin = fullSmall ? 0 : start;
                for (let part = 0; part < bytes.length; part += CHUNK_SIZE) {
                    const slice = bytes.slice(part, part + CHUNK_SIZE);
                    await storeRange(recordPath, origin + part, slice);
                    remember(recordPath, origin + part, slice.byteLength);
                }
            } catch (error) {
                // A disabled/full IndexedDB must not break online gameplay.
                console.warn("[Kotonoha] Cannot persist HTTP bytes:", error);
            }
        }
        return {status: response.status, bytes,
            headers: responseHeaders};
    }

    async function listDownloaded() {
        await Promise.all([...pending]);
        const ranges = new Map();
        const collect = (path, offset, length) => {
            if (!validPath(path) || !Number.isSafeInteger(offset) ||
                !Number.isSafeInteger(length) || length <= 0) return;
            const key = canonical(path);
            if (!ranges.has(key)) ranges.set(key, {path, parts: []});
            ranges.get(key).parts.push([offset, length]);
        };
        try {
            const db = await openDB();
            await new Promise((resolve, reject) => {
                const tx = db.transaction("records", "readonly");
                const index = tx.objectStore("records").index("byScope");
                const request = index.openCursor(IDBKeyRange.only(scope));
                request.onsuccess = () => {
                    const cursor = request.result;
                    if (!cursor) return resolve();
                    collect(cursor.value.path, cursor.value.start,
                        cursor.value.length);
                    cursor.continue();
                };
                request.onerror = () => reject(request.error);
            });
        } catch (error) {
            console.warn("[Kotonoha] Cache metadata unavailable:", error);
        }
        for (const [path, parts] of observed) {
            for (const [start, length] of parts) collect(path, start, length);
        }
        return [...ranges].sort(([a], [b]) => a.localeCompare(b))
            .map(([, entry]) => {
                const {path, parts} = entry;
                parts.sort((a, b) => a[0] - b[0]);
                const merged = [];
                for (const [start, length] of parts) {
                    const last = merged[merged.length - 1];
                    if (last && start <= last[0] + last[1])
                        last[1] = Math.max(last[0] + last[1],
                            start + length) - last[0];
                    else merged.push([start, length]);
                }
                return {path, ranges: merged};
            });
    }
    function dumpedNodes() {
        const result = Object.create(null);
        for (const [id, node] of observedNodes) {
            result[id] = {
                assets: [...node.assets].map(([path, ranges]) =>
                    ({path, ranges})),
                next: [...node.next]
            };
        }
        return result;
    }
    async function buildPreload() {
        if (!baseURL) throw new Error("Choose HTTP(S) before exporting preload");
        return {
            schemaVersion: 1,
            generatedAt: new Date().toISOString(),
            manifestHash,
            entries: await listDownloaded(),
            nodes: dumpedNodes()
        };
    }
    async function dumpPreload() {
        const preload = await buildPreload();
        const blob = new Blob([JSON.stringify(preload, null, 2) + "\n"],
            {type: "application/json"});
        const objectURL = URL.createObjectURL(blob);
        const link = document.createElement("a");
        link.href = objectURL;
        link.download = "preload.json";
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(() => URL.revokeObjectURL(objectURL), 1000);
        console.info("[Kotonoha] Exported preload.json with",
            preload.entries.length, "downloaded asset(s)");
        return preload;
    }
    async function fetchRequiredRange(path, start, length, signal) {
        const indexedSize = indexedSizes.get(canonical(path));
        if (indexedSize === undefined || start < 0 || length <= 0 ||
            start + length > indexedSize)
            throw new Error("Preload asset/range not listed in manifest: " + path);
        const requestScope = scope;
        const url = fileURL(indexedPaths.get(canonical(path)));
        const response = await fetch(url, {
            headers: {Range: `bytes=${start}-${start + length - 1}`},
            signal,
            cache: "no-store"
        });
        if (response.status === 206) {
            const received = response.headers.get("Content-Range") || "";
            const match = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(received);
            if (!match || Number(match[1]) !== start ||
                Number(match[2]) !== start + length - 1 ||
                Number(match[3]) < start + length)
                throw new Error("Incorrect HTTP Content-Range for " + path);
        } else if (!(response.status === 200 &&
                     start === 0 && length <= 8 * CHUNK_SIZE)) {
            throw new Error("HTTP Range required for " + path +
                " (HTTP " + response.status + ")");
        }
        const bytes = await readBoundedResponse(response, length);
        if (bytes.byteLength !== length)
            throw new Error("Incomplete preload response for " + path);
        if (requestScope !== scope)
            throw new Error("Asset source changed during preload");
        await storeRange(path, start, bytes);
        remember(path, start, length);
    }
    async function preloadIfPresent(progress) {
        if (!scope) throw new Error("Asset cache is not configured");
        const url = new URL("preload.json", baseURL);
        let source;
        try {
            const response = await fetch(url, {cache: "no-store"});
            if (response.status === 404) return false;
            if (!response.ok) throw new Error(
                "Cannot fetch preload.json (HTTP " + response.status + ")");
            const declared = Number(response.headers.get("content-length") || 0);
            if (declared > MAX_PRELOAD_JSON)
                throw new Error("preload.json is too large");
            source = await response.text();
        } catch (error) {
            // When offline, reuse the last validated boot guide for the
            // exact version of the indexed manifest.
            source = await loadDocument("preload", scope);
            if (!source) {
                // A cold offline install cannot establish whether a required
                // guide exists. Do not bypass the mandatory boot barrier.
                throw error;
            }
            console.info("[Kotonoha] Using cached preload.json offline");
        }
        if (new TextEncoder().encode(source).byteLength > MAX_PRELOAD_JSON)
            throw new Error("preload.json is too large");
        const preload = JSON.parse(source);
        if (!preload || preload.schemaVersion !== 1 ||
            !Array.isArray(preload.entries) || preload.entries.length > 10000)
            throw new Error("Invalid preload.json structure");
        if (preload.manifestHash !== manifestHash)
            throw new Error("preload.json was generated for a different manifest");
        // Optional story graph produced by the runtime recorder (or edited
        // by game authors) guides speculative loading after initial boot.
        const graph = new Map();
        if (preload.nodes !== undefined) {
            if (!preload.nodes || typeof preload.nodes !== "object" ||
                Array.isArray(preload.nodes) ||
                Object.keys(preload.nodes).length > 10000)
                throw new Error("Invalid preload.json nodes");
            for (const [id, value] of Object.entries(preload.nodes)) {
                if (!id || id.length > 512 || !value ||
                    !Array.isArray(value.assets) ||
                    !Array.isArray(value.next) ||
                    value.assets.length > 1000 || value.next.length > 100)
                    throw new Error("Invalid predictive scene node: " + id);
                graph.set(id, value);
            }
        }
        const items = [];
        for (const entry of preload.entries) {
            if (!entry || !validPath(entry.path) ||
                !Array.isArray(entry.ranges) ||
                !indexedSizes.has(canonical(entry.path)))
                throw new Error("Preload asset not listed in manifest");
            for (const range of entry.ranges) {
                if (!Array.isArray(range) || range.length !== 2 ||
                    !range.every(Number.isSafeInteger) ||
                    range[0] < 0 || range[1] <= 0 ||
                    range[0] + range[1] > indexedSizes.get(canonical(entry.path)))
                    throw new Error("Invalid preload.json byte range");
                for (let start = range[0], end = start + range[1];
                     start < end; start += CHUNK_SIZE) {
                    items.push([entry.path, start, Math.min(CHUNK_SIZE, end - start)]);
                    if (items.length > 100000)
                        throw new Error("preload.json contains too many chunks");
                }
            }
        }
        // Pin the complete boot set: LRU cannot evict earlier required
        // chunks while later ones are being fetched. If the boot set does
        // not fit the available quota, fail visibly instead of booting
        // with incomplete assets.
        const pinned = new Map();
        for (const [path, start, length] of items) {
            const key = canonical(path);
            if (!pinned.has(key)) pinned.set(key, []);
            pinned.get(key).push([start, length]);
        }
        protectedBootRanges.set(scope, pinned);
        const mandatoryBytes = items.reduce((sum, [, ,length]) => sum + length, 0);
        if (mandatoryBytes > cacheLimit())
            throw new Error("Mandatory preload exceeds cache limit");
        await storeDocument("preload", scope, source);
        let done = 0;
        for (const [path, start, length] of items) {
            if (!await readRange(path, start, length))
                await fetchRequiredRange(path, start, length);
            ++done;
            if (progress) progress(done, items.length, path);
        }
        sceneGraph = graph;
        console.info("[Kotonoha] Initial preload ready:", done, "chunk(s)");
        return true;
    }

    function ensureNode(id) {
        if (!observedNodes.has(id) && observedNodes.size < 10000)
            observedNodes.set(id, {assets: new Map(), next: new Set()});
        return observedNodes.get(id);
    }

    async function sceneEntered(nodeId) {
        const id = String(nodeId || "");
        if (!scope || !id || id.length > 512) return;
        if (activeNode && activeNode !== id) {
            const previous = ensureNode(activeNode);
            if (previous) previous.next.add(id);
        }
        activeNode = id;
        ensureNode(id);
        // Cancel obsolete speculative transfers on story transitions.
        if (predictionController) predictionController.abort();
        predictionController = new AbortController();
        const signal = predictionController.signal;
        const node = sceneGraph.get(id);
        if (!node) return;
        const neighbors = node.next.map(next =>
            typeof next === "string" ? {id: next, weight: 1} : next)
            .filter(next => next && typeof next.id === "string" &&
                Number.isFinite(next.weight ?? 1))
            .sort((a, b) => (b.weight ?? 1) - (a.weight ?? 1))
            .slice(0, 3);
        const targets = [id, ...neighbors.map(next => next.id)];
        const work = [];
        let remaining = PREDICTION_BUDGET;
        for (const targetId of targets) {
            const target = sceneGraph.get(targetId);
            if (!target) continue;
            for (const asset of target.assets) {
                const path = typeof asset === "string" ? asset : asset?.path;
                if (!validPath(path) || remaining <= 0) continue;
                const fileSize = indexedSizes.get(canonical(path));
                if (fileSize === undefined) continue;
                const ranges = Array.isArray(asset.ranges) ? asset.ranges :
                    (Number.isSafeInteger(fileSize) && fileSize > 0
                        ? [[0, fileSize]] : []);
                for (const range of ranges) {
                    if (!Array.isArray(range) || range.length !== 2 ||
                        !range.every(Number.isSafeInteger) ||
                        range[0] < 0 || range[1] <= 0 ||
                        range[0] + range[1] > Number.MAX_SAFE_INTEGER)
                        continue;
                    for (let start = range[0], end = Math.min(range[0] + range[1], fileSize);
                         start < end && remaining > 0; start += CHUNK_SIZE) {
                        const length = Math.min(end - start, CHUNK_SIZE, remaining);
                        work.push([path, start, length]);
                        remaining -= length;
                    }
                    if (remaining <= 0) break;
                }
            }
        }
        let cursor = 0;
        const worker = async () => {
            while (!signal.aborted && cursor < work.length) {
                const [path, start, length] = work[cursor++];
                try {
                    if (!await readRange(path, start, length))
                        await fetchRequiredRange(path, start, length, signal);
                } catch (error) {
                    if (!signal.aborted)
                        console.warn("[Kotonoha] Predictive prefetch failed:",
                            path, error);
                }
            }
        };
        await Promise.all([worker(), worker()]);
    }

    window.KotonohaSceneEntered = sceneEntered;
    window.KotonohaAssetCache = {
        configure, readRange, recordFetch, fetchJob,
        preloadIfPresent, buildPreload, dumpPreload,
        sceneEntered, storeManifest, cachedManifest, getCacheStats
    };
    window.KotonohaDumpPreload = dumpPreload;
})();
