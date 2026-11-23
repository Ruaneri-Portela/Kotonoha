// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
import SwiftUI
import UIKit

// Cartilha visual Kotonoha v2. Native Liquid Glass stays system-owned.
private enum KotonohaBrand {
    static let navy = Color(red: 13.0/255, green: 20.0/255, blue: 46.0/255)
    static let raised = Color(red: 24.0/255, green: 36.0/255, blue: 73.0/255)
    static let indigo = Color(red: 44.0/255, green: 62.0/255, blue: 154.0/255)
    static let violet = Color(red: 106.0/255, green: 91.0/255, blue: 231.0/255)
    static let magenta = Color(red: 200.0/255, green: 107.0/255, blue: 217.0/255)
    static let cyan = Color(red: 0.0, green: 214.0/255, blue: 1.0)
    static let sakura = Color(red: 1.0, green: 143.0/255, blue: 189.0/255)
    static let outline = Color(red: 57.0/255, green: 49.0/255, blue: 94.0/255)
    static let muted = Color(red: 185.0/255, green: 181.0/255, blue: 212.0/255)
}

import UniformTypeIdentifiers

// MARK: - Liquid Glass Helpers (iOS 26+ with iOS 15–18 .ultraThinMaterial fallback)

private struct KotonohaGlassSurfaceModifier: ViewModifier {
    let cornerRadius: CGFloat
    let interactive: Bool

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
        if #available(iOS 26.0, *) {
            if interactive {
                content.glassEffect(.regular.interactive(), in: shape)
            } else {
                content.glassEffect(.regular, in: shape)
            }
        } else {
            content
                .background(.ultraThinMaterial, in: shape)
                .overlay(
                    shape.strokeBorder(KotonohaBrand.magenta.opacity(0.42), lineWidth: 1)
                )
        }
    }
}

private struct KotonohaGlassButtonModifier: ViewModifier {
    let prominent: Bool

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            if prominent {
                content.buttonStyle(.glassProminent)
            } else {
                content.buttonStyle(.glass)
            }
        } else {
            if prominent {
                content.buttonStyle(.borderedProminent)
            } else {
                content.buttonStyle(.bordered)
            }
        }
    }
}

private struct KotonohaGlassContainer<Content: View>: View {
    let spacing: CGFloat
    @ViewBuilder let content: () -> Content

    init(spacing: CGFloat = 14, @ViewBuilder content: @escaping () -> Content) {
        self.spacing = spacing
        self.content = content
    }

    var body: some View {
        if #available(iOS 26.0, *) {
            GlassEffectContainer(spacing: spacing) {
                content()
            }
        } else {
            content()
        }
    }
}

private extension View {
    func kotonohaGlassSurface(cornerRadius: CGFloat = 20, interactive: Bool = false) -> some View {
        modifier(KotonohaGlassSurfaceModifier(cornerRadius: cornerRadius, interactive: interactive))
    }

    func kotonohaGlassButton(prominent: Bool = false) -> some View {
        modifier(KotonohaGlassButtonModifier(prominent: prominent))
    }
}

// MARK: - Ambient Backdrop

private struct KotonohaAmbientBackdrop: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    KotonohaBrand.navy,
                    KotonohaBrand.raised,
                    KotonohaBrand.indigo.opacity(0.45)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            RadialGradient(
                colors: [KotonohaBrand.cyan.opacity(0.20), .clear],
                center: .topLeading,
                startRadius: 20,
                endRadius: 520
            )
            RadialGradient(
                colors: [KotonohaBrand.magenta.opacity(0.22), .clear],
                center: .bottomTrailing,
                startRadius: 30,
                endRadius: 540
            )
        }
        .overlay(alignment: .bottomTrailing) {
            Text("✿")
                .font(.system(size: 138, weight: .ultraLight))
                .foregroundStyle(KotonohaBrand.sakura.opacity(0.07))
                .padding(28)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
        }
        .ignoresSafeArea()
    }
}

// MARK: - Main Launcher View

struct KotonohaLauncherView: View {
    @ObservedObject var store: KotonohaIOSBookmarkStore
    let onPlay: (KotonohaGameItem) -> Void

    @State private var searchText: String = ""
    @State private var selectedGameId: String?
    @State private var showingParamsSheet: Bool = false
    @State private var showingRootsSheet: Bool = false

    private var filteredGames: [KotonohaGameItem] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty else { return store.games }
        return store.games.filter { item in
            item.title.localizedCaseInsensitiveContains(query) ||
            item.engineDisplayName.localizedCaseInsensitiveContains(query) ||
            item.engineSlug.localizedCaseInsensitiveContains(query)
        }
    }

    private var selectedGame: KotonohaGameItem? {
        if let id = selectedGameId,
           let match = store.games.first(where: { $0.id == id }) {
            return match
        }
        return filteredGames.first
    }

    var body: some View {
        ZStack {
            KotonohaAmbientBackdrop()

            VStack(spacing: 14) {
                headerBar
                contentSplit
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 14)
        }
        .tint(KotonohaBrand.violet)
        .preferredColorScheme(.dark)
        .onAppear {
            store.rescan()
        }
        .onChange(of: store.games) { newGames in
            if selectedGameId == nil || !newGames.contains(where: { $0.id == selectedGameId }) {
                selectedGameId = newGames.first?.id
            }
        }
        .sheet(isPresented: $showingParamsSheet) {
            if let game = selectedGame {
                KotonohaGameParametersSheet(store: store, game: game)
            }
        }
        .sheet(isPresented: $showingRootsSheet) {
            KotonohaLibraryRootsSheet(store: store)
        }
    }

    // MARK: Header Toolbar

    private var headerBar: some View {
        KotonohaGlassContainer(spacing: 10) {
            HStack(spacing: 12) {
                HStack(spacing: 10) {
                    Image(systemName: "sparkles.rectangle.stack.fill")
                        .font(.title2.weight(.semibold))
                        .foregroundStyle(.cyan)
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
                .kotonohaGlassSurface(cornerRadius: 18)

                HStack(spacing: 8) {
                    Image(systemName: "magnifyingglass")
                        .foregroundStyle(.secondary)
                    TextField("Search games or engines...", text: $searchText)
                        .textFieldStyle(.plain)
                        .disableAutocorrection(true)
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
                .kotonohaGlassSurface(cornerRadius: 18)

                Button {
                    store.presentPicker(contentTypes: [.folder], allowsMultiple: false, bookmarkInLibrary: true) { _ in }
                } label: {
                    Label("Add Folder", systemImage: "folder.badge.plus")
                        .font(.subheadline.weight(.semibold))
                }
                .kotonohaGlassButton()

                Button {
                    store.presentPicker(contentTypes: KotonohaContentTypes.discImageTypes, allowsMultiple: true, bookmarkInLibrary: true) { _ in }
                } label: {
                    Label("Add ISO/BIN", systemImage: "opticaldisc.fill")
                        .font(.subheadline.weight(.semibold))
                }
                .kotonohaGlassButton()

                Button {
                    store.presentPicker(contentTypes: KotonohaContentTypes.zipArchives, allowsMultiple: false, bookmarkInLibrary: false) { url in
                        if let url { store.importZip(url) }
                    }
                } label: {
                    Label("Import ZIP", systemImage: "square.and.arrow.down")
                        .font(.subheadline.weight(.semibold))
                }
                .kotonohaGlassButton()

                Button {
                    store.rescan()
                } label: {
                    Image(systemName: store.isScanning ? "arrow.triangle.2.circlepath.circle.fill" : "arrow.clockwise")
                        .font(.subheadline.weight(.semibold))
                }
                .kotonohaGlassButton()
                .disabled(store.isScanning)
                .accessibilityLabel("Rescan Library")

                Button {
                    showingRootsSheet = true
                } label: {
                    Image(systemName: "slider.horizontal.3")
                        .font(.subheadline.weight(.semibold))
                }
                .kotonohaGlassButton()
                .accessibilityLabel("Library Sources & Settings")
            }
        }
    }

    // MARK: Split Content

    private var contentSplit: some View {
        HStack(spacing: 16) {
            libraryListPane
                .frame(minWidth: 280, idealWidth: 340, maxWidth: 420)
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
                .kotonohaGlassSurface(cornerRadius: 22)
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
                .foregroundStyle(.cyan.opacity(0.85))
            Text("No Games Found")
                .font(.headline)
            Text("Copy games into On My iPhone/iPad \u{2192} Kotonoha \u{2192} games, or add a folder, disc image, or ZIP archive above.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 12)
            HStack(spacing: 10) {
                Button {
                    store.presentPicker(contentTypes: [.folder], allowsMultiple: false, bookmarkInLibrary: true) { _ in }
                } label: {
                    Label("Add Folder", systemImage: "folder.badge.plus")
                }
                .kotonohaGlassButton(prominent: true)

                Button {
                    store.presentPicker(contentTypes: KotonohaContentTypes.discImageTypes, allowsMultiple: true, bookmarkInLibrary: true) { _ in }
                } label: {
                    Label("Add ISO", systemImage: "opticaldisc")
                }
                .kotonohaGlassButton()

                Button {
                    store.presentPicker(contentTypes: KotonohaContentTypes.zipArchives, allowsMultiple: false, bookmarkInLibrary: false) { url in
                        if let url { store.importZip(url) }
                    }
                } label: {
                    Label("Import ZIP", systemImage: "square.and.arrow.down")
                }
                .kotonohaGlassButton()
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .kotonohaGlassSurface(cornerRadius: 22)
    }

    private func gameRowCard(_ game: KotonohaGameItem) -> some View {
        let isSelected = selectedGame?.id == game.id
        return Button {
            selectedGameId = game.id
        } label: {
            HStack(spacing: 12) {
                ZStack {
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .fill(isSelected ? Color.cyan.opacity(0.28) : Color.white.opacity(0.08))
                        .frame(width: 44, height: 44)
                    Image(systemName: game.isDiscImage ? "opticaldisc.fill" : "folder.fill")
                        .font(.title3)
                        .foregroundStyle(isSelected ? .cyan : .white.opacity(0.85))
                }

                VStack(alignment: .leading, spacing: 4) {
                    Text(game.title)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(1)

                    HStack(spacing: 6) {
                        Text(game.engineDisplayName)
                            .font(.caption2.weight(.medium))
                            .foregroundStyle(.cyan)
                            .lineLimit(1)

                        if !game.overlayPath.isEmpty || !game.params.assetOverlayPath.isEmpty {
                            Text("OVERLAY")
                                .font(.system(size: 9, weight: .bold))
                                .padding(.horizontal, 5)
                                .padding(.vertical, 2)
                                .background(KotonohaBrand.violet.opacity(0.30), in: Capsule())
                        }
                        if game.params.hasAnyOverride {
                            Image(systemName: "slider.horizontal.3")
                                .font(.caption2)
                                .foregroundStyle(.orange)
                        }
                    }
                }
                Spacer(minLength: 4)
                if !game.linkedInProcess {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.yellow)
                        .font(.caption)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .kotonohaGlassSurface(cornerRadius: 18, interactive: true)
            .overlay(
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(isSelected ? Color.cyan.opacity(0.85) : Color.clear, lineWidth: 1.5)
            )
        }
        .buttonStyle(.plain)
    }

    // MARK: Detail Pane

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
                                            colors: [.cyan.opacity(0.35), .blue.opacity(0.25)],
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
                                Text(game.title)
                                    .font(.title2.weight(.bold))
                                    .foregroundStyle(.primary)

                                HStack(spacing: 8) {
                                    badge(text: game.engineDisplayName, tint: .cyan)
                                    badge(text: game.isDiscImage ? "Disc Image" : "Folder", tint: .indigo)
                                    if !game.overlayPath.isEmpty || !game.params.assetOverlayPath.isEmpty {
                                        badge(text: "Overlay Active", tint: .purple)
                                    }
                                    let activeBackend = store.effectiveRenderer(for: game.params)
                                    badge(
                                        text: "Backend: \(KotonohaGraphicsLabels.rendererLabel(activeBackend))",
                                        tint: .teal
                                    )
                                    let activeUpscaler = store.effectiveUpscalerBackend(for: game.params)
                                    if activeUpscaler != "none" {
                                        let activeFactor = store.effectiveUpscalerFactor(for: game.params)
                                        badge(
                                            text: "\(KotonohaGraphicsLabels.upscalerLabel(activeUpscaler)) \(activeFactor)x",
                                            tint: .mint
                                        )
                                    }
                                }
                            }
                            Spacer()
                        }

                        KotonohaGlassContainer(spacing: 12) {
                            HStack(spacing: 12) {
                                Button {
                                    onPlay(game)
                                } label: {
                                    Label("Play", systemImage: "play.fill")
                                        .font(.headline.weight(.semibold))
                                        .frame(minWidth: 130)
                                        .padding(.vertical, 6)
                                }
                                .kotonohaGlassButton(prominent: true)
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
                                .kotonohaGlassButton()

                                Spacer()
                            }
                        }

                        Divider().opacity(0.3)

                        VStack(alignment: .leading, spacing: 10) {
                            metadataRow(label: "Source Path", value: game.path)
                            metadataRow(label: "Detection Evidence", value: game.evidence)
                            metadataRow(
                                label: store.splitSavesByGame ? "Isolated Save Directory" : "Shared Save Directory",
                                value: game.saveDirectory
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
                .kotonohaGlassSurface(cornerRadius: 24)
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "hand.tap.fill")
                        .font(.largeTitle)
                        .foregroundStyle(.secondary)
                    Text("Select a game to inspect or launch")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .kotonohaGlassSurface(cornerRadius: 24)
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

    private func activeParamsSummary(_ p: KotonohaGameParams) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Active Optional Parameters")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.orange)
            if !p.ktrfPath.isEmpty {
                Text("--ktrf: \(p.ktrfPath)").font(.caption2.monospaced())
            }
            if !p.scriptsDir.isEmpty {
                Text("--scripts: \(p.scriptsDir)").font(.caption2.monospaced())
            }
            if !p.assetOverlayPath.isEmpty {
                Text("--asset-overlay: \(p.assetOverlayPath)").font(.caption2.monospaced())
            }
            if !p.translationPath.isEmpty {
                Text("--translation: \(p.translationPath)").font(.caption2.monospaced())
            }
            if !p.subtitleStylePath.isEmpty {
                Text("--subtitle-style: \(p.subtitleStylePath)").font(.caption2.monospaced())
            }
            if !p.customDocumentsDir.isEmpty {
                Text("--documents: \(p.customDocumentsDir)").font(.caption2.monospaced())
            }
            if !p.startScenario.isEmpty {
                Text("--start: \(p.startScenario)").font(.caption2.monospaced())
            }
            if !p.rendererBackend.isEmpty {
                Text("--renderer: \(p.rendererBackend)").font(.caption2.monospaced())
            }
            if !p.upscalerBackend.isEmpty {
                Text("--upscaler: \(p.upscalerBackend)").font(.caption2.monospaced())
            }
            if !p.upscalerFactor.isEmpty {
                Text("--upscaler-factor: \(p.upscalerFactor)").font(.caption2.monospaced())
            }
            if !p.upscalerPreset.isEmpty {
                Text("--upscaler-preset: \(p.upscalerPreset)").font(.caption2.monospaced())
            }
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.orange.opacity(0.12), in: RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - Optional Per-Game Parameters Sheet

private struct KotonohaGameParametersSheet: View {
    @ObservedObject var store: KotonohaIOSBookmarkStore
    let game: KotonohaGameItem
    @Environment(\.dismiss) private var dismiss

    @State private var params: KotonohaGameParams
    @State private var routeValidation: (valid: Bool, message: String) = (true, "")

    init(store: KotonohaIOSBookmarkStore, game: KotonohaGameItem) {
        self.store = store
        self.game = game
        _params = State(initialValue: game.params)
    }

    var body: some View {
        NavigationView {
            Form {
                if game.isSchoolDays {
                    Section {
                        pathPickerRow(
                            title: "Route File (--ktrf)",
                            value: $params.ktrfPath,
                            placeholder: defaultRoutePlaceholder,
                            contentTypes: KotonohaContentTypes.routeFileTypes
                        )
                        if !routeValidation.message.isEmpty {
                            HStack(spacing: 6) {
                                Image(systemName: routeValidation.valid ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                                    .foregroundStyle(routeValidation.valid ? .green : .orange)
                                Text(routeValidation.message)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        pathPickerRow(
                            title: "Scripts Folder (--scripts)",
                            value: $params.scriptsDir,
                            placeholder: "Optional translated ORS scripts directory",
                            contentTypes: [.folder]
                        )
                    } header: {
                        Text("School Days HQ")
                    }
                }

                Section {
                    pathPickerRow(
                        title: "Asset Overlay (--asset-overlay)",
                        value: $params.assetOverlayPath,
                        placeholder: game.overlayPath.isEmpty ? "Optional patch/overlay folder" : "Auto: \(URL(fileURLWithPath: game.overlayPath).lastPathComponent)",
                        contentTypes: [.folder]
                    )
                    pathPickerRow(
                        title: "Translation (--translation)",
                        value: $params.translationPath,
                        placeholder: "Optional .ktr / .json dictionary",
                        contentTypes: KotonohaContentTypes.translationFileTypes
                    )
                    pathPickerRow(
                        title: "Subtitle Style (--subtitle-style)",
                        value: $params.subtitleStylePath,
                        placeholder: "Optional .ass subtitle style",
                        contentTypes: KotonohaContentTypes.subtitleStyleTypes
                    )
                } header: {
                    Text("Overlays & Localization")
                }

                Section {
                    pathPickerRow(
                        title: "Custom Documents (--documents)",
                        value: $params.customDocumentsDir,
                        placeholder: "Default isolated save folder",
                        contentTypes: [.folder]
                    )
                    HStack {
                        Text("Start Scenario (--start)")
                        Spacer()
                        TextField("Optional scenario / node ID", text: $params.startScenario)
                            .multilineTextAlignment(.trailing)
                            .disableAutocorrection(true)
                            .textInputAutocapitalization(.never)
                    }
                    Toggle("Isolate Saves Per Game", isOn: $store.splitSavesByGame)
                } header: {
                    Text("Saves & Startup")
                }

                Section {
                    Picker("Renderer Backend", selection: $params.rendererBackend) {
                        Text("Use Global Setting").tag("")
                        ForEach(KotonohaGraphicsLabels.iosRenderers.filter { !$0.id.isEmpty }, id: \.id) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    Picker("Upscaler", selection: $params.upscalerBackend) {
                        Text("Use Global Setting").tag("")
                        ForEach(KotonohaGraphicsLabels.iosUpscalers, id: \.id) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    let effectiveUpscaler = params.upscalerBackend.isEmpty
                        ? store.upscalerBackend
                        : params.upscalerBackend
                    if effectiveUpscaler != "none" {
                        Picker("Scale Factor", selection: $params.upscalerFactor) {
                            Text("Use Global Setting").tag("")
                            ForEach(KotonohaGraphicsLabels.scaleFactors, id: \.self) { f in
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
                } header: {
                    Text("Graphics & Upscaling")
                }

                Section {
                    Button(role: .destructive) {
                        params = KotonohaGameParams()
                        revalidateRoute()
                    } label: {
                        Label("Reset All Optional Parameters", systemImage: "arrow.counterclockwise")
                    }
                }
            }
            .navigationTitle("Parameters — \(game.title)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        store.saveParams(for: game.saveProfileId, params: params)
                        dismiss()
                    }
                    .font(.headline)
                }
            }
            .onAppear {
                revalidateRoute()
            }
            .onChange(of: params.ktrfPath) { _ in
                revalidateRoute()
            }
        }
    }

    private var defaultRoutePlaceholder: String {
        let def = store.defaultSchoolDaysRoute
        if def.isEmpty {
            return "Bundled school-days-hq.ktnroute"
        }
        return "Default: \(URL(fileURLWithPath: def).lastPathComponent)"
    }

    private func revalidateRoute() {
        guard game.isSchoolDays else { return }
        routeValidation = store.validateRoute(params.ktrfPath)
    }

    private func pathPickerRow(
        title: String,
        value: Binding<String>,
        placeholder: String,
        contentTypes: [UTType]
    ) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title)
                    .font(.subheadline.weight(.medium))
                Spacer()
                if !value.wrappedValue.isEmpty {
                    Button("Clear") {
                        value.wrappedValue = ""
                    }
                    .font(.caption)
                    .foregroundStyle(.red)
                }
                Button("Browse") {
                    store.presentPicker(
                        contentTypes: contentTypes,
                        allowsMultiple: false,
                        bookmarkInLibrary: false
                    ) { url in
                        if let url {
                            value.wrappedValue = url.standardizedFileURL.path
                        }
                    }
                }
                .font(.caption.weight(.semibold))
            }
            Text(value.wrappedValue.isEmpty ? placeholder : value.wrappedValue)
                .font(.caption.monospaced())
                .foregroundStyle(value.wrappedValue.isEmpty ? .secondary : .primary)
                .lineLimit(2)
        }
        .padding(.vertical, 2)
    }
}

// MARK: - Graphics & Upscaler Option Labels

private enum KotonohaGraphicsLabels {
    struct Option: Identifiable {
        let id: String
        let label: String
    }

    static let iosRenderers: [Option] = [
        Option(id: "", label: "Automatic"),
        Option(id: "metal", label: "Metal"),
        Option(id: "opengles2", label: "OpenGL ES"),
        Option(id: "software", label: "Software")
    ]

    static let iosUpscalers: [Option] = [
        Option(id: "none", label: "Off"),
        Option(id: "auto", label: "Auto (MetalFX / OpenGL)"),
        Option(id: "metalfx", label: "MetalFX"),
        Option(id: "mpv", label: "MPV / Anime4K / ArtCNN (OpenGL ES)"),
        Option(id: "linear", label: "Linear")
    ]

    static let scaleFactors: [String] = ["1.5", "2.0", "3.0", "4.0"]

    static func rendererLabel(_ value: String) -> String {
        switch value {
        case "": return "Auto"
        case "metal": return "Metal"
        case "opengles2": return "OpenGL ES"
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

// MARK: - Library Roots & Global Settings Sheet

private struct KotonohaLibraryRootsSheet: View {
    @ObservedObject var store: KotonohaIOSBookmarkStore
    @Environment(\.dismiss) private var dismiss
    @State private var manualLibraryPath: String = ""

    var body: some View {
        NavigationView {
            List {
                Section {
                    Picker("Renderer Backend", selection: $store.rendererBackend) {
                        ForEach(KotonohaGraphicsLabels.iosRenderers, id: \.id) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    Picker("Upscaler", selection: $store.upscalerBackend) {
                        ForEach(KotonohaGraphicsLabels.iosUpscalers, id: \.id) { opt in
                            Text(opt.label).tag(opt.id)
                        }
                    }
                    if store.upscalerBackend != "none" {
                        Picker("Scale Factor", selection: $store.upscalerFactor) {
                            ForEach(KotonohaGraphicsLabels.scaleFactors, id: \.self) { f in
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
                } header: {
                    Text("Graphics & Upscaling")
                } footer: {
                    Text("Auto uses MetalFX on Metal, Anime4K/ArtCNN on OpenGL ES, and Linear elsewhere. MPV automatically selects OpenGL ES when Renderer is Automatic.")
                }

                Section("Save Isolation") {
                    Toggle("Isolate Saves Per Game Title", isOn: $store.splitSavesByGame)
                }

                Section("Built-In Sandbox Folders (Files.app)") {
                    Label(store.documentsGamesPath, systemImage: "folder.fill")
                        .font(.caption.monospaced())
                    Label(store.documentsDirectoryPath, systemImage: "doc.on.doc.fill")
                        .font(.caption.monospaced())
                }

                Section("External Bookmarked Folders & Disc Images") {
                    HStack(spacing: 8) {
                        TextField("Paste folder or disc path", text: $manualLibraryPath)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled(true)
                            .font(.caption.monospaced())
                        Button("Add") {
                            let trimmed = manualLibraryPath.trimmingCharacters(in: .whitespacesAndNewlines)
                            if !trimmed.isEmpty {
                                store.handleExternalPathOrURL(trimmed)
                                manualLibraryPath = ""
                            }
                        }
                        .disabled(manualLibraryPath.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }

                    Button {
                        store.presentPicker(contentTypes: [.folder], allowsMultiple: true) { _ in }
                    } label: {
                        Label("Add Folder from Files…", systemImage: "folder.badge.plus")
                    }

                    Button {
                        store.presentPicker(contentTypes: KotonohaContentTypes.discImageTypes, allowsMultiple: true) { _ in }
                    } label: {
                        Label("Add Disc Image (ISO / BIN)…", systemImage: "opticaldisc")
                    }

                    if store.bookmarkedPaths.isEmpty {
                        Text("No external bookmarks added yet.")
                            .foregroundStyle(.secondary)
                    } else {
                        ForEach(store.bookmarkedPaths, id: \.self) { path in
                            Text(path)
                                .font(.caption.monospaced())
                        }
                        .onDelete { offsets in
                            for index in offsets {
                                store.removeBookmarkedPath(store.bookmarkedPaths[index])
                            }
                        }
                    }
                }
            }
            .navigationTitle("Settings & Sources")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

// MARK: - Objective-C++ Bridge Presenter

@objc(KotonohaIOSLauncherPresenter)
@MainActor
final class KotonohaIOSLauncherPresenter: NSObject {
    private static var launcherWindow: UIWindow?
    private static weak var previousKeyWindow: UIWindow?

    @objc(presentWithOnLaunch:) static func present(
        onLaunch: @escaping (
            String, String, String, Bool, String,
            String, String, String, String, String, String, String,
            String, String, String, String
        ) -> Void
    ) {
        if launcherWindow != nil {
            KotonohaIOSBookmarkStore.shared.rescan()
            return
        }
        guard let scene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene }).first else {
            NSLog("[KotonohaLauncher] Unable to find UIWindowScene for native launcher")
            return
        }
        previousKeyWindow = scene.windows.first(where: { $0.isKeyWindow })
        let store = KotonohaIOSBookmarkStore.shared
        let rootView = KotonohaLauncherView(store: store) { item in
            onLaunch(
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
            )
        }
        let host = UIHostingController(rootView: rootView)
        let window = UIWindow(windowScene: scene)
        window.windowLevel = .normal + 1
        window.rootViewController = host
        launcherWindow = window
        window.makeKeyAndVisible()
    }

    @objc(dismiss) static func dismiss() {
        guard let window = launcherWindow else { return }
        launcherWindow = nil
        window.isHidden = true
        window.rootViewController = nil
        previousKeyWindow?.makeKeyAndVisible()
        previousKeyWindow = nil
    }

    @objc(notifyOpenedURL:) static func notifyOpenedURL(_ pathOrUrl: String) {
        KotonohaIOSBookmarkStore.shared.handleExternalPathOrURL(pathOrUrl)
    }
}
