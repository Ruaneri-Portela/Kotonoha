// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
import AppKit
import Foundation
import SwiftUI
import UniformTypeIdentifiers

private struct MacGame: Decodable, Identifiable {
    let title: String
    let path: String
    let engineDisplayName: String
    let linkedInProcess: Bool
    var id: String { path }
}

@MainActor
private final class MacLibrary: ObservableObject {
    @Published var games: [MacGame] = []
    @Published var scanning = false
    @Published var status = ""

    private let rootsKey = "KotonohaMacLibraryRoots"
    var roots: [String] {
        get { UserDefaults.standard.stringArray(forKey: rootsKey) ?? [] }
        set { UserDefaults.standard.set(newValue, forKey: rootsKey) }
    }

    func scan() {
        let paths = roots
        scanning = true
        status = "Scanning library..."
        Task.detached(priority: .userInitiated) {
            let discovered = Self.scanOffMain(paths)
            await MainActor.run {
                self.games = discovered
                self.scanning = false
                self.status = discovered.isEmpty ? "Add a game folder or disc image." :
                    "\(discovered.count) game(s) ready"
            }
        }
    }

    nonisolated private static func scanOffMain(_ paths: [String]) -> [MacGame] {
        let cPaths = paths.map { strdup($0) }
        defer { cPaths.forEach { free($0) } }
        let pointers: [UnsafePointer<CChar>?] = cPaths.map { UnsafePointer($0) }
        let result = pointers.withUnsafeBufferPointer { buffer in
            KotonohaLauncherScanRootsJson(buffer.baseAddress,
                                          Int32(buffer.count), 1, "")
        }
        guard let result else { return [] }
        defer { KotonohaLauncherFreeString(result) }
        let bytes = Data(String(cString: result).utf8)
        return (try? JSONDecoder().decode([MacGame].self, from: bytes)) ?? []
    }

    func addFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK { add(panel.urls.map(\.path)) }
    }

    func addDisc() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.diskImage, .data]
        if panel.runModal() == .OK { add(panel.urls.map(\.path)) }
    }

    private func add(_ paths: [String]) {
        roots = Array(Set(roots + paths)).sorted()
        scan()
    }
}

private struct MacLauncherView: View {
    @StateObject private var library = MacLibrary()
    let onLaunch: (String) -> Void
    let onQuit: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("Kotonoha").font(.largeTitle.bold())
                Spacer()
                Button("Add Folder", action: library.addFolder)
                Button("Add ISO/BIN", action: library.addDisc)
                Button("Rescan", action: library.scan)
            }
            Text(library.status).foregroundStyle(.secondary)
            if library.games.isEmpty {
                VStack(spacing: 12) {
                    Image(systemName: "books.vertical").font(.largeTitle)
                    Text("No Games Found").font(.headline)
                    Text("Choose a game folder or disc image.")
                }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(library.games) { game in
                    HStack {
                        VStack(alignment: .leading) {
                            Text(game.title).font(.headline)
                            Text(game.engineDisplayName).foregroundStyle(.secondary)
                            Text(game.path).font(.caption).foregroundStyle(.tertiary)
                        }
                        Spacer()
                        Button("Play") { onLaunch(game.path) }
                            .disabled(!game.linkedInProcess)
                    }
                    .padding(.vertical, 5)
                }
            }
            HStack {
                Spacer()
                Button("Quit", action: onQuit)
            }
        }
        .padding(20)
        .frame(minWidth: 780, minHeight: 520)
        .onAppear { library.scan() }
    }
}

@objc(KotonohaMacLauncherPresenter)
final class KotonohaMacLauncherPresenter: NSObject, NSWindowDelegate {
    private static var window: NSWindow?
    private static var delegate: KotonohaMacLauncherPresenter?
    private static var quit: (() -> Void)?

    @objc(presentWithOnLaunch:onQuit:)
    static func present(onLaunch: @escaping (NSString) -> Void,
                        onQuit: @escaping () -> Void) {
        dismiss()
        quit = onQuit
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 600),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Kotonoha"
        window.center()
        let delegate = KotonohaMacLauncherPresenter()
        window.delegate = delegate
        window.contentView = NSHostingView(rootView: MacLauncherView(
            onLaunch: { onLaunch($0 as NSString) }, onQuit: onQuit))
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
