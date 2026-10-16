import SwiftUI
import VNCore

struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var info: String?
    @State private var showInfo = false
    @AppStorage(AppDiagnostics.preferenceKey) private var diagnosticLogging = false
    /// Owned here rather than by the Advanced page so a logging failure is
    /// caught while that page is closed.
    @State private var diagnosticError: String?
    /// Mirrors `settings.idleOpacity` while dragging; committed on release.
    @State private var idleOpacity: Double = 0

    var body: some View {
        NavigationStack {
            Form {
                Section("Appearance") {
                    Picker("Appearance", selection: $model.settings.appearance) {
                        ForEach(Appearance.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    }
                    .pickerStyle(.segmented)
                }

                Section {
                    NavigationLink {
                        AccountsSettingsView()
                    } label: {
                        Label {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("Accounts & Sync")
                                Text(accountsSummary)
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }
                        } icon: {
                            Image(systemName: "person.crop.circle.badge.checkmark")
                                .foregroundStyle(.tint)
                        }
                    }
                    .accessibilityIdentifier("accounts-settings-link")
                }

                Section("In Game") {
                    Picker(selection: $model.settings.background) {
                        ForEach(PlayerBackground.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    } label: {
                        SettingLabel("Background Fill", systemImage: "photo")
                    }
                    .pickerStyle(.menu)

                    Toggle(isOn: $model.settings.floatingButton) {
                        SettingLabel("Floating Button", systemImage: "moon.stars.fill")
                    }

                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            SettingLabel("Idle Opacity", systemImage: "circle.lefthalf.filled")
                            Spacer()
                            Text(verbatim: "\(Int(idleOpacity * 100))%")
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
                        // Bound to local state: committing every intermediate
                        // value would re-encode and write the whole preference
                        // file on each drag callback.
                        Slider(
                            value: $idleOpacity,
                            in: 0.1...1,
                            onEditingChanged: { editing in
                                if !editing { model.settings.idleOpacity = idleOpacity }
                            }
                        )
                        .accessibilityLabel("Idle Opacity")
                    }
                    .padding(.vertical, 4)

                    Toggle(isOn: $model.settings.stretchToScreen) {
                        SettingLabel("Stretch 4:3 Games to 16:9", systemImage: "arrow.left.and.right")
                    }

                    Toggle(isOn: $model.settings.threeFingerMenu) {
                        SettingLabel("Three-Finger Tap for Menu", systemImage: "hand.tap")
                    }

                    Toggle(isOn: $model.settings.skipPatchVideos) {
                        SettingLabel("Skip Translation Group Credit Videos", systemImage: "forward.end.fill")
                    }
                    .accessibilityIdentifier("skip-patch-videos-toggle")
                }

                Section {
                    Toggle(isOn: $model.settings.respectSilentMode) {
                        SettingLabel("Respect Silent Mode", systemImage: "speaker.slash")
                    }
                    .accessibilityIdentifier("respect-ios-silent-mode-toggle")
                } header: {
                    Text("Sound")
                } footer: {
                    Text("When on, games stay silent while the device is in Silent Mode; when off, they play anyway. Takes effect the next time a game launches.")
                }

                Section {
                    NavigationLink {
                        StorageView()
                    } label: {
                        HStack {
                            SettingLabel("Storage", systemImage: "internaldrive")
                            Spacer()
                            Text(storageTotal)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .accessibilityIdentifier("storage-settings-link")
                } header: {
                    Text("General")
                } footer: {
                    Text("Wi-Fi upload and ZIP import aren’t available yet. Import with the plus button at the top right of the Library, or put game folders directly into the engine folders under Documents.")
                }

                Section {
                    NavigationLink {
                        AdvancedSettingsView(diagnosticError: $diagnosticError)
                    } label: {
                        Label {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("Advanced")
                                Text("Rendering, Emote animation, diagnostic logs")
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }
                        } icon: {
                            Image(systemName: "slider.horizontal.3")
                                .foregroundStyle(.tint)
                        }
                    }
                    .accessibilityIdentifier("advanced-settings-link")
                }

                Section("More") {
                    ShareLink(item: String(localized: "Mikage 0.1\nDevice: \(UIDevice.current.model)\nSystem: \(UIDevice.current.systemVersion)\nRenderer: \(model.settings.renderer.displayName)\nFull diagnostic log: Settings → Advanced → turn on Full Diagnostic Log, reproduce the issue, then export\nDescription:\nSteps to reproduce:")) {
                        SettingLabel("Send Feedback", systemImage: "envelope")
                    }

                    Button {
                        explain(String(localized: "Mikage 0.1 · Multi-engine visual novel player\n\nIncludes the KRKRSDL3 runtime for KiriKiri games, ONScripter Yuri for ONScripter games, openArtemis for Artemis games and Ren’Py 8 for Ren’Py games.\n\nThe app includes no game content."))
                    } label: {
                        SettingLabel("About Mikage", systemImage: "info.circle")
                    }
                }

                Section {
                    let buildInfo = AppBuildInfo()
                    VStack(spacing: 4) {
                        Text(buildInfo.versionLine)
                            .tracking(3)
                        Text(buildInfo.headLine)
                            .font(.system(.caption2, design: .monospaced))
                    }
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
                    .frame(maxWidth: .infinity)
                    .accessibilityIdentifier("app-build-info")
                }
                .listRowBackground(Color.clear)
            }
            .scrollContentBackground(.hidden)
            .background(AppBackground())
            .navigationTitle("Settings")
            .onAppear { idleOpacity = model.settings.idleOpacity }
        }
        .alert("Mikage", isPresented: $showInfo) {
            Button("OK", role: .cancel) { }
        } message: {
            Text(info ?? "")
        }
        .onChange(of: diagnosticLogging) { enabled in
            if enabled { diagnosticError = nil }
            do { try AppDiagnostics.shared.configure(enabled: enabled) }
            catch {
                diagnosticLogging = false
                explain(String(localized: "Couldn’t start logging: \(error.localizedDescription)"))
            }
        }
        .task(id: diagnosticLogging) {
            while diagnosticLogging && !Task.isCancelled {
                if let error = AppDiagnostics.shared.lastError {
                    diagnosticError = error
                    diagnosticLogging = false
                    break
                }
                try? await Task.sleep(nanoseconds: 2_000_000_000)
            }
        }
    }

    /// "Bangumi, VNDB" for connected services, or a prompt when none is.
    private var accountsSummary: String {
        let connected = MetadataService.allCases.filter { model.accounts[$0] != nil }
        return connected.isEmpty
            ? String(localized: "Bangumi and VNDB")
            : connected.map(\.displayName).joined(separator: ", ")
    }

    private var storageTotal: String {
        AppModel.size(model.games.reduce(0) { $0 + $1.byteCount })
    }

    private func explain(_ text: String) {
        info = text
        showInfo = true
    }
}

/// Rendering, Emote and diagnostics options most players never need.
struct AdvancedSettingsView: View {
    @EnvironmentObject private var model: AppModel
    @Binding var diagnosticError: String?
    @AppStorage(AppDiagnostics.preferenceKey) private var diagnosticLogging = false
    @State private var exportingLogs = false
    @State private var logExport: URL?
    @State private var showLogExport = false
    @State private var confirmClearLogs = false
    @State private var info: String?
    @State private var showInfo = false

    var body: some View {
        Form {
            Section {
                Picker("Renderer", selection: $model.settings.renderer) {
                    ForEach(RendererPreference.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
                .pickerStyle(.menu)

                Toggle("Show Performance Stats on Launch", isOn: $model.settings.performance)

                Picker("Performance Stats", selection: $model.settings.performanceDetail) {
                    ForEach(PerformanceDetail.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
                .pickerStyle(.menu)
            } header: {
                Text("Graphics")
            } footer: {
                Text("If the picture looks wrong, switch to Software Compositing · Metal. Metal accelerates offscreen drawing, Emote and D3DLayer; regular KRKR layers are still composited on the CPU. Performance stats can also be toggled any time from the in-game menu.")
            }


            Section {
                Toggle("Full Diagnostic Log", isOn: $diagnosticLogging)
                    .accessibilityIdentifier("diagnostic-logging-toggle")
                if let diagnosticError {
                    Text("Logging stopped: \(diagnosticError)")
                        .foregroundStyle(.red)
                }
                Button {
                    exportingLogs = true
                    Task {
                        do {
                            logExport = try await Task.detached(priority: .utility) {
                                try AppDiagnostics.shared.export()
                            }.value
                            showLogExport = true
                        } catch { explain(error.localizedDescription) }
                        exportingLogs = false
                    }
                } label: {
                    exportingLogs ? Text("Exporting…") : Text("Export Diagnostic Log")
                }
                .disabled(exportingLogs)
                .accessibilityIdentifier("export-diagnostic-logs")
                Button("Clear Diagnostic Log", role: .destructive) { confirmClearLogs = true }
                    .disabled(exportingLogs)
                    .accessibilityIdentifier("clear-diagnostic-logs")
            } header: {
                Text("Diagnostics")
            } footer: {
                Text("Helps track down problems such as black screens. Off by default. Records app, window orientation, foreground and background, audio route, KRKR and SDL events; up to about 16 MB with automatic rotation, and existing records are kept after turning it off. May include game names, resource names and error text, so check before sharing. Doesn’t read game files or saves, and can’t replace system crash reports.")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Advanced")
        .navigationBarTitleDisplayMode(.inline)
        .alert("Mikage", isPresented: $showInfo) {
            Button("OK", role: .cancel) { }
        } message: {
            Text(info ?? "")
        }
        .sheet(isPresented: $showLogExport) {
            if let logExport { ActivitySheet(items: [logExport]) }
        }
        .confirmationDialog("Clear recorded diagnostic logs?", isPresented: $confirmClearLogs, titleVisibility: .visible) {
            Button("Clear Logs", role: .destructive) {
                do { try AppDiagnostics.shared.clear() }
                catch { explain(String(localized: "Couldn’t clear logs: \(error.localizedDescription)")) }
            }
        }
    }

    private func explain(_ text: String) {
        info = text
        showInfo = true
    }
}
