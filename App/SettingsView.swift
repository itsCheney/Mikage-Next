import SwiftUI

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
                Section("外观") {
                    Picker("外观", selection: $model.settings.appearance) {
                        ForEach(Appearance.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    }
                    .pickerStyle(.segmented)
                }

                Section("游戏内") {
                    Picker(selection: $model.settings.background) {
                        ForEach(PlayerBackground.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    } label: {
                        SettingLabel("背景填充", systemImage: "photo")
                    }
                    .pickerStyle(.menu)

                    Toggle(isOn: $model.settings.floatingButton) {
                        SettingLabel("悬浮球", systemImage: "moon.stars.fill")
                    }

                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            SettingLabel("闲置透明度", systemImage: "circle.lefthalf.filled")
                            Spacer()
                            Text("\(Int(idleOpacity * 100))%")
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
                        .accessibilityLabel("闲置透明度")
                    }
                    .padding(.vertical, 4)

                    Toggle(isOn: $model.settings.threeFingerMenu) {
                        SettingLabel("三指轻点唤出菜单", systemImage: "hand.tap")
                    }

                    Toggle(isOn: $model.settings.skipPatchVideos) {
                        SettingLabel("跳过汉化组署名视频", systemImage: "forward.end.fill")
                    }
                    .accessibilityIdentifier("skip-patch-videos-toggle")
                }

                Section {
                    Toggle(isOn: $model.settings.respectSilentMode) {
                        SettingLabel("遵循静音模式", systemImage: "speaker.slash")
                    }
                    .accessibilityIdentifier("respect-ios-silent-mode-toggle")
                } header: {
                    Text("声音")
                } footer: {
                    Text("开启后，设备处于静音模式时游戏不发声；关闭后忽略静音模式继续播放。下次启动游戏时生效。")
                }

                Section {
                    Button {
                        explain("当前 \(model.games.count) 部作品，另有 \(model.missingGames.count) 条缺失记录。\n游戏文件：\(storageTotal)\n\n文件保存在“文件 → 我的 iPhone → Mikage Next”下的 krkr、ons、renpy、artemis 目录中。KiriKiri 存档仍位于各游戏的 savedata 目录。")
                    } label: {
                        HStack {
                            SettingLabel("存储空间", systemImage: "internaldrive")
                            Spacer()
                            Text(storageTotal)
                                .foregroundStyle(.secondary)
                        }
                    }
                } header: {
                    Text("通用")
                } footer: {
                    Text("局域网上传与 ZIP 导入尚未接入。可以通过游戏库右上角的加号导入，或直接将游戏文件夹放入 Documents 下的引擎目录。")
                }

                Section {
                    NavigationLink {
                        AdvancedSettingsView(diagnosticError: $diagnosticError)
                    } label: {
                        Label {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("高级")
                                Text("渲染、Emote 动画、诊断日志")
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

                Section("更多") {
                    ShareLink(item: "Mikage 0.1\n设备：\(UIDevice.current.model)\n系统：\(UIDevice.current.systemVersion)\n渲染偏好：\(model.settings.renderer.displayName)\n完整诊断日志：设置 → 高级 → 开启完整诊断日志，复现问题后导出\n问题描述：\n复现步骤：") {
                        SettingLabel("问题反馈", systemImage: "envelope")
                    }

                    Button {
                        explain("Mikage 0.1 · 多引擎视觉小说播放器\n\n当前内置 KRKRSDL3 runtime，支持自动扫描并启动 KiriKiri 游戏。ONScripter、Ren’Py 与 Artemis 目录已接入游戏库，运行时将在后续阶段适配。\n\n应用不附带游戏内容。")
                    } label: {
                        SettingLabel("关于 Mikage", systemImage: "info.circle")
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
            .navigationTitle("设置")
            .onAppear { idleOpacity = model.settings.idleOpacity }
        }
        .alert("Mikage", isPresented: $showInfo) {
            Button("好", role: .cancel) { }
        } message: {
            Text(info ?? "")
        }
        .onChange(of: diagnosticLogging) { enabled in
            if enabled { diagnosticError = nil }
            do { try AppDiagnostics.shared.configure(enabled: enabled) }
            catch {
                diagnosticLogging = false
                explain("无法启动日志记录：\(error.localizedDescription)")
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
                Picker("渲染方式", selection: $model.settings.renderer) {
                    ForEach(RendererPreference.allCases, id: \.self) {
                        Text($0.displayName).tag($0)
                    }
                }
                .pickerStyle(.menu)

                Toggle("启动时显示性能分析", isOn: $model.settings.performance)
            } header: {
                Text("图形")
            } footer: {
                Text("画面异常时可切换为软件合成 · Metal。Metal 加速离屏绘制、Emote 和 D3DLayer，普通 KRKR 图层仍由 CPU 合成。性能分析也可以在游戏菜单中随时开关。")
            }

            Section {
                Toggle("Emote 候选动画", isOn: $model.settings.experimentalEmote)
                    .accessibilityIdentifier("experimental-emote-toggle")
            } header: {
                Text("动画")
            } footer: {
                Text("新的 Emote 播放方式，用于对比动作流畅度，默认关闭。下次启动游戏时生效；若动作或表情异常，可关闭后重新进入游戏。")
            }

            Section {
                Toggle("完整诊断日志", isOn: $diagnosticLogging)
                    .accessibilityIdentifier("diagnostic-logging-toggle")
                if let diagnosticError {
                    Text("日志记录已停止：\(diagnosticError)")
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
                    Text(exportingLogs ? "正在导出…" : "导出诊断日志")
                }
                .disabled(exportingLogs)
                .accessibilityIdentifier("export-diagnostic-logs")
                Button("清除诊断日志", role: .destructive) { confirmClearLogs = true }
                    .disabled(exportingLogs)
                    .accessibilityIdentifier("clear-diagnostic-logs")
            } header: {
                Text("诊断")
            } footer: {
                Text("用于定位黑屏等问题，默认关闭。记录 App、窗口方向、前后台、音频切换、KRKR 和 SDL 事件；最多约 16 MB，自动轮转，关闭后保留已有记录。可能包含游戏名、资源名及错误文本，分享前请检查。不读取游戏文件或存档，无法代替系统崩溃报告。")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("高级")
        .navigationBarTitleDisplayMode(.inline)
        .alert("Mikage", isPresented: $showInfo) {
            Button("好", role: .cancel) { }
        } message: {
            Text(info ?? "")
        }
        .sheet(isPresented: $showLogExport) {
            if let logExport { ActivitySheet(items: [logExport]) }
        }
        .confirmationDialog("清除已记录的诊断日志？", isPresented: $confirmClearLogs, titleVisibility: .visible) {
            Button("清除日志", role: .destructive) {
                do { try AppDiagnostics.shared.clear() }
                catch { explain("清除日志失败：\(error.localizedDescription)") }
            }
        }
    }

    private func explain(_ text: String) {
        info = text
        showInfo = true
    }
}
