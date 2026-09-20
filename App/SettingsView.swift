import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var info: String?
    @State private var showInfo = false
    @AppStorage(AppDiagnostics.preferenceKey) private var diagnosticLogging = false
    @State private var exportingLogs = false
    @State private var logExport: URL?
    @State private var showLogExport = false
    @State private var confirmClearLogs = false
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

                    Button {
                        explain("当前提供默认应用图标，替换图标资源将在后续版本加入。")
                    } label: {
                        Label("App 图标", systemImage: "app.badge")
                    }
                }

                Section {
                    Picker(selection: $model.settings.renderer) {
                        ForEach(RendererPreference.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    } label: {
                        Label("渲染方式", systemImage: "cpu")
                    }
                    .pickerStyle(.menu)
                } header: {
                    Text("图形")
                } footer: {
                    Text("游戏显示异常时可尝试切换。Metal 加速离屏绘制、Emote 和 D3DLayer，普通 KRKR 图层仍由 CPU 合成。可切换为软件合成 · Metal。")
                }

                Section("游戏内") {
                    Picker(selection: $model.settings.background) {
                        ForEach(PlayerBackground.allCases, id: \.self) {
                            Text($0.displayName).tag($0)
                        }
                    } label: {
                        Label("背景填充", systemImage: "photo.fill")
                    }
                    .pickerStyle(.menu)

                    Toggle(isOn: $model.settings.floatingButton) {
                        Label("悬浮球", systemImage: "pawprint.fill")
                    }

                    HStack {
                        Label("闲置透明度", systemImage: "circle.lefthalf.filled")
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
                        Text("\(Int(idleOpacity * 100))%")
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                            .frame(width: 38, alignment: .trailing)
                    }

                    Toggle(isOn: $model.settings.threeFingerMenu) {
                        Label("三指轻点唤出菜单", systemImage: "hand.tap")
                    }

                    Toggle(isOn: $model.settings.performance) {
                        Label("性能分析", systemImage: "gauge.with.dots.needle.33percent")
                    }

                    Toggle(isOn: $model.settings.skipPatchVideos) {
                        Label("跳过汉化组署名视频", systemImage: "forward.end.fill")
                    }
                    .accessibilityIdentifier("skip-patch-videos-toggle")
                }

                Section("文件与传输") {
                    Button {
                        explain("局域网上传模块尚未连接。请通过游戏库右上角的加号导入，或直接将游戏文件夹放入 Documents/krkr、ons、renpy、artemis。")
                    } label: {
                        HStack {
                            Label("局域网上传", systemImage: "wifi")
                            Spacer()
                            Text("待接入").foregroundStyle(.secondary)
                        }
                    }

                    Button {
                        explain("当前 \(model.games.count) 部作品，另有 \(model.missingGames.count) 条缺失记录。\n游戏文件：\(AppModel.size(model.games.reduce(0) { $0 + $1.byteCount }))\n\n文件保存在“文件 → 我的 iPhone → Mikage Next”下的 krkr、ons、renpy、artemis 目录中。KiriKiri 存档仍位于各游戏的 savedata 目录。")
                    } label: {
                        Label("存储信息", systemImage: "internaldrive.fill")
                    }
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
                        Label(exportingLogs ? "正在导出…" : "导出诊断日志", systemImage: "square.and.arrow.up")
                    }
                    .disabled(exportingLogs)
                    .accessibilityIdentifier("export-diagnostic-logs")
                    Button("清除诊断日志", role: .destructive) { confirmClearLogs = true }
                        .disabled(exportingLogs)
                        .accessibilityIdentifier("clear-diagnostic-logs")
                } header: {
                    Text("诊断与日志")
                } footer: {
                    Text("默认关闭。记录 App、窗口方向、前后台、音频切换、KRKR 和 SDL 事件，用于定位黑屏。日志最多约 8 MB，自动轮转；关闭后保留已有记录。可能包含游戏名、资源名及错误文本，分享前请检查。不读取游戏文件或存档，并过滤 VM 反汇编和寄存器转储；无法代替系统崩溃报告。")
                }

                Section("更多") {
                    ShareLink(item: "Mikage 0.1\n设备：\(UIDevice.current.model)\n系统：\(UIDevice.current.systemVersion)\n渲染偏好：\(model.settings.renderer.displayName)\n完整诊断日志：设置 → 诊断与日志 → 开启记录，复现问题后导出\n问题描述：\n复现步骤：") {
                        Label("问题反馈", systemImage: "envelope.fill")
                    }

                    Button {
                        explain("Mikage 0.1 · 多引擎视觉小说播放器\n\n当前内置 KRKRSDL3 runtime，支持自动扫描并启动 KiriKiri 游戏。ONScripter、Ren’Py 与 Artemis 目录已接入游戏库，运行时将在后续阶段适配。\n\n应用不附带游戏内容。")
                    } label: {
                        Label("关于", systemImage: "info.circle.fill")
                    }

                }

                Section {
                    Text("MIKAGE NEXT · 0.1")
                        .font(.caption2)
                        .tracking(3)
                        .foregroundStyle(.tertiary)
                        .frame(maxWidth: .infinity)
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
        .sheet(isPresented: $showLogExport) {
            if let logExport { ActivitySheet(items: [logExport]) }
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
