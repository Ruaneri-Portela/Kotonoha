// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Idiomatic Swift facade over the KotonohaLauncherCore C ABI. This file is
// compiled into the Xcode KotonohaLauncherCore.framework; Apple apps import
// the framework (`import KotonohaLauncherCore`) and use the types below.
import Foundation
import KotonohaLauncherCoreCABI

/// Swift mirror of `KotonohaGameLaunchRequest` (LauncherBridge.h).
public struct LaunchRequest: Sendable {
    public var gamePath: String
    public var engineSlug: String
    public var saveProfileId: String
    public var splitSavesByGame: Bool
    public var customSaveRoot: String
    public var assetOverlayPath: String
    public var translationPath: String
    public var subtitleStylePath: String
    public var customDocumentsDir: String
    public var ktrfPath: String
    public var scriptsDir: String
    public var startScenario: String
    public var rendererBackend: String
    public var upscalerBackend: String
    public var upscalerPreset: String
    public var upscalerFactor: String

    public init(
        gamePath: String,
        engineSlug: String = "",
        saveProfileId: String = "",
        splitSavesByGame: Bool = false,
        customSaveRoot: String = "",
        assetOverlayPath: String = "",
        translationPath: String = "",
        subtitleStylePath: String = "",
        customDocumentsDir: String = "",
        ktrfPath: String = "",
        scriptsDir: String = "",
        startScenario: String = "",
        rendererBackend: String = "",
        upscalerBackend: String = "",
        upscalerPreset: String = "",
        upscalerFactor: String = ""
    ) {
        self.gamePath = gamePath
        self.engineSlug = engineSlug
        self.saveProfileId = saveProfileId
        self.splitSavesByGame = splitSavesByGame
        self.customSaveRoot = customSaveRoot
        self.assetOverlayPath = assetOverlayPath
        self.translationPath = translationPath
        self.subtitleStylePath = subtitleStylePath
        self.customDocumentsDir = customDocumentsDir
        self.ktrfPath = ktrfPath
        self.scriptsDir = scriptsDir
        self.startScenario = startScenario
        self.rendererBackend = rendererBackend
        self.upscalerBackend = upscalerBackend
        self.upscalerPreset = upscalerPreset
        self.upscalerFactor = upscalerFactor
    }
}

/// Minimal decoded form of a discovered game entry returned by the C ABI.
public struct DiscoveredGame: Codable, Sendable {
    public let title: String
    public let saveProfileId: String
    public let path: String
    public let engineSlug: String
    public let engineDisplayName: String
    public let saveDirectory: String
    public let isDiscImage: Bool
    public let linkedInProcess: Bool
}

public struct UpscalePreset: Codable, Sendable {
    public let index: Int
    public let name: String
}

/// Thread-safe facade; every heap string from C is released after conversion.
public enum KotonohaLauncher {
    private static let decoder = JSONDecoder()

    private static func take(_ pointer: UnsafeMutablePointer<CChar>?) -> String {
        guard let pointer else { return "" }
        defer { KotonohaLauncherFreeString(pointer) }
        return String(cString: pointer)
    }

    private static func decode<T: Decodable>(
        _ type: T.Type, from json: String, fallback: T
    ) -> T {
        guard let data = json.data(using: .utf8),
              let value = try? decoder.decode(T.self, from: data) else {
            return fallback
        }
        return value
    }

    /// Scan library roots and return decoded game entries.
    public static func scanRoots(
        _ roots: [String],
        splitSavesByGame: Bool = false,
        customSaveRoot: String = ""
    ) -> [DiscoveredGame] {
        let owned = roots.map { strdup($0)! }
        defer { owned.forEach { free($0) } }
        let pointers = UnsafeMutableBufferPointer<UnsafePointer<CChar>?>.allocate(
            capacity: max(owned.count, 1))
        defer { pointers.deallocate() }
        for (index, value) in owned.enumerated() {
            pointers[index] = UnsafePointer(value)
        }
        let json = take(KotonohaLauncherScanRootsJson(
            pointers.baseAddress, Int32(roots.count),
            splitSavesByGame ? 1 : 0, customSaveRoot))
        return decode([DiscoveredGame].self, from: json, fallback: [])
    }

    /// Box usado pelo callback nativo (executado em threads de varredura).
    private final class ScanBox {
        let lock = NSLock()
        let handler: ((DiscoveredGame) -> Void)?
        var entries: [DiscoveredGame] = []
        init(_ handler: ((DiscoveredGame) -> Void)?) { self.handler = handler }
        func add(_ entry: DiscoveredGame) {
            lock.lock()
            entries.append(entry)
            lock.unlock()
            handler?(entry)
        }
    }

    /// Scan com callback por jogo e profundidade configuravel (0 = padrao).
    /// Cada jogo e entregue a `onGame` assim que e detectado.
    public static func scanRoots(
        _ roots: [String],
        splitSavesByGame: Bool = false,
        customSaveRoot: String = "",
        maxDepth: Int = 0,
        onGame: ((DiscoveredGame) -> Void)? = nil
    ) -> [DiscoveredGame] {
        let owned = roots.map { strdup($0)! }
        defer { owned.forEach { free($0) } }
        let pointers = UnsafeMutableBufferPointer<UnsafePointer<CChar>?>.allocate(
            capacity: max(owned.count, 1))
        defer { pointers.deallocate() }
        for (index, value) in owned.enumerated() { pointers[index] = UnsafePointer(value) }

        let box = ScanBox(onGame)
        let userData = Unmanaged.passUnretained(box).toOpaque()
        let callback: @convention(c) (UnsafePointer<CChar>?, UnsafeMutableRawPointer?) -> Void = { json, data in
            guard let json, let data else { return }
            let value = String(cString: json)
            KotonohaLauncherFreeString(UnsafeMutablePointer(mutating: json))
            let array = decode([DiscoveredGame].self,
                               from: "[" + value + "]", fallback: [])
            if let entry = array.first {
                Unmanaged<ScanBox>.fromOpaque(data).takeUnretainedValue().add(entry)
            }
        }
        KotonohaLauncherScanRootsJsonWithCallback(
            pointers.baseAddress, Int32(roots.count),
            splitSavesByGame ? 1 : 0, customSaveRoot, Int32(maxDepth),
            callback, userData)
        return box.entries.sorted { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
    }

    /// Detect a single folder or disc image.
    public static func detectPath(
        _ path: String,
        splitSavesByGame: Bool = false,
        customSaveRoot: String = ""
    ) -> DiscoveredGame? {
        let json = take(KotonohaLauncherDetectPathJson(
            path, splitSavesByGame ? 1 : 0, customSaveRoot))
        guard json != "{}",
              let data = json.data(using: .utf8),
              let entry = try? decoder.decode(DiscoveredGame.self, from: data) else {
            return nil
        }
        return entry
    }

    /// Load persisted per-game parameters as raw JSON.
    public static func loadParams(_ saveProfileId: String) -> String {
        take(KotonohaLauncherLoadGameParamsJson(saveProfileId))
    }

    /// Persist per-game launch parameters.
    public static func saveParams(_ request: LaunchRequest) -> Bool {
        KotonohaLauncherSaveGameParams(
            request.saveProfileId,
            request.assetOverlayPath,
            request.translationPath,
            request.subtitleStylePath,
            request.customDocumentsDir,
            request.ktrfPath,
            request.scriptsDir,
            request.startScenario,
            request.rendererBackend,
            request.upscalerBackend,
            request.upscalerPreset,
            request.upscalerFactor) != 0
    }

    /// Available upscaler shader presets.
    public static func upscalePresets() -> [UpscalePreset] {
        decode([UpscalePreset].self,
               from: take(KotonohaLauncherUpscalePresetsJson()),
               fallback: [])
    }

    /// Resolve the isolated save directory for a profile.
    public static func resolveSaveDirectory(_ request: LaunchRequest) -> String {
        take(KotonohaLauncherResolveSaveDirectory(
            request.gamePath, request.engineSlug, request.saveProfileId,
            request.splitSavesByGame ? 1 : 0, request.customSaveRoot))
    }

    /// Validate a School Days routing graph; returns the diagnostic text.
    public static func validateRoute(_ ktrfPath: String) -> (valid: Bool, info: String) {
        var buffer = [CChar](repeating: 0, count: 1024)
        let valid = buffer.withUnsafeMutableBufferPointer { raw in
            KotonohaLauncherValidateRoute(ktrfPath, raw.baseAddress, Int32(raw.count)) != 0
        }
        return (valid, String(cString: buffer))
    }

    /// Async variant for SwiftUI view models that scan large libraries.
    public static func scanRootsAsync(
        _ roots: [String],
        splitSavesByGame: Bool = false,
        customSaveRoot: String = ""
    ) async -> [DiscoveredGame] {
        await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(returning: scanRoots(
                    roots,
                    splitSavesByGame: splitSavesByGame,
                    customSaveRoot: customSaveRoot))
            }
        }
    }
}
