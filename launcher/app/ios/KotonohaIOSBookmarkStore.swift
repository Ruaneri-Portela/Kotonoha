// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
import Foundation
import SwiftUI
import UIKit
import UniformTypeIdentifiers
import zlib

struct KotonohaUpscalePresetOption: Codable, Identifiable, Equatable, Hashable, Sendable {
    let index: Int
    let name: String
    var id: Int { index }
}

struct KotonohaGameParams: Codable, Equatable, Hashable, Sendable {
    var assetOverlayPath: String = ""
    var translationPath: String = ""
    var subtitleStylePath: String = ""
    var customDocumentsDir: String = ""
    var ktrfPath: String = ""
    var scriptsDir: String = ""
    var startScenario: String = ""
    var rendererBackend: String = ""
    var upscalerBackend: String = ""
    var upscalerPreset: String = ""
    var upscalerFactor: String = ""

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        assetOverlayPath = try c.decodeIfPresent(String.self, forKey: .assetOverlayPath) ?? ""
        translationPath = try c.decodeIfPresent(String.self, forKey: .translationPath) ?? ""
        subtitleStylePath = try c.decodeIfPresent(String.self, forKey: .subtitleStylePath) ?? ""
        customDocumentsDir = try c.decodeIfPresent(String.self, forKey: .customDocumentsDir) ?? ""
        ktrfPath = try c.decodeIfPresent(String.self, forKey: .ktrfPath) ?? ""
        scriptsDir = try c.decodeIfPresent(String.self, forKey: .scriptsDir) ?? ""
        startScenario = try c.decodeIfPresent(String.self, forKey: .startScenario) ?? ""
        rendererBackend = try c.decodeIfPresent(String.self, forKey: .rendererBackend) ?? ""
        upscalerBackend = try c.decodeIfPresent(String.self, forKey: .upscalerBackend) ?? ""
        upscalerPreset = try c.decodeIfPresent(String.self, forKey: .upscalerPreset) ?? ""
        upscalerFactor = try c.decodeIfPresent(String.self, forKey: .upscalerFactor) ?? ""
    }

    var hasAnyOverride: Bool {
        !assetOverlayPath.isEmpty ||
        !translationPath.isEmpty ||
        !subtitleStylePath.isEmpty ||
        !customDocumentsDir.isEmpty ||
        !ktrfPath.isEmpty ||
        !scriptsDir.isEmpty ||
        !startScenario.isEmpty ||
        !rendererBackend.isEmpty ||
        !upscalerBackend.isEmpty ||
        !upscalerPreset.isEmpty ||
        !upscalerFactor.isEmpty
    }
}

struct KotonohaGameItem: Codable, Identifiable, Equatable, Hashable, Sendable {
    var id: String { path }
    let title: String
    let saveProfileId: String
    let path: String
    let overlayPath: String
    let engineSlug: String
    let engineDisplayName: String
    let evidence: String
    let libraryTarget: String
    let targetBinary: String
    let packageId: String
    let saveDirectory: String
    let isDiscImage: Bool
    let linkedInProcess: Bool
    let isSchoolDays: Bool
    var params: KotonohaGameParams
}

enum KotonohaContentTypes {
    static let zipArchives: [UTType] = [.zip]
    static let discImageTypes: [UTType] = {
        var types: [UTType] = []
        if let iso = UTType("public.iso-image") { types.append(iso) }
        if let bin = UTType(filenameExtension: "bin") { types.append(bin) }
        if let img = UTType(filenameExtension: "img") { types.append(img) }
        types.append(.data)
        types.append(.item)
        return types
    }()

    static let routeFileTypes: [UTType] = {
        var types: [UTType] = []
        if let ktn = UTType(filenameExtension: "ktnroute") { types.append(ktn) }
        if let ktrf = UTType(filenameExtension: "ktrf") { types.append(ktrf) }
        types.append(.data)
        types.append(.item)
        return types
    }()

    static let translationFileTypes: [UTType] = {
        var types: [UTType] = []
        if let ktr = UTType(filenameExtension: "ktr") { types.append(ktr) }
        types.append(.json)
        types.append(.plainText)
        types.append(.data)
        return types
    }()

    static let subtitleStyleTypes: [UTType] = {
        var types: [UTType] = []
        if let ass = UTType(filenameExtension: "ass") { types.append(ass) }
        types.append(.plainText)
        types.append(.data)
        return types
    }()
}

@MainActor
final class KotonohaIOSBookmarkStore: ObservableObject {
    static let shared = KotonohaIOSBookmarkStore()

    @Published var games: [KotonohaGameItem] = []
    @Published var bookmarkedPaths: [String] = []
    @Published var isScanning: Bool = false
    @Published var statusMessage: String = ""
    @Published var splitSavesByGame: Bool {
        didSet {
            UserDefaults.standard.set(splitSavesByGame, forKey: Self.splitSavesKey)
            rescan()
        }
    }
    @Published var customSaveRoot: String {
        didSet {
            UserDefaults.standard.set(customSaveRoot, forKey: Self.customSaveRootKey)
        }
    }
    @Published var rendererBackend: String {
        didSet {
            UserDefaults.standard.set(rendererBackend, forKey: Self.rendererBackendKey)
        }
    }
    @Published var upscalerBackend: String {
        didSet {
            UserDefaults.standard.set(upscalerBackend, forKey: Self.upscalerBackendKey)
            if upscalerBackend != "none" && (Double(upscalerFactor) ?? 1.0) <= 1.0 {
                upscalerFactor = "2.0"
            }
        }
    }
    @Published var upscalerPreset: String {
        didSet {
            UserDefaults.standard.set(upscalerPreset, forKey: Self.upscalerPresetKey)
        }
    }
    @Published var upscalerFactor: String {
        didSet {
            UserDefaults.standard.set(upscalerFactor, forKey: Self.upscalerFactorKey)
        }
    }

    let upscalePresets: [KotonohaUpscalePresetOption]

    private static let bookmarksKey = "me.hirameki.kotonoha.bookmarks.v1"
    private static let manualPathsKey = "me.hirameki.kotonoha.manualPaths.v1"
    private static let splitSavesKey = "me.hirameki.kotonoha.splitSaves.v1"
    private static let customSaveRootKey = "me.hirameki.kotonoha.customSaveRoot.v1"
    private static let rendererBackendKey = "me.hirameki.kotonoha.rendererBackend.v1"
    private static let upscalerBackendKey = "me.hirameki.kotonoha.upscalerBackend.v1"
    private static let upscalerPresetKey = "me.hirameki.kotonoha.upscalerPreset.v1"
    private static let upscalerFactorKey = "me.hirameki.kotonoha.upscalerFactor.v1"

    // Retain security-scoped URLs for the lifetime of the app so POSIX/SDL
    // reads from bookmarked folders and disc images never fail with EPERM.
    private var activeScopedURLs: [String: URL] = [:]
    private var activePickerDelegate: DocumentPickerCoordinator?

    private init() {
        let defaults = UserDefaults.standard
        if defaults.object(forKey: Self.splitSavesKey) != nil {
            self.splitSavesByGame = defaults.bool(forKey: Self.splitSavesKey)
        } else {
            self.splitSavesByGame = true
        }
        self.customSaveRoot = defaults.string(forKey: Self.customSaveRootKey) ?? ""
        self.rendererBackend = defaults.string(forKey: Self.rendererBackendKey) ?? ""
        self.upscalerBackend = defaults.string(forKey: Self.upscalerBackendKey) ?? "none"
        self.upscalerPreset = defaults.string(forKey: Self.upscalerPresetKey) ?? "0"
        self.upscalerFactor = defaults.string(forKey: Self.upscalerFactorKey) ?? "2.0"
        self.upscalePresets = Self.loadUpscalePresets()
        restorePersistedBookmarks()
    }

    private static func loadUpscalePresets() -> [KotonohaUpscalePresetOption] {
        guard let ptr = KotonohaLauncherUpscalePresetsJson() else { return [] }
        defer { KotonohaLauncherFreeString(ptr) }
        let data = Data(String(cString: ptr).utf8)
        return (try? JSONDecoder().decode([KotonohaUpscalePresetOption].self, from: data)) ?? []
    }

    func effectiveRenderer(for params: KotonohaGameParams) -> String {
        params.rendererBackend.isEmpty ? rendererBackend : params.rendererBackend
    }

    func effectiveUpscalerBackend(for params: KotonohaGameParams) -> String {
        params.upscalerBackend.isEmpty ? upscalerBackend : params.upscalerBackend
    }

    func effectiveUpscalerPreset(for params: KotonohaGameParams) -> String {
        params.upscalerPreset.isEmpty ? upscalerPreset : params.upscalerPreset
    }

    func effectiveUpscalerFactor(for params: KotonohaGameParams) -> String {
        let backend = effectiveUpscalerBackend(for: params)
        if backend == "none" {
            return "1.0"
        }
        let chosen = params.upscalerFactor.isEmpty ? upscalerFactor : params.upscalerFactor
        if (Double(chosen) ?? 1.0) <= 1.0 {
            return "2.0"
        }
        return chosen
    }

    var documentsDirectoryPath: String {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)
            .first?.path ?? ""
    }

    var documentsGamesPath: String {
        if let raw = KotonohaIOSDocumentsGamesPath() {
            let path = String(cString: raw)
            if !path.isEmpty {
                return path
            }
        }
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        let gamesURL = docs?.appendingPathComponent("games", isDirectory: true)
        if let gamesURL {
            try? FileManager.default.createDirectory(at: gamesURL, withIntermediateDirectories: true)
            return gamesURL.path
        }
        return ""
    }

    var defaultSchoolDaysRoute: String {
        guard let ptr = KotonohaLauncherDefaultSchoolDaysRoute() else { return "" }
        defer { KotonohaLauncherFreeString(ptr) }
        return String(cString: ptr)
    }

    func allScanRoots() -> [String] {
        var roots: [String] = []
        let gamesDir = documentsGamesPath
        if !gamesDir.isEmpty {
            roots.append(gamesDir)
        }
        let docsDir = documentsDirectoryPath
        if !docsDir.isEmpty && docsDir != gamesDir {
            roots.append(docsDir)
        }
        for path in bookmarkedPaths where !path.isEmpty && !roots.contains(path) {
            roots.append(path)
        }
        return roots
    }

    func rescan() {
        let roots = allScanRoots()
        let split = splitSavesByGame ? Int32(1) : Int32(0)
        let saveRoot = customSaveRoot
        isScanning = true
        statusMessage = "Scanning library..."

        Task.detached(priority: .userInitiated) {
            let scanned = Self.scanRootsOffMainThread(
                roots: roots,
                splitSaves: split,
                customSaveRoot: saveRoot
            )
            await MainActor.run {
                self.games = scanned
                self.isScanning = false
                if scanned.isEmpty {
                    self.statusMessage = "No games found yet. Copy games into Files/Kotonoha/games or tap + Folder / + ISO."
                } else {
                    self.statusMessage = "\(scanned.count) game(s) ready"
                }
            }
        }
    }

    func importZip(_ url: URL) {
        isScanning = true
        statusMessage = "Importing \(url.lastPathComponent)..."
        let destination = URL(fileURLWithPath: documentsGamesPath, isDirectory: true)
        Task.detached(priority: .userInitiated) {
            do {
                let imported = try KotonohaZipImporter.extract(url, into: destination)
                await MainActor.run {
                    self.statusMessage = "Imported \(imported.lastPathComponent)"
                    self.rescan()
                }
            } catch {
                await MainActor.run {
                    self.isScanning = false
                    self.statusMessage = "ZIP import failed: \(error.localizedDescription)"
                }
            }
        }
    }

    nonisolated private static func scanRootsOffMainThread(
        roots: [String],
        splitSaves: Int32,
        customSaveRoot: String
    ) -> [KotonohaGameItem] {
        let cStrings: [UnsafeMutablePointer<CChar>?] = roots.map { strdup($0) }
        defer {
            for ptr in cStrings {
                free(ptr)
            }
        }
        let constPtrs: [UnsafePointer<CChar>?] = cStrings.map { UnsafePointer($0) }
        let jsonPtr: UnsafeMutablePointer<CChar>? = constPtrs.withUnsafeBufferPointer { buffer in
            customSaveRoot.withCString { saveRootC in
                KotonohaLauncherScanRootsJson(
                    buffer.baseAddress,
                    Int32(buffer.count),
                    splitSaves,
                    saveRootC
                )
            }
        }
        guard let jsonPtr else { return [] }
        defer { KotonohaLauncherFreeString(jsonPtr) }
        let jsonData = Data(String(cString: jsonPtr).utf8)
        do {
            return try JSONDecoder().decode([KotonohaGameItem].self, from: jsonData)
        } catch {
            NSLog("[KotonohaLauncher] Failed to decode catalog JSON: %@", error.localizedDescription)
            return []
        }
    }

    @discardableResult
    func saveParams(for profileId: String, params: KotonohaGameParams) -> Bool {
        let ok = profileId.withCString { profileC in
            params.assetOverlayPath.withCString { overlayC in
                params.translationPath.withCString { transC in
                    params.subtitleStylePath.withCString { subC in
                        params.customDocumentsDir.withCString { docsC in
                            params.ktrfPath.withCString { ktrfC in
                                params.scriptsDir.withCString { scriptsC in
                                    params.startScenario.withCString { startC in
                                        params.rendererBackend.withCString { rendererC in
                                            params.upscalerBackend.withCString { upscalerC in
                                                params.upscalerPreset.withCString { presetC in
                                                    params.upscalerFactor.withCString { factorC in
                                                        KotonohaLauncherSaveGameParams(
                                                            profileC,
                                                            overlayC,
                                                            transC,
                                                            subC,
                                                            docsC,
                                                            ktrfC,
                                                            scriptsC,
                                                            startC,
                                                            rendererC,
                                                            upscalerC,
                                                            presetC,
                                                            factorC
                                                        )
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if ok != 0 {
            for idx in games.indices where games[idx].saveProfileId == profileId {
                games[idx].params = params
            }
            return true
        }
        return false
    }

    func validateRoute(_ path: String) -> (valid: Bool, message: String) {
        var buffer = [CChar](repeating: 0, count: 512)
        let ok = path.withCString { pathC in
            KotonohaLauncherValidateRoute(pathC, &buffer, Int32(buffer.count))
        }
        let message = String(cString: buffer)
        return (ok != 0, message)
    }

    func addBookmarkedURL(_ url: URL, shouldRescan: Bool = true) {
        let accessed = url.startAccessingSecurityScopedResource()
        let standardized = url.standardizedFileURL
        let path = standardized.path
        if accessed || activeScopedURLs[path] == nil {
            activeScopedURLs[path] = standardized
        }

        do {
            // `.minimalBookmark` cannot be combined with security-scoped
            // bookmarks; CoreFoundation rejects it for folder URLs.
            let bookmark = try standardized.bookmarkData(
                options: [],
                includingResourceValuesForKeys: nil,
                relativeTo: nil
            )
            var stored = loadRawBookmarks()
            if !bookmarkedPaths.contains(path) {
                stored.append(bookmark)
                saveRawBookmarks(stored)
                bookmarkedPaths.append(path)
            }
        } catch {
            NSLog("[KotonohaLauncher] Bookmark creation failed for %@: %@", path, error.localizedDescription)
            if !bookmarkedPaths.contains(path) {
                bookmarkedPaths.append(path)
            }
            persistManualPath(path)
        }

        if shouldRescan {
            rescan()
        }
    }

    private func persistManualPath(_ path: String) {
        var manual = UserDefaults.standard.stringArray(forKey: Self.manualPathsKey) ?? []
        if !manual.contains(path) {
            manual.append(path)
            UserDefaults.standard.set(manual, forKey: Self.manualPathsKey)
        }
    }

    func removeBookmarkedPath(_ path: String) {
        if let url = activeScopedURLs.removeValue(forKey: path) {
            url.stopAccessingSecurityScopedResource()
        }
        bookmarkedPaths.removeAll { $0 == path }
        var manual = UserDefaults.standard.stringArray(forKey: Self.manualPathsKey) ?? []
        manual.removeAll { $0 == path }
        UserDefaults.standard.set(manual, forKey: Self.manualPathsKey)
        rebuildStoredBookmarksFromActive()
        rescan()
    }

    func handleExternalPathOrURL(_ raw: String) {
        guard !raw.isEmpty else { return }
        let url: URL
        if raw.hasPrefix("file://"), let parsed = URL(string: raw) {
            url = parsed
        } else {
            url = URL(fileURLWithPath: raw)
        }
        addBookmarkedURL(url, shouldRescan: true)
    }

    func presentPicker(
        contentTypes: [UTType],
        allowsMultiple: Bool = false,
        bookmarkInLibrary: Bool = true,
        completion: @escaping (URL?) -> Void
    ) {
        guard let presenter = Self.topViewController() else {
            completion(nil)
            return
        }
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: contentTypes,
            asCopy: false
        )
        picker.allowsMultipleSelection = allowsMultiple
        picker.shouldShowFileExtensions = true

        let coordinator = DocumentPickerCoordinator { [weak self] urls in
            Task { @MainActor in
                guard let self else { return }
                self.activePickerDelegate = nil
                guard !urls.isEmpty else {
                    completion(nil)
                    return
                }
                for url in urls {
                    if bookmarkInLibrary {
                        self.addBookmarkedURL(url, shouldRescan: false)
                    } else {
                        _ = url.startAccessingSecurityScopedResource()
                        let standardized = url.standardizedFileURL
                        self.activeScopedURLs[standardized.path] = standardized
                    }
                }
                if bookmarkInLibrary {
                    self.rescan()
                }
                completion(urls.first)
            }
        }
        self.activePickerDelegate = coordinator
        picker.delegate = coordinator
        presenter.present(picker, animated: true)
    }

    private func restorePersistedBookmarks() {
        let rawBookmarks = loadRawBookmarks()
        var refreshed: [Data] = []
        var paths: [String] = []

        for data in rawBookmarks {
            var isStale = false
            do {
                let url = try URL(
                    resolvingBookmarkData: data,
                    options: [],
                    relativeTo: nil,
                    bookmarkDataIsStale: &isStale
                )
                _ = url.startAccessingSecurityScopedResource()
                let standardized = url.standardizedFileURL
                let path = standardized.path
                activeScopedURLs[path] = standardized
                if !paths.contains(path) {
                    paths.append(path)
                }
                if isStale,
                   let fresh = try? standardized.bookmarkData(
                       options: [],
                       includingResourceValuesForKeys: nil,
                       relativeTo: nil
                   ) {
                    refreshed.append(fresh)
                } else {
                    refreshed.append(data)
                }
            } catch {
                NSLog("[KotonohaLauncher] Skipping unresolvable bookmark: %@", error.localizedDescription)
            }
        }

        if refreshed.count != rawBookmarks.count {
            saveRawBookmarks(refreshed)
        }
        let manualPaths = UserDefaults.standard.stringArray(forKey: Self.manualPathsKey) ?? []
        for manual in manualPaths where !manual.isEmpty && !paths.contains(manual) {
            paths.append(manual)
        }
        self.bookmarkedPaths = paths
    }

    private func rebuildStoredBookmarksFromActive() {
        var newData: [Data] = []
        for path in bookmarkedPaths {
            if let url = activeScopedURLs[path],
               let data = try? url.bookmarkData(
                   options: [],
                   includingResourceValuesForKeys: nil,
                   relativeTo: nil
               ) {
                newData.append(data)
            }
        }
        saveRawBookmarks(newData)
    }

    private func loadRawBookmarks() -> [Data] {
        UserDefaults.standard.array(forKey: Self.bookmarksKey) as? [Data] ?? []
    }

    private func saveRawBookmarks(_ bookmarks: [Data]) {
        UserDefaults.standard.set(bookmarks, forKey: Self.bookmarksKey)
    }

    static func topViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
        let keyWindow = scenes
            .flatMap { $0.windows }
            .first { $0.isKeyWindow } ?? scenes.first?.windows.first
        var current = keyWindow?.rootViewController
        while let presented = current?.presentedViewController {
            current = presented
        }
        return current
    }
}

private final class DocumentPickerCoordinator: NSObject, UIDocumentPickerDelegate {
    private let onComplete: ([URL]) -> Void

    init(onComplete: @escaping ([URL]) -> Void) {
        self.onComplete = onComplete
    }

    func documentPicker(
        _ controller: UIDocumentPickerViewController,
        didPickDocumentsAt urls: [URL]
    ) {
        onComplete(urls)
    }

    func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
        onComplete([])
    }
}

private enum KotonohaZipImporter {
    private struct Entry {
        let name: String
        let method: UInt16
        let crc: UInt32
        let compressed: UInt64
        let expanded: UInt64
        let localOffset: UInt64
        let isDirectory: Bool
    }

    private static func failure(_ message: String) -> NSError {
        NSError(domain: "KotonohaZipImporter", code: 1,
                userInfo: [NSLocalizedDescriptionKey: message])
    }

    private static func number(_ data: Data, _ offset: Int, _ count: Int) throws -> UInt64 {
        guard offset >= 0, count > 0, offset + count <= data.count else {
            throw failure("Truncated ZIP directory")
        }
        var value: UInt64 = 0
        for index in 0..<count {
            value |= UInt64(data[offset + index]) << (index * 8)
        }
        return value
    }

    private static func read(_ file: FileHandle, at offset: UInt64, count: Int) throws -> Data {
        try file.seek(toOffset: offset)
        let bytes = try file.read(upToCount: count) ?? Data()
        guard bytes.count == count else { throw failure("Truncated ZIP archive") }
        return bytes
    }

    static func extract(_ archive: URL, into games: URL) throws -> URL {
        let file = try FileHandle(forReadingFrom: archive)
        defer { try? file.close() }
        let length = try file.seekToEnd()
        guard length >= 22 else { throw failure("Invalid ZIP archive") }
        let tailLength = Int(min(length, 65_557))
        let tail = try read(file, at: length - UInt64(tailLength), count: tailLength)
        var end: Int?
        for offset in stride(from: tail.count - 22, through: 0, by: -1) {
            if try number(tail, offset, 4) == 0x06054b50 {
                end = offset
                break
            }
        }
        guard let end else { throw failure("ZIP directory was not found") }
        var count = try number(tail, end + 10, 2)
        var directorySize = try number(tail, end + 12, 4)
        var directoryOffset = try number(tail, end + 16, 4)
        if count == 0xffff || directoryOffset == 0xffff_ffff ||
            directorySize == 0xffff_ffff {
            let eocdOffset = length - UInt64(tailLength) + UInt64(end)
            guard eocdOffset >= 20 else { throw failure("Invalid ZIP64 footer") }
            let locator = try read(file, at: eocdOffset - 20, count: 20)
            guard try number(locator, 0, 4) == 0x07064b50 else {
                throw failure("ZIP64 footer was not found")
            }
            let record = try read(file, at: number(locator, 8, 8), count: 56)
            guard try number(record, 0, 4) == 0x06064b50 else {
                throw failure("Invalid ZIP64 footer")
            }
            count = try number(record, 32, 8)
            directorySize = try number(record, 40, 8)
            directoryOffset = try number(record, 48, 8)
        }
        guard count > 0, count <= 100_000,
              directoryOffset <= length,
              directorySize <= length - directoryOffset else {
            throw failure("Invalid ZIP directory size")
        }
        var entries: [Entry] = []
        var cursor = directoryOffset
        var totalSize: UInt64 = 0
        for _ in 0..<count {
            let header = try read(file, at: cursor, count: 46)
            guard try number(header, 0, 4) == 0x02014b50 else {
                throw failure("Invalid ZIP entry")
            }
            let flags = try number(header, 8, 2)
            let method = UInt16(try number(header, 10, 2))
            var compressed = try number(header, 20, 4)
            var expanded = try number(header, 24, 4)
            var localOffset = try number(header, 42, 4)
            let nameLength = Int(try number(header, 28, 2))
            let extraLength = Int(try number(header, 30, 2))
            let commentLength = Int(try number(header, 32, 2))
            let external = try number(header, 38, 4)
            let payload = try read(file, at: cursor + 46,
                                   count: nameLength + extraLength)
            guard let name = String(data: payload.prefix(nameLength), encoding: .utf8) else {
                throw failure("ZIP contains a filename that is not UTF-8")
            }
            let unixMode = (external >> 16) & 0xf000
            guard flags & 1 == 0, unixMode != 0xa000,
                  method == 0 || method == 8 else {
                throw failure("Encrypted, linked, or unsupported ZIP entry: \(name)")
            }
            let extra = payload.dropFirst(nameLength)
            if compressed == 0xffff_ffff || expanded == 0xffff_ffff ||
                localOffset == 0xffff_ffff {
                var position = 0
                var found = false
                while position + 4 <= extra.count {
                    let part = Data(extra.dropFirst(position))
                    let tag = try number(part, 0, 2)
                    let size = Int(try number(part, 2, 2))
                    guard position + 4 + size <= extra.count else {
                        throw failure("Invalid ZIP64 entry")
                    }
                    if tag == 1 {
                        var field = 4
                        if expanded == 0xffff_ffff {
                            expanded = try number(part, field, 8)
                            field += 8
                        }
                        if compressed == 0xffff_ffff {
                            compressed = try number(part, field, 8)
                            field += 8
                        }
                        if localOffset == 0xffff_ffff {
                            localOffset = try number(part, field, 8)
                        }
                        found = true
                        break
                    }
                    position += 4 + size
                }
                guard found else { throw failure("Missing ZIP64 entry size") }
            }
            totalSize += expanded
            guard totalSize <= 64 * 1024 * 1024 * 1024 else {
                throw failure("ZIP expands beyond the 64 GiB limit")
            }
            entries.append(Entry(name: name, method: method,
                                 crc: UInt32(try number(header, 16, 4)),
                                 compressed: compressed, expanded: expanded,
                                 localOffset: localOffset,
                                 isDirectory: name.hasSuffix("/")))
            cursor += UInt64(46 + nameLength + extraLength + commentLength)
        }

        try FileManager.default.createDirectory(at: games,
                                                withIntermediateDirectories: true)
        let rawName = archive.deletingPathExtension().lastPathComponent
        let name = rawName.isEmpty || rawName == "." || rawName == ".."
            ? "Imported Game" : rawName.replacingOccurrences(of: "/", with: "_")
        var destination = games.appendingPathComponent(name, isDirectory: true)
        var suffix = 2
        while FileManager.default.fileExists(atPath: destination.path) {
            destination = games.appendingPathComponent("\(name)-\(suffix)",
                                                       isDirectory: true)
            suffix += 1
        }
        let staging = games.appendingPathComponent(".import-\(UUID().uuidString)",
                                                  isDirectory: true)
        try FileManager.default.createDirectory(at: staging,
                                                withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: staging) }
        for entry in entries {
            let parts = entry.name.replacingOccurrences(of: "\\", with: "/")
                .split(separator: "/", omittingEmptySubsequences: false)
            guard !entry.name.hasPrefix("/"),
                  !parts.contains(where: { $0 == ".." || $0 == "." || $0.contains(":") }),
                  !parts.isEmpty else {
                throw failure("Unsafe ZIP path: \(entry.name)")
            }
            let output = parts.filter { !$0.isEmpty }.reduce(staging) {
                $0.appendingPathComponent(String($1))
            }
            guard output.standardizedFileURL.path.hasPrefix(staging.path + "/") else {
                throw failure("Unsafe ZIP path: \(entry.name)")
            }
            if entry.isDirectory {
                try FileManager.default.createDirectory(at: output,
                                                        withIntermediateDirectories: true)
                continue
            }
            let local = try read(file, at: entry.localOffset, count: 30)
            guard try number(local, 0, 4) == 0x04034b50 else {
                throw failure("Invalid ZIP local entry")
            }
            let dataOffset = entry.localOffset + 30 +
                (try number(local, 26, 2)) + (try number(local, 28, 2))
            guard dataOffset <= length, entry.compressed <= length - dataOffset else {
                throw failure("ZIP entry extends beyond archive")
            }
            try FileManager.default.createDirectory(
                at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
            FileManager.default.createFile(atPath: output.path, contents: nil)
            let writer = try FileHandle(forWritingTo: output)
            defer { try? writer.close() }
            try file.seek(toOffset: dataOffset)
            var remaining = entry.compressed
            var written: UInt64 = 0
            var checksum = crc32(0, nil, 0)
            var stream = z_stream()
            if entry.method == 8 {
                guard inflateInit2_(&stream, -15, ZLIB_VERSION,
                                    Int32(MemoryLayout<z_stream>.size)) == Z_OK else {
                    throw failure("Cannot initialize ZIP decompressor")
                }
            }
            defer { if entry.method == 8 { inflateEnd(&stream) } }
            var finished = entry.method == 0
            while remaining > 0 {
                let chunk = try file.read(upToCount: Int(min(remaining, 65_536))) ?? Data()
                guard !chunk.isEmpty else { throw failure("Truncated ZIP entry") }
                remaining -= UInt64(chunk.count)
                if entry.method == 0 {
                    try writer.write(contentsOf: chunk)
                    written += UInt64(chunk.count)
                    checksum = chunk.withUnsafeBytes {
                        crc32(checksum, $0.bindMemory(to: Bytef.self).baseAddress,
                              uInt(chunk.count))
                    }
                    continue
                }
                try chunk.withUnsafeBytes { source in
                    stream.next_in = UnsafeMutablePointer<Bytef>(mutating:
                        source.bindMemory(to: Bytef.self).baseAddress!)
                    stream.avail_in = uInt(chunk.count)
                    repeat {
                        var outputBytes = [UInt8](repeating: 0, count: 65_536)
                        let result = outputBytes.withUnsafeMutableBufferPointer { buffer -> Int32 in
                            stream.next_out = buffer.baseAddress
                            stream.avail_out = uInt(buffer.count)
                            return inflate(&stream, Z_NO_FLUSH)
                        }
                        guard result == Z_OK || result == Z_STREAM_END else {
                            throw failure("Corrupt compressed ZIP entry")
                        }
                        let produced = outputBytes.count - Int(stream.avail_out)
                        if produced > 0 {
                            let data = Data(outputBytes.prefix(produced))
                            try writer.write(contentsOf: data)
                            written += UInt64(produced)
                            checksum = data.withUnsafeBytes {
                                crc32(checksum, $0.bindMemory(to: Bytef.self).baseAddress,
                                      uInt(produced))
                            }
                        }
                        if result == Z_STREAM_END { finished = true }
                    } while stream.avail_in > 0 && !finished
                }
            }
            guard finished, written == entry.expanded,
                  UInt32(checksum) == entry.crc else {
                throw failure("ZIP entry size or checksum mismatch: \(entry.name)")
            }
        }
        let topLevel = try FileManager.default.contentsOfDirectory(
            at: staging, includingPropertiesForKeys: [.isDirectoryKey])
        let source: URL
        if topLevel.count == 1,
           (try topLevel[0].resourceValues(forKeys: [.isDirectoryKey])).isDirectory == true {
            source = topLevel[0]
        } else {
            source = staging
        }
        try FileManager.default.moveItem(at: source, to: destination)
        return destination
    }
}
