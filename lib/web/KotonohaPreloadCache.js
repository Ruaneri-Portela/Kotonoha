// Kotonoha HTTP asset cache. Never touches /save or local-folder assets.
// preload.json is a dump of successfully fetched ranges, not manifest contents.
(function() {
    "use strict";
    const DB_NAME = "KotonohaAssetCache";
    const DB_VERSION = 2;
    const MAX_PRELOAD_JSON = 2 * 1024 * 1024;
    const CHUNK_SIZE = 1024 * 1024;
    const pending = new Set();
    const observed = new Map();
    let baseURL = null;
    let scope = null;
    let manifestHash = null;
    let dbPromise = null;

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
        baseURL = new URL(url).href;
        manifestHash = manifest ? fingerprint(manifest) : null;
        // Without versioned metadata, do not reuse potentially stale
        // anonymous HTTP assets across separate browser sessions.
        scope = baseURL + "#" + (manifestHash ||
            "session-" + Date.now() + "-" + Math.random().toString(36).slice(2));
        observed.clear();
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
                    if (!db.objectStoreNames.contains("records")) {
                        const store = db.createObjectStore("records", { keyPath: "key" });
                        store.createIndex("byScope", "scope");
                    }
                    if (!db.objectStoreNames.contains("documents"))
                        db.createObjectStore("documents", {keyPath: "key"});
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
    function storeRange(path, start, data) {
        if (!scope || !validPath(path)) return Promise.reject(
            new Error("Asset cache is not configured"));
        const length = data.byteLength;
        if (!Number.isSafeInteger(start) || start < 0 ||
            !length || length > 8 * CHUNK_SIZE ||
            start + length > Number.MAX_SAFE_INTEGER)
            return Promise.reject(new Error("Invalid cache range"));
        const keyPath = canonical(path);
        const key = JSON.stringify([scope, keyPath, start, length]);
        const bytes = data instanceof Uint8Array ? data :
            new Uint8Array(data);
        return openDB().then(db => new Promise((resolve, reject) => {
            const tx = db.transaction(["chunks", "records"], "readwrite");
            tx.objectStore("chunks").put({
                key, scope, path: keyPath, start, length, bytes
            });
            tx.objectStore("records").put({
                key, scope, path, start, length
            });
            tx.oncomplete = () => resolve();
            tx.onabort = () => reject(tx.error || new Error("Cache write aborted"));
            tx.onerror = () => reject(tx.error || new Error("Cache write failed"));
        }));
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
        }
        return output;
    }
    function remember(path, start, length) {
        if (!observed.has(path)) observed.set(path, []);
        observed.get(path).push([start, length]);
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
    // Fetch transport for FetchAssetProvider's queued HEAD and byte-range
    // jobs. The same IndexedDB cache serves demand reads and boot preloads.
    async function fetchJob(job) {
        if (!scope) throw new Error("HTTP asset cache is not configured");
        const {path, start, length, full, discovery} = job;
        const recordPath = validPath(job.remotePath) ? job.remotePath : path;
        if (!validPath(path)) throw new Error("Invalid fetch path");
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
        const declared = Number(response.headers.get("content-length") || 0);
        if (declared > 8 * CHUNK_SIZE)
            throw new Error("HTTP response exceeds the 8 MiB memory bound");
        // The response could omit Content-Length. Abort the reader after
        // crossing the upper bound rather than buffering an entire archive.
        const reader = response.body?.getReader();
        let chunks = [], consumed = 0;
        if (reader) {
            try {
                while (true) {
                    const next = await reader.read();
                    if (next.done) break;
                    consumed += next.value.byteLength;
                    if (consumed > 8 * CHUNK_SIZE)
                        throw new Error("HTTP response exceeded 8 MiB");
                    chunks.push(next.value);
                }
            } catch (error) {
                await reader.cancel().catch(() => {});
                throw error;
            } finally {
                reader.releaseLock();
            }
        } else {
            const fallback = new Uint8Array(await response.arrayBuffer());
            if (fallback.byteLength > 8 * CHUNK_SIZE)
                throw new Error("HTTP response exceeded 8 MiB");
            chunks = [fallback]; consumed = fallback.byteLength;
        }
        const bytes = new Uint8Array(consumed);
        let at = 0;
        for (const part of chunks) {
            bytes.set(part, at);
            at += part.byteLength;
        }
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
        if (isValidRange || fullSmall) {
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
    async function buildPreload() {
        if (!baseURL) throw new Error("Choose HTTP(S) before exporting preload");
        return {
            schemaVersion: 1,
            generatedAt: new Date().toISOString(),
            manifestHash,
            entries: await listDownloaded()
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
    async function fetchRequiredRange(path, start, length) {
        const url = fileURL(path);
        const response = await fetch(url, {
            headers: {Range: `bytes=${start}-${start + length - 1}`},
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
        const bytes = new Uint8Array(await response.arrayBuffer());
        if (bytes.byteLength !== length)
            throw new Error("Incomplete preload response for " + path);
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
                if (navigator.onLine === false) return false;
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
        const items = [];
        for (const entry of preload.entries) {
            if (!entry || !validPath(entry.path) ||
                !Array.isArray(entry.ranges))
                throw new Error("Invalid preload.json asset");
            for (const range of entry.ranges) {
                if (!Array.isArray(range) || range.length !== 2 ||
                    !range.every(Number.isSafeInteger) ||
                    range[0] < 0 || range[1] <= 0 ||
                    range[0] + range[1] > Number.MAX_SAFE_INTEGER)
                    throw new Error("Invalid preload.json byte range");
                for (let start = range[0], end = start + range[1];
                     start < end; start += CHUNK_SIZE) {
                    items.push([entry.path, start, Math.min(CHUNK_SIZE, end - start)]);
                    if (items.length > 100000)
                        throw new Error("preload.json contains too many chunks");
                }
            }
        }
        // Cache the guide itself so installed PWAs can verify required
        // chunks without fetching preload.json on a later offline launch.
        // All required chunks must be committed to IndexedDB before boot.
        await storeDocument("preload", scope, source);
        let done = 0;
        for (const [path, start, length] of items) {
            if (!await readRange(path, start, length))
                await fetchRequiredRange(path, start, length);
            ++done;
            if (progress) progress(done, items.length, path);
        }
        console.info("[Kotonoha] Initial preload ready:", done, "chunk(s)");
        return true;
    }
    window.KotonohaAssetCache = {
        configure, readRange, recordFetch, fetchJob,
        preloadIfPresent, buildPreload, dumpPreload,
        storeManifest, cachedManifest
    };
    window.KotonohaDumpPreload = dumpPreload;
})();
