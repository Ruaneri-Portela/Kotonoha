// One origin-wide CacheStorage entry for the immutable, versioned
// libkotonoha-<build>.wasm. Per-game PWA service workers cannot intercept
// ../libs/ (outside their scope), so only this exact shared binary is
// intercepted here. HTTP Range and all game-assets/ requests remain untouched.
(function () {
    "use strict";
    const url = window.KotonohaSharedWasmURL;
    if (!url || typeof window.fetch !== "function") return;
    const nativeFetch = window.fetch.bind(window);
    const cacheName = "kotonoha:shared-wasm:v1";

    window.fetch = function (input, init) {
        let request;
        try {
            request = new Request(input, init);
        } catch (_) {
            return nativeFetch(input, init);
        }
        if (request.method !== "GET" || request.headers.has("range") ||
            request.url !== url) {
            return nativeFetch(input, init);
        }

        return (async () => {
            let cache = null;
            try {
                if (typeof window.caches !== "undefined") {
                    cache = await window.caches.open(cacheName);
                    // The filename includes the engine revision. Reuse across
                    // all games without re-downloading on every page load.
                    const previous = await cache.match(request);
                    if (previous) return previous;
                }
            } catch (error) {
                console.warn("[Kotonoha] Shared WASM cache unavailable:", error);
                cache = null;
            }

            const response = await nativeFetch(input, init);
            if (cache && response.ok && response.status === 200) {
                try {
                    // Store before returning, so an initialized offline PWA
                    // has the entire engine; quota errors remain nonfatal.
                    await cache.put(request, response.clone());
                } catch (error) {
                    console.warn("[Kotonoha] Could not cache shared engine:", error);
                }
            }
            return response;
        })();
    };
})();
