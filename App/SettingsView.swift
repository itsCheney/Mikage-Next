import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var info: String?
    @State private var showInfo = false

    var body: some View {
        NavigationStack {
            Form {
                Section("外观") {
                    Picker("外观", selection: $model.settings.appearance) {
                        Text("跟随系统").tag("跟随系统")
                        Text("浅色").tag("浅色")
                        Text("深色").tag("深色")
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
                        Text("Metal 原生").tag("Metal 原生")
                        Text("OpenGL ES").tag("OpenGL ES")
                    } label: {
                        Label("渲染方式", systemImage: "cpu")
                    }
                    .pickerStyle(.menu)
                } header: {
                    Text("图形")
                } footer: {
                    Text("游戏显示异常时可尝试切换。当前构建尚未连接渲染器。")
                }

                Section("游戏内") {
                    Picker(selection: $model.settings.background) {
                        Text("游戏封面").tag("游戏封面")
                        Text("纯黑").tag("纯黑")
                    } label: {
                        Label("背景填充", systemImage: "photo.fill")
                    }
                    .pickerStyle(.menu)

                    Toggle(isOn: $model.settings.floatingButton) {
                        Label("悬浮球", systemImage: "pawprint.fill")
                    }

                    HStack {
                        Label("闲置透明度", systemImage: "circle.lefthalf.filled")
                        Slider(value: $model.settings.idleOpacity, in: 0.1...1)
                            .accessibilityLabel("闲置透明度")
                        Text("\(Int(model.settings.idleOpacity * 100))%")
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
                }

                Section("文件与传输") {
                    Button {
                        explain("局域网上传模块尚未连接，当前不会启动网络服务。请通过游戏库右上角的加号导入文件夹。")
                    } label: {
                        HStack {
                            Label("局域网上传", systemImage: "wifi")
                            Spacer()
                            Text("待接入").foregroundStyle(.secondary)
                        }
                    }

                    Button {
                        explain("已导入 \(model.games.count) 部作品\n游戏文件：\(AppModel.size(model.games.reduce(0) { $0 + $1.byteCount }))\n\n文件保存在“文件 → 我的 iPhone → Mikage Next → Mikage”中。可在此备份游戏及其 savedata 目录。")
                    } label: {
                        Label("存储信息", systemImage: "internaldrive.fill")
                    }
                }

                Section("更多") {
                    ShareLink(item: "Mikage 0.1\n设备：\(UIDevice.current.model)\n系统：\(UIDevice.current.systemVersion)\n渲染偏好：\(model.settings.renderer)\n问题描述：\n复现步骤：") {
                        Label("问题反馈", systemImage: "envelope.fill")
                    }

                    Button {
                        explain("Mikage 0.1 · 界面与导入原型\n\n按参考截图实现游戏库、设置和游戏内菜单。此构建尚未连接 KRKR、ZIP 解压和 Wi-Fi 传输，不能运行真实游戏。\n\n封面为原创矢量示意图，应用不附带游戏内容。")
                    } label: {
                        Label("关于", systemImage: "info.circle.fill")
                    }

                    Button {
                        model.demo = true
                        model.tab = 0
                    } label: {
                        Label("查看界面演示", systemImage: "play.rectangle")
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
        }
        .alert("Mikage", isPresented: $showInfo) {
            Button("好", role: .cancel) { }
        } message: {
            Text(info ?? "")
        }
    }

    private func explain(_ text: String) {
        info = text
        showInfo = true
    }
}
