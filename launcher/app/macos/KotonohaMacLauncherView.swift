// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
import AppKit
import Foundation
import SwiftUI
import UniformTypeIdentifiers

// Cartilha visual Kotonoha v2, variante "Soft" compartilhada com ImGui.
// Os tokens oficiais continuam disponiveis; a UI usa a derivacao dessaturada
// para evitar contraste agressivo. Native Liquid Glass stays system-owned.
private enum KotonohaBrand {
    // Paleta oficial v2 (identidade da marca).
    static let navy = Color(red: 13.0/255, green: 20.0/255, blue: 46.0/255)
    static let raised = Color(red: 24.0/255, green: 36.0/255, blue: 73.0/255)
    static let indigo = Color(red: 44.0/255, green: 62.0/255, blue: 154.0/255)
    static let violet = Color(red: 106.0/255, green: 91.0/255, blue: 231.0/255)
    static let magenta = Color(red: 200.0/255, green: 107.0/255, blue: 217.0/255)
    static let cyan = Color(red: 0.0, green: 214.0/255, blue: 1.0)
    static let sakura = Color(red: 1.0, green: 143.0/255, blue: 189.0/255)
    static let outline = Color(red: 57.0/255, green: 49.0/255, blue: 94.0/255)
    static let muted = Color(red: 185.0/255, green: 181.0/255, blue: 212.0/255)

    // Variante Soft — mesmos valores de lib/src/debug/ImGuiTheme.cpp.
    static let accent = Color(red: 167.0/255, green: 155.0/255, blue: 212.0/255)
    static let accentSoft = Color(red: 142.0/255, green: 134.0/255, blue: 184.0/255)
    static let accentHover = Color(red: 195.0/255, green: 180.0/255, blue: 224.0/255)
    static let surface = Color(red: 36.0/255, green: 48.0/255, blue: 75.0/255)
    static let surfaceRaised = Color(red: 44.0/255, green: 56.0/255, blue: 86.0/255)
    static let softOutline = Color(red: 60.0/255, green: 71.0/255, blue: 101.0/255)
    static let text = Color(red: 222.0/255, green: 229.0/255, blue: 242.0/255)
    static let success = Color(red: 134.0/255, green: 199.0/255, blue: 167.0/255)
    static let warning = Color(red: 220.0/255, green: 197.0/255, blue: 156.0/255)
    static let error = Color(red: 221.0/255, green: 164.0/255, blue: 173.0/255)

    // Vivid rose family used for primary actions and the "End" action.
    static let rose = Color(red: 1.0, green: 0.35, blue: 0.58)
    static let roseDeep = Color(red: 0.91, green: 0.22, blue: 0.46)
    static let roseSoft = Color(red: 1.0, green: 0.62, blue: 0.74)
}

// Espacamentos 4/8/16/24 e raios 8/12/20 de design/tokens.json.
private enum KotonohaMetrics {
    static let xs: CGFloat = 4
    static let sm: CGFloat = 8
    static let md: CGFloat = 16
    static let lg: CGFloat = 24
    static let controlRadius: CGFloat = 8
    static let cardRadius: CGFloat = 12
    static let sheetRadius: CGFloat = 20
}

// MARK: - Data Models

private struct MacUpscalePresetOption: Codable, Identifiable, Equatable, Hashable, Sendable {
    let index: Int
    let name: String
    var id: Int { index }
}

private struct MacGameParams: Codable, Equatable, Hashable, Sendable {
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

private struct MacGameItem: Codable, Identifiable, Equatable, Hashable, Sendable {
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
    var params: MacGameParams
}

private enum MacGraphicsOptions {
    struct Option: Identifiable {
        let id: String
        let label: String
    }

    static let renderers: [Option] = [
        Option(id: "", label: "Automatic"),
        Option(id: "metal", label: "Metal"),
        Option(id: "opengl", label: "OpenGL"),
        Option(id: "software", label: "Software")
    ]

    static let upscalers: [Option] = [
        Option(id: "none", label: "Off"),
        Option(id: "auto", label: "Auto (MetalFX / OpenGL)"),
        Option(id: "metalfx", label: "MetalFX"),
        Option(id: "mpv", label: "MPV / Anime4K / ArtCNN (OpenGL)"),
        Option(id: "linear", label: "Linear")
    ]

    static let scaleFactors: [String] = ["1.5", "2.0", "3.0", "4.0"]

    static func rendererLabel(_ value: String) -> String {
        switch value {
        case "": return "Auto"
        case "metal": return "Metal"
        case "opengl": return "OpenGL"
        case "software": return "Software"
        default: return value
        }
    }

    static func upscalerLabel(_ value: String) -> String {
        switch value {
        case "none": return "Off"
        case "auto": return "Auto"
        case "metalfx": return "MetalFX"
        case "mpv": return "MPV"
        case "linear": return "Linear"
        default: return value
        }
    }
}

// MARK: - Liquid Glass Helpers (macOS 26+ with .ultraThinMaterial fallback)

private struct MacGlassSurfaceModifier: ViewModifier {
    let cornerRadius: CGFloat
    let interactive: Bool

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
        if #available(macOS 26.0, *) {
            if interactive {
                content.glassEffect(.regular.interactive(), in: shape)
            } else {
                content.glassEffect(.regular, in: shape)
            }
        } else {
            content
                .background(.ultraThinMaterial, in: shape)
                .overlay(
                    shape.strokeBorder(KotonohaBrand.accent.opacity(0.30), lineWidth: 1)
                )
        }
    }
}

private struct MacGlassButtonModifier: ViewModifier {
    let prominent: Bool
    let tone: Color

    func body(content: Content) -> some View {
        let styled: AnyView = {
            if #available(macOS 26.0, *) {
                if prominent {
                    return AnyView(content.buttonStyle(.glassProminent))
                }
                return AnyView(content.buttonStyle(.glass))
            }
            if prominent {
                return AnyView(content.buttonStyle(.borderedProminent))
            }
            return AnyView(content.buttonStyle(.bordered))
        }()
        return styled
            .tint(tone)
            .foregroundStyle(prominent ? .white : tone)
    }
}

private struct MacGlassContainer<Content: View>: View {
    let spacing: CGFloat
    @ViewBuilder let content: () -> Content

    init(spacing: CGFloat = 14, @ViewBuilder content: @escaping () -> Content) {
        self.spacing = spacing
        self.content = content
    }

    var body: some View {
        if #available(macOS 26.0, *) {
            GlassEffectContainer(spacing: spacing) {
                content()
            }
        } else {
            content()
        }
    }
}

private extension View {
    func macGlassSurface(cornerRadius: CGFloat = 20, interactive: Bool = false) -> some View {
        modifier(MacGlassSurfaceModifier(cornerRadius: cornerRadius, interactive: interactive))
    }

    func macGlassButton(prominent: Bool = false,
                        tone: Color = KotonohaBrand.rose) -> some View {
        modifier(MacGlassButtonModifier(prominent: prominent, tone: tone))
    }
}

private struct MacAmbientBackdrop: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    KotonohaBrand.navy,
                    KotonohaBrand.surface,
                    KotonohaBrand.indigo.opacity(0.22)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            RadialGradient(
                colors: [KotonohaBrand.cyan.opacity(0.08), .clear],
                center: .topLeading,
                startRadius: 40,
                endRadius: 560
            )
            RadialGradient(
                colors: [KotonohaBrand.accent.opacity(0.10), .clear],
                center: .bottomTrailing,
                startRadius: 60,
                endRadius: 560
            )
        }
        .overlay(alignment: .bottomTrailing) {
            Text("✿")
                .font(.system(size: 138, weight: .ultraLight))
                .foregroundStyle(KotonohaBrand.sakura.opacity(0.05))
                .padding(28)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
        }
        .ignoresSafeArea()
    }
}

// MARK: - Library & Settings Store

// Minimal EN/PT dictionary for launcher UI strings. The selected Apple
// language drives it; pt-BR falls back to Portuguese, everything else to
// English. Code symbols and CLI arguments are never translated.
@MainActor
private enum MacL10n {
    enum Key: String {
        case play, end, running, gameLog, noLog, launchFailed, ready
    }

    private static let ptBR: [Key: String] = [
        .play: "Jogar", .end: "Encerrar", .running: "rodando",
        .gameLog: "Log do jogo", .noLog: "Nenhuma saída ainda.",
        .launchFailed: "Falha ao iniciar", .ready: "Pronto"
    ]
    private static let en: [Key: String] = [
        .play: "Play", .end: "End", .running: "running",
        .gameLog: "Game log", .noLog: "No output yet.",
        .launchFailed: "Launch failed", .ready: "Ready"
    ]

    static var isPortuguese: Bool {
        let languages = UserDefaults.standard.stringArray(forKey: "AppleLanguages")
        return (languages?.first ?? Locale.current.identifier)
            .lowercased().hasPrefix("pt")
    }

    static func text(_ key: Key) -> String {
        (isPortuguese ? ptBR : en)[key] ?? key.rawValue
    }
}

@MainActor
private final class MacLauncherStore: ObservableObject {
    static let shared = MacLauncherStore()

    @Published var games: [MacGameItem] = []
    @Published var roots: [String] = []
    @Published var isScanning: Bool = false
    @Published var statusMessage: String = ""
    /// PID of every detached host, keyed by game path. Each entry is polled
    /// so a game that exits releases its "End" button and can be relaunched.
    @Published var runningPids: [String: Int32] = [:]
    @Published var logLines: [String] = []
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

    let upscalePresets: [MacUpscalePresetOption]
    private var logFilePath: URL?

    private static let rootsKey = "KotonohaMacLibraryRoots"
    private static let splitSavesKey = "me.hirameki.kotonoha.mac.splitSaves.v1"
    private static let customSaveRootKey = "me.hirameki.kotonoha.mac.customSaveRoot.v1"
    private static let rendererBackendKey = "me.hirameki.kotonoha.mac.rendererBackend.v1"
    private static let upscalerBackendKey = "me.hirameki.kotonoha.mac.upscalerBackend.v1"
    private static let upscalerPresetKey = "me.hirameki.kotonoha.mac.upscalerPreset.v1"
    private static let upscalerFactorKey = "me.hirameki.kotonoha.mac.upscalerFactor.v1"

    private init() {
        let defaults = UserDefaults.standard
        self.roots = defaults.stringArray(forKey: Self.rootsKey) ?? []
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
        configureGameLog()
    }

    // The detached host inherits this environment variable and writes its
    // stdout/stderr there; the hub tails it like the .NET manager does.
    private func configureGameLog() {
        guard let support = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask).first else { return }
        let directory = support.appendingPathComponent(
            "Kotonoha/me.hirameki.kotonoha", isDirectory: true)
        try? FileManager.default.createDirectory(
            at: directory, withIntermediateDirectories: true)
        let path = directory.appendingPathComponent("game-host.log")
        logFilePath = path
        setenv("KOTONOHA_DETACHED_LOG", path.path, 1)
        Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                guard let self, let path = self.logFilePath,
                      let text = try? String(contentsOf: path, encoding: .utf8)
                else { return }
                let lines = text.split(separator: "\n").map(String.init).suffix(200)
                if self.logLines != Array(lines) {
                    self.logLines = Array(lines)
                }
                // The spawned host is detached (PPID 1), so poll it directly.
                for (path, pid) in self.runningPids
                where pid > 0 && kill(pid, 0) != 0 {
                    self.markStopped(path: path)
                }
            }
        }
    }

    func resetLog() {
        guard let path = logFilePath else { return }
        try? Data().write(to: path)
        logLines = []
    }

    func isRunning(_ path: String) -> Bool {
        (runningPids[path] ?? 0) > 0
    }

    func markRunning(pid: Int32, path: String) {
        runningPids[path] = pid
        statusMessage = "\(MacL10n.text(.running)) — \(pid)"
    }

    func markStopped(path: String) {
        runningPids.removeValue(forKey: path)
        if runningPids.isEmpty {
            statusMessage = MacL10n.text(.ready)
        }
    }

    func terminateRunning(path: String) {
        guard let pid = runningPids[path], pid > 0 else { return }
        if let ptr = KotonohaLauncherTerminateGame(pid) {
            KotonohaLauncherFreeString(ptr)
        }
        markStopped(path: path)
    }

    private static func loadUpscalePresets() -> [MacUpscalePresetOption] {
        guard let ptr = KotonohaLauncherUpscalePresetsJson() else { return [] }
        defer { KotonohaLauncherFreeString(ptr) }
        let data = Data(String(cString: ptr).utf8)
        return (try? JSONDecoder().decode([MacUpscalePresetOption].self, from: data)) ?? []
    }

    var defaultSchoolDaysRoute: String {
        guard let ptr = KotonohaLauncherDefaultSchoolDaysRoute() else { return "" }
        defer { KotonohaLauncherFreeString(ptr) }
        return String(cString: ptr)
    }

    func effectiveUpscalerBackend(for params: MacGameParams) -> String {
        params.upscalerBackend.isEmpty ? upscalerBackend : params.upscalerBackend
    }

    func effectiveRenderer(for params: MacGameParams) -> String {
        let chosen = params.rendererBackend.isEmpty ? rendererBackend : params.rendererBackend
        if chosen.isEmpty && effectiveUpscalerBackend(for: params) == "mpv" {
            return "opengl"
        }
        return chosen
    }

    func effectiveUpscalerPreset(for params: MacGameParams) -> String {
        params.upscalerPreset.isEmpty ? upscalerPreset : params.upscalerPreset
    }

    func effectiveUpscalerFactor(for params: MacGameParams) -> String {
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

    func rescan() {
        let scanRoots = roots
        let split = splitSavesByGame ? Int32(1) : Int32(0)
        let saveRoot = customSaveRoot
        isScanning = true
        statusMessage = "Scanning library..."
        Task.detached(priority: .userInitiated) {
            let discovered = Self.scanOffMain(
                roots: scanRoots,
                splitSaves: split,
                customSaveRoot: saveRoot
            )
            await MainActor.run {
                self.games = discovered
                self.isScanning = false
                self.statusMessage = discovered.isEmpty
                    ? "Add a game folder or ISO/BIN disc image."
                    : "\(discovered.count) game(s) ready"
            }
        }
    }

    nonisolated private static func scanOffMain(
        roots: [String],
        splitSaves: Int32,
        customSaveRoot: String
    ) -> [MacGameItem] {
        let cPaths = roots.map { strdup($0) }
        defer { cPaths.forEach { free($0) } }
        let pointers: [UnsafePointer<CChar>?] = cPaths.map { UnsafePointer($0) }
        let result = pointers.withUnsafeBufferPointer { buffer in
            customSaveRoot.withCString { saveRootC in
                KotonohaLauncherScanRootsJson(
                    buffer.baseAddress,
                    Int32(buffer.count),
                    splitSaves,
                    saveRootC
                )
            }
        }
        guard let result else { return [] }
        defer { KotonohaLauncherFreeString(result) }
        let bytes = Data(String(cString: result).utf8)
        return (try? JSONDecoder().decode([MacGameItem].self, from: bytes)) ?? []
    }

    func addFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            addRoots(panel.urls.map(\.path))
        }
    }

    func addDisc() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.diskImage, .data]
        if panel.runModal() == .OK {
            addRoots(panel.urls.map(\.path))
        }
    }

    func browseFolder() -> String? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        return panel.runModal() == .OK ? panel.url?.path : nil
    }

    func browseFile() -> String? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        return panel.runModal() == .OK ? panel.url?.path : nil
    }

    func addRoots(_ paths: [String]) {
        roots = Array(Set(roots + paths)).sorted()
        UserDefaults.standard.set(roots, forKey: Self.rootsKey)
        rescan()
    }

    func removeRoot(_ path: String) {
        roots.removeAll { $0 == path }
        UserDefaults.standard.set(roots, forKey: Self.rootsKey)
        rescan()
    }

    @discardableResult
    func saveParams(for profileId: String, params: MacGameParams) -> Bool {
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
        return (ok != 0, String(cString: buffer))
    }

    // DetectPathJson via the shared C ABI, surfaced as a short UI message.
    func detectPath(_ path: String) -> String {
        let split = splitSavesByGame ? Int32(1) : Int32(0)
        let json: String? = path.withCString { pathC in
            customSaveRoot.withCString { rootC in
                guard let ptr = KotonohaLauncherDetectPathJson(
                    pathC, split, rootC
                ) else { return nil }
                defer { KotonohaLauncherFreeString(ptr) }
                return String(cString: ptr)
            }
        }
        guard let json, let data = json.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return "No engine detected in \(URL(fileURLWithPath: path).lastPathComponent)"
        }
        let title = object["title"] as? String ?? ""
        let engine = object["engineDisplayName"] as? String
            ?? object["engineSlug"] as? String ?? "unknown engine"
        return title.isEmpty
            ? "No engine detected in \(URL(fileURLWithPath: path).lastPathComponent)"
            : "Detected: \(title) — \(engine)"
    }

    // ResolveSaveDirectory via the shared C ABI; the catalog value is the
    // fallback when the native library is unavailable.
    func resolvedSaveDirectory(for game: MacGameItem) -> String {
        let fallback = game.saveDirectory
        let split = splitSavesByGame ? Int32(1) : Int32(0)
        let resolved: String? = game.path.withCString { gameC in
            game.engineSlug.withCString { engineC in
                game.saveProfileId.withCString { profileC in
                    customSaveRoot.withCString { rootC in
                        guard let ptr = KotonohaLauncherResolveSaveDirectory(
                            gameC, engineC, profileC, split, rootC
                        ) else { return nil }
                        defer { KotonohaLauncherFreeString(ptr) }
                        let value = String(cString: ptr)
                        return value.isEmpty ? nil : value
                    }
                }
            }
        }
        return resolved ?? fallback
    }
}

// MARK: - Main macOS Liquid Glass Launcher View

private struct MacLauncherView: View {
    @ObservedObject var store: MacLauncherStore
    let onPlay: (MacGameItem) -> Int32
    let onQuit: () -> Void

    @State private var searchText: String = ""
    @State private var selectedGameId: String?
    @State private var showingParamsSheet: Bool = false
    @State private var showingSettingsSheet: Bool = false

    private var filteredGames: [MacGameItem] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty else { return store.games }
        return store.games.filter { item in
            item.title.localizedCaseInsensitiveContains(query) ||
            item.engineDisplayName.localizedCaseInsensitiveContains(query) ||
            item.engineSlug.localizedCaseInsensitiveContains(query)
        }
    }

    private var selectedGame: MacGameItem? {
        if let id = selectedGameId,
           let match = store.games.first(where: { $0.id == id }) {
            return match
        }
        return filteredGames.first
    }

    var body: some View {
        ZStack {
            MacAmbientBackdrop()

            VStack(spacing: KotonohaMetrics.md) {
                headerBar
                contentSplit
            }
            .padding(.horizontal, KotonohaMetrics.lg)
            .padding(.vertical, KotonohaMetrics.md)
            .clipShape(
                RoundedRectangle(
                    cornerRadius: KotonohaMetrics.sheetRadius,
                    style: .continuous
                )
            )
        }
        .frame(minWidth: 880, minHeight: 560)
        .tint(KotonohaBrand.accent)
        .preferredColorScheme(.dark)
        .onAppear {
            store.rescan()
        }
        .onChange(of: store.games) { _, newGames in
            if selectedGameId == nil || !newGames.contains(where: { $0.id == selectedGameId }) {
                selectedGameId = newGames.first?.id
            }
        }
        .sheet(isPresented: $showingParamsSheet) {
            if let game = selectedGame {
                MacGameParametersSheet(store: store, game: game)
            }
        }
        .sheet(isPresented: $showingSettingsSheet) {
            MacSettingsSheet(store: store)
        }
    }

    private var headerBar: some View {
        MacGlassContainer(spacing: 10) {
            HStack(spacing: 10) {
                HStack(spacing: 10) {
                    Image(systemName: "sparkles.rectangle.stack.fill")
                        .font(.title2.weight(.semibold))
                        .foregroundStyle(KotonohaBrand.accent)
                    VStack(alignment: .leading, spacing: 1) {
                        Text("Kotonoha")
                            .font(.headline.weight(.bold))
                            .foregroundStyle(.primary)
                        Text(store.statusMessage.isEmpty ? "Universal Visual Novel Hub" : store.statusMessage)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .macGlassSurface(cornerRadius: 18)

                HStack(spacing: 8) {
                    Image(systemName: "magnifyingglass")
                        .foregroundStyle(.secondary)
                    TextField("Search games or engines...", text: $searchText)
                        .textFieldStyle(.plain)
                    if !searchText.isEmpty {
                        Button {
                            searchText = ""
                        } label: {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundStyle(.secondary)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .macGlassSurface(cornerRadius: 18)

                Button(action: store.addFolder) {
                    Label("Add Folder/Library", systemImage: "folder.badge.plus")
                        .font(.subheadline.weight(.semibold))
                }
                .macGlassButton()

                Button(action: store.addDisc) {
                    Label("Add ISO/BIN", systemImage: "opticaldisc.fill")
                        .font(.subheadline.weight(.semibold))
                }
                .macGlassButton()

                Button(action: store.rescan) {
                    Image(systemName: store.isScanning ? "arrow.triangle.2.circlepath.circle.fill" : "arrow.clockwise")
                        .font(.subheadline.weight(.semibold))
                }
                .macGlassButton()
                .disabled(store.isScanning)
                .help("Rescan Library")

                Button {
                    showingSettingsSheet = true
                } label: {
                    Image(systemName: "slider.horizontal.3")
                        .font(.subheadline.weight(.semibold))
                }
                .macGlassButton()
                .help("Graphics, Upscaler & Library Sources")

                Button(action: onQuit) {
                    Image(systemName: "power")
                        .font(.subheadline.weight(.semibold))
                }
                .macGlassButton()
                .help("Quit")
            }
        }
    }

    private var contentSplit: some View {
        HStack(spacing: 16) {
            libraryListPane
                .frame(minWidth: 280, idealWidth: 330, maxWidth: 400)
            detailPane
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var libraryListPane: some View {
        VStack(alignment: .leading, spacing: 10) {
            if store.isScanning && store.games.isEmpty {
                VStack(spacing: 12) {
                    ProgressView()
                    Text("Scanning game folders & disc images...")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .macGlassSurface(cornerRadius: 22)
            } else if filteredGames.isEmpty {
                emptyLibraryCard
            } else {
                ScrollView {
                    LazyVStack(spacing: 10) {
                        ForEach(filteredGames) { game in
                            gameRowCard(game)
                        }
                    }
                    .padding(4)
                }
            }
        }
    }

    private var emptyLibraryCard: some View {
        VStack(spacing: 14) {
            Image(systemName: "books.vertical.fill")
                .font(.system(size: 40))
                .foregroundStyle(KotonohaBrand.accent.opacity(0.85))
            Text("No Games Found")
                .font(.headline)
            Text("Add a game folder or ISO/BIN disc image using the buttons above.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 12)
            HStack(spacing: 10) {
                Button(action: store.addFolder) {
                    Label("Add Folder/Library", systemImage: "folder.badge.plus")
                }
                .macGlassButton(prominent: true)

                Button(action: store.addDisc) {
                    Label("Add ISO", systemImage: "opticaldisc")
                }
                .macGlassButton()
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .macGlassSurface(cornerRadius: 22)
    }

    private func gameRowCard(_ game: MacGameItem) -> some View {
        let isSelected = selectedGame?.id == game.id
        return HStack(spacing: 12) {
                ZStack {
                    RoundedRectangle(cornerRadius: KotonohaMetrics.cardRadius, style: .continuous)
                        .fill(isSelected ? KotonohaBrand.accent.opacity(0.24) : Color.white.opacity(0.06))
                        .frame(width: 44, height: 44)
                    Image(systemName: game.isDiscImage ? "opticaldisc.fill" : "folder.fill")
                        .font(.title3)
                        .foregroundStyle(isSelected ? KotonohaBrand.accent : KotonohaBrand.text.opacity(0.85))
                }

                VStack(alignment: .leading, spacing: 4) {
                    Text(game.title)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(1)

                    HStack(spacing: 6) {
                        Text(game.engineDisplayName)
                            .font(.caption2.weight(.medium))
                            .foregroundStyle(KotonohaBrand.accent)
                            .lineLimit(1)

                        if !game.overlayPath.isEmpty || !game.params.assetOverlayPath.isEmpty {
                            Text("OVERLAY")
                                .font(.system(size: 9, weight: .bold))
                                .padding(.horizontal, 5)
                                .padding(.vertical, 2)
                                .background(KotonohaBrand.accent.opacity(0.26), in: Capsule())
                        }
                        if game.params.hasAnyOverride {
                            Image(systemName: "slider.horizontal.3")
                                .font(.caption2)
                                .foregroundStyle(KotonohaBrand.accent)
                        }
                    }
                }
                Spacer(minLength: 4)
                if !game.linkedInProcess {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(KotonohaBrand.warning)
                        .font(.caption)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .macGlassSurface(cornerRadius: 18, interactive: true)
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(
                        isSelected ? KotonohaBrand.accent.opacity(0.85) : Color.clear,
                        lineWidth: 1.5
                    )
            )
        .contentShape(Rectangle())
        .onTapGesture { selectedGameId = game.id }
    }

    private var detailPane: some View {
        Group {
            if let game = selectedGame {
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        HStack(alignment: .top, spacing: 14) {
                            ZStack {
                                RoundedRectangle(cornerRadius: 18, style: .continuous)
                                    .fill(
                                        LinearGradient(
                                            colors: [
                                                KotonohaBrand.accent.opacity(0.30),
                                                KotonohaBrand.indigo.opacity(0.20)
                                            ],
                                            startPoint: .topLeading,
                                            endPoint: .bottomTrailing
                                        )
                                    )
                                    .frame(width: 64, height: 64)
                                Image(systemName: game.isDiscImage ? "opticaldisc.fill" : "gamecontroller.fill")
                                    .font(.system(size: 28))
                                    .foregroundStyle(.white)
                            }

                            VStack(alignment: .leading, spacing: 6) {
                                HStack(spacing: 8) {
                                    Text(game.title)
                                        .font(.title2.weight(.bold))
                                        .foregroundStyle(.primary)
                                    if store.isRunning(game.path) {
                                        badge(
                                            text: "(\(MacL10n.text(.running)))",
                                            tint: KotonohaBrand.roseDeep
                                        )
                                    }
                                }

                                HStack(spacing: 8) {
                                    badge(text: game.engineDisplayName, tint: KotonohaBrand.accent)
                                    badge(
                                        text: game.isDiscImage ? "Disc Image" : "Folder",
                                        tint: KotonohaBrand.accentSoft
                                    )
                                    if !game.overlayPath.isEmpty || !game.params.assetOverlayPath.isEmpty {
                                        badge(text: "Overlay Active", tint: KotonohaBrand.violet)
                                    }
                                    let activeBackend = store.effectiveRenderer(for: game.params)
                                    badge(
                                        text: "Backend: \(MacGraphicsOptions.rendererLabel(activeBackend))",
                                        tint: KotonohaBrand.surfaceRaised
                                    )
                                    let activeUpscaler = store.effectiveUpscalerBackend(for: game.params)
                                    if activeUpscaler != "none" {
                                        let activeFactor = store.effectiveUpscalerFactor(for: game.params)
                                        badge(
                                            text: "\(MacGraphicsOptions.upscalerLabel(activeUpscaler)) \(activeFactor)x",
                                            tint: KotonohaBrand.accentSoft
                                        )
                                    }
                                }
                            }
                            Spacer()
                        }

                        MacGlassContainer(spacing: 12) {
                            HStack(spacing: 12) {
                                Button {
                                    if store.isRunning(game.path) {
                                        store.terminateRunning(path: game.path)
                                    } else {
                                        store.resetLog()
                                        let pid = onPlay(game)
                                        if pid > 0 {
                                            store.markRunning(pid: pid, path: game.path)
                                        } else {
                                            store.statusMessage = MacL10n.text(.launchFailed)
                                        }
                                    }
                                } label: {
                                    let isRunning = store.isRunning(game.path)
                                    Label(
                                        isRunning ? MacL10n.text(.end) : MacL10n.text(.play),
                                        systemImage: isRunning ? "stop.fill" : "play.fill"
                                    )
                                    .font(.headline.weight(.semibold))
                                    .frame(minWidth: 130)
                                    .padding(.vertical, 6)
                                }
                                .macGlassButton(
                                    prominent: true,
                                    tone: store.isRunning(game.path)
                                        ? KotonohaBrand.roseDeep
                                        : KotonohaBrand.rose
                                )
                                .disabled(!game.linkedInProcess)

                                Button {
                                    showingParamsSheet = true
                                } label: {
                                    Label(
                                        game.params.hasAnyOverride ? "Parameters (Custom)" : "Parameters",
                                        systemImage: "slider.horizontal.3"
                                    )
                                    .font(.subheadline.weight(.semibold))
                                    .padding(.vertical, 6)
                                }
                                .macGlassButton()

                                Spacer()
                            }
                        }

                        MacGlassContainer(spacing: 8) {
                            VStack(alignment: .leading, spacing: 6) {
                                Label(MacL10n.text(.gameLog),
                                      systemImage: "terminal")
                                    .font(.caption.weight(.semibold))
                                    .foregroundStyle(KotonohaBrand.roseSoft)
                                ScrollView {
                                    VStack(alignment: .leading, spacing: 2) {
                                        if store.logLines.isEmpty {
                                            Text(MacL10n.text(.noLog))
                                                .font(.caption.monospaced())
                                                .foregroundStyle(.secondary)
                                        } else {
                                            ForEach(store.logLines, id: \.self) { line in
                                                Text(line)
                                                    .font(.caption2.monospaced())
                                                    .foregroundStyle(.primary)
                                                    .frame(maxWidth: .infinity,
                                                           alignment: .leading)
                                            }
                                        }
                                    }
                                }
                                .frame(height: 110)
                            }
                            .padding(12)
                        }

                        Divider().opacity(0.3)

                        VStack(alignment: .leading, spacing: 10) {
                            metadataRow(label: "Source Path", value: game.path)
                            metadataRow(label: "Detection Evidence", value: game.evidence)
                            metadataRow(
                                label: store.splitSavesByGame ? "Isolated Save Directory" : "Shared Save Directory",
                                value: store.resolvedSaveDirectory(for: game)
                            )
                            if !game.overlayPath.isEmpty && game.params.assetOverlayPath.isEmpty {
                                metadataRow(label: "Auto-Paired Overlay", value: game.overlayPath)
                            }
                            if game.params.hasAnyOverride {
                                activeParamsSummary(game.params)
                            }
                        }
                    }
                    .padding(22)
                }
                .macGlassSurface(cornerRadius: 24)
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "cursorarrow.click.2")
                        .font(.largeTitle)
                        .foregroundStyle(.secondary)
                    Text("Select a game to inspect or launch")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .macGlassSurface(cornerRadius: 24)
            }
        }
    }

    private func badge(text: String, tint: Color) -> some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(tint.opacity(0.24), in: Capsule())
            .foregroundStyle(.white)
    }

    private func metadataRow(label: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.caption.monospaced())
                .foregroundStyle(.primary.opacity(0.92))
                .textSelection(.enabled)
        }
    }

    private func activeParamsSummary(_ p: MacGameParams) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Active Optional Parameters")
                .font(.caption.weight(.semibold))
                .foregroundStyle(KotonohaBrand.accent)
            if !p.ktrfPath.isEmpty { Text("--ktrf: \(p.ktrfPath)").font(.caption2.monospaced()) }
            if !p.scriptsDir.isEmpty { Text("--scripts: \(p.scriptsDir)").font(.caption2.monospaced()) }
            if !p.assetOverlayPath.isEmpty { Text("--asset-overlay: \(p.assetOverlayPath)").font(.caption2.monospaced()) }
            if !p.translationPath.isEmpty { Text("--translation: \(p.translationPath)").font(.caption2.monospaced()) }
            if !p.subtitleStylePath.isEmpty { Text("--subtitle-style: \(p.subtitleStylePath)").font(.caption2.monospaced()) }
            if !p.customDocumentsDir.isEmpty { Text("--documents: \(p.customDocumentsDir)").font(.caption2.monospaced()) }
            if !p.startScenario.isEmpty { Text("--start: \(p.startScenario)").font(.caption2.monospaced()) }
            if !p.rendererBackend.isEmpty { Text("--renderer: \(p.rendererBackend)").font(.caption2.monospaced()) }
            if !p.upscalerBackend.isEmpty { Text("--upscaler: \(p.upscalerBackend)").font(.caption2.monospaced()) }
            if !p.upscalerFactor.isEmpty { Text("--upscaler-factor: \(p.upscalerFactor)").font(.caption2.monospaced()) }
            if !p.upscalerPreset.isEmpty { Text("--upscaler-preset: \(p.upscalerPreset)").font(.caption2.monospaced()) }
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            KotonohaBrand.accent.opacity(0.10),
            in: RoundedRectangle(
                cornerRadius: KotonohaMetrics.cardRadius,
                style: .continuous
            )
        )
    }
}

// MARK: - Per-Game Parameters Sheet

private struct MacGameParametersSheet: View {
    @ObservedObject var store: MacLauncherStore
    let game: MacGameItem
    @Environment(\.dismiss) private var dismiss

    @State private var params: MacGameParams
    @State private var routeValidation: (valid: Bool, message: String) = (true, "")

    init(store: MacLauncherStore, game: MacGameItem) {
        self.store = store
        self.game = game
        _params = State(initialValue: game.params)
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Parameters — \(game.title)")
                    .font(.headline)
                Spacer()
                Button("Reset") {
                    params = MacGameParams()
                    revalidateRoute()
                }
                Button("Cancel") { dismiss() }
                Button("Save") {
                    store.saveParams(for: game.saveProfileId, params: params)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
            .padding(16)

            Divider()

            Form {
                Section("Graphics & Upscaling (Override)") {
                    Picker("Renderer Backend", selection: $params.rendererBackend) {
                        Text("Use Global Setting").tag("")
                        ForEach(MacGraphicsOptions.renderers.filter { !$0.id.isEmpty }) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    Picker("Upscaler", selection: $params.upscalerBackend) {
                        Text("Use Global Setting").tag("")
                        ForEach(MacGraphicsOptions.upscalers) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    let effectiveUpscaler = params.upscalerBackend.isEmpty
                        ? store.upscalerBackend
                        : params.upscalerBackend
                    if effectiveUpscaler != "none" {
                        Picker("Scale Factor", selection: $params.upscalerFactor) {
                            Text("Use Global Setting").tag("")
                            ForEach(MacGraphicsOptions.scaleFactors, id: \.self) { f in
                                Text("\(f)x").tag(f)
                            }
                        }
                        let effectiveRenderer = params.rendererBackend.isEmpty
                            ? store.rendererBackend
                            : params.rendererBackend
                        if effectiveUpscaler == "mpv" ||
                           (effectiveUpscaler == "auto" && effectiveRenderer.hasPrefix("opengl")) {
                            Picker("Shader Preset", selection: $params.upscalerPreset) {
                                Text("Use Global Setting").tag("")
                                ForEach(store.upscalePresets) { preset in
                                    Text(preset.name).tag(String(preset.index))
                                }
                            }
                        }
                    }
                }

                if game.isSchoolDays {
                    Section("School Days HQ") {
                        pathRow(title: "Route File (--ktrf)", value: $params.ktrfPath, folder: false)
                        let defaultRoute = store.defaultSchoolDaysRoute
                        Text(defaultRoute.isEmpty
                             ? "Default: bundled school-days-hq.ktnroute"
                             : "Default: \(URL(fileURLWithPath: defaultRoute).lastPathComponent)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        if !routeValidation.message.isEmpty {
                            Text(routeValidation.message)
                                .font(.caption)
                                .foregroundStyle(
                                    routeValidation.valid
                                        ? KotonohaBrand.success
                                        : KotonohaBrand.warning
                                )
                        }
                        pathRow(title: "Scripts Folder (--scripts)", value: $params.scriptsDir, folder: true)
                    }
                }

                Section("Overlays & Localization") {
                    pathRow(title: "Asset Overlay (--asset-overlay)", value: $params.assetOverlayPath, folder: true)
                    pathRow(title: "Translation (--translation)", value: $params.translationPath, folder: false)
                    pathRow(title: "Subtitle Style (--subtitle-style)", value: $params.subtitleStylePath, folder: false)
                }

                Section("Saves & Startup") {
                    pathRow(title: "Custom Documents (--documents)", value: $params.customDocumentsDir, folder: true)
                    TextField("Start Scenario (--start)", text: $params.startScenario)
                    Toggle("Isolate Saves Per Game", isOn: $store.splitSavesByGame)
                }
            }
            .kotonohaGroupedFormStyle()
        }
        .frame(minWidth: 540, minHeight: 520)
        .onAppear { revalidateRoute() }
        .onChange(of: params.ktrfPath) { revalidateRoute() }
    }

    private func revalidateRoute() {
        guard game.isSchoolDays else { return }
        routeValidation = store.validateRoute(params.ktrfPath)
    }

    private func pathRow(title: String, value: Binding<String>, folder: Bool) -> some View {
        HStack {
            TextField(title, text: value)
            if !value.wrappedValue.isEmpty {
                Button("Clear") { value.wrappedValue = "" }
            }
            Button("Browse...") {
                if let picked = folder ? store.browseFolder() : store.browseFile() {
                    value.wrappedValue = picked
                }
            }
        }
    }
}

// MARK: - Global Settings & Sources Sheet

private struct MacSettingsSheet: View {
    @ObservedObject var store: MacLauncherStore
    @Environment(\.dismiss) private var dismiss
    @State private var manualLibraryPath: String = ""
    @State private var detectionMessage: String = ""

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Settings & Library Sources")
                    .font(.headline)
                Spacer()
                Button("Done") { dismiss() }
                    .keyboardShortcut(.defaultAction)
            }
            .padding(16)

            Divider()

            Form {
                Section("Graphics & Upscaling") {
                    Picker("Renderer Backend", selection: $store.rendererBackend) {
                        ForEach(MacGraphicsOptions.renderers) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    Picker("Upscaler", selection: $store.upscalerBackend) {
                        ForEach(MacGraphicsOptions.upscalers) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    if store.upscalerBackend != "none" {
                        Picker("Scale Factor", selection: $store.upscalerFactor) {
                            ForEach(MacGraphicsOptions.scaleFactors, id: \.self) { f in
                                Text("\(f)x").tag(f)
                            }
                        }
                        if store.upscalerBackend == "mpv" ||
                           (store.upscalerBackend == "auto" && store.rendererBackend.hasPrefix("opengl")) {
                            Picker("Shader Preset", selection: $store.upscalerPreset) {
                                ForEach(store.upscalePresets) { preset in
                                    Text(preset.name).tag(String(preset.index))
                                }
                            }
                        }
                    }
                    Text("Auto uses MetalFX on Metal, Anime4K/ArtCNN on OpenGL, and Linear elsewhere.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("Save Isolation") {
                    Toggle("Isolate Saves Per Game Title", isOn: $store.splitSavesByGame)
                    HStack(spacing: KotonohaMetrics.sm) {
                        TextField("Custom save root (optional)", text: $store.customSaveRoot)
                            .textFieldStyle(.roundedBorder)
                        Button("Choose…") {
                            if let picked = store.browseFolder() {
                                store.customSaveRoot = picked
                            }
                        }
                    }
                    Text("Empty uses ~/Library/Application Support/Kotonoha/<app-id>.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("Library Sources") {
                    HStack(spacing: 8) {
                        TextField("/Volumes/Games or ~/Games", text: $manualLibraryPath)
                            .textFieldStyle(.roundedBorder)
                            .onSubmit {
                                addManualLibraryPath()
                            }
                        Button("Add Path") {
                            addManualLibraryPath()
                        }
                        .disabled(manualLibraryPath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }

                    HStack(spacing: KotonohaMetrics.sm) {
                        Button("Choose Folder…") {
                            store.addFolder()
                        }
                        Button("Choose Disc Image…") {
                            store.addDisc()
                        }
                    }

                    HStack(spacing: KotonohaMetrics.sm) {
                        Button("Detect Folder…") {
                            if let picked = store.browseFolder() {
                                detectionMessage = store.detectPath(picked)
                            }
                        }
                        Button("Detect Disc/File…") {
                            if let picked = store.browseFile() {
                                detectionMessage = store.detectPath(picked)
                            }
                        }
                    }
                    if !detectionMessage.isEmpty {
                        Text(detectionMessage)
                            .font(.caption)
                            .foregroundStyle(KotonohaBrand.accent)
                            .textSelection(.enabled)
                    }

                    if store.roots.isEmpty {
                        Text("No folders or disc images added yet.")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(store.roots, id: \.self) { root in
                            HStack {
                                Text(root)
                                    .font(.caption.monospaced())
                                    .lineLimit(1)
                                Spacer()
                                Button("Remove") {
                                    store.removeRoot(root)
                                }
                                .buttonStyle(.borderless)
                                .foregroundStyle(KotonohaBrand.error)
                            }
                        }
                    }
                }
            }
            .kotonohaGroupedFormStyle()
        }
        .frame(minWidth: 520, minHeight: 500)
    }

    private func addManualLibraryPath() {
        let trimmed = manualLibraryPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        let expanded = (trimmed as NSString).expandingTildeInPath
        store.addRoots([expanded])
        manualLibraryPath = ""
    }
}

private extension View {
    @ViewBuilder
    func kotonohaGroupedFormStyle() -> some View {
        if #available(macOS 13.0, *) {
            self.formStyle(.grouped)
        } else {
            self.padding()
        }
    }
}

// MARK: - Objective-C++ Bridge Presenter

@objc(KotonohaMacLauncherPresenter)
@MainActor
final class KotonohaMacLauncherPresenter: NSObject, NSWindowDelegate {
    private static var window: NSWindow?
    private static var delegate: KotonohaMacLauncherPresenter?
    private static var quit: (() -> Void)?

    @objc(presentWithOnLaunch:onQuit:)
    static func present(
        onLaunch: @escaping (
            String, String, String, Bool, String,
            String, String, String, String, String, String, String,
            String, String, String, String
        ) -> Int,
        onQuit: @escaping () -> Void
    ) {
        dismiss()
        quit = onQuit
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 980, height: 640),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Kotonoha"
        window.center()
        let delegate = KotonohaMacLauncherPresenter()
        window.delegate = delegate
        let store = MacLauncherStore.shared
        window.contentView = NSHostingView(
            rootView: MacLauncherView(
                store: store,
                onPlay: { item in
                    Int32(onLaunch(
                        item.path,
                        item.engineSlug,
                        item.saveProfileId,
                        store.splitSavesByGame,
                        store.customSaveRoot,
                        item.params.assetOverlayPath.isEmpty ? item.overlayPath : item.params.assetOverlayPath,
                        item.params.translationPath,
                        item.params.subtitleStylePath,
                        item.params.customDocumentsDir,
                        item.params.ktrfPath,
                        item.params.scriptsDir,
                        item.params.startScenario,
                        store.effectiveRenderer(for: item.params),
                        store.effectiveUpscalerBackend(for: item.params),
                        store.effectiveUpscalerPreset(for: item.params),
                        store.effectiveUpscalerFactor(for: item.params)
                    ))
                },
                onQuit: onQuit
            )
        )
        self.window = window
        self.delegate = delegate
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc static func dismiss() {
        window?.delegate = nil
        window?.close()
        window = nil
        delegate = nil
        quit = nil
    }

    func windowWillClose(_ notification: Notification) {
        Self.quit?()
    }
}
