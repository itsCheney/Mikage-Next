import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var info: String?
    @State private var showInfo = false
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 23) {
                Text("设置").font(.system(size: 34, weight: .bold)).padding(.top, 16)
                group("外观") {
                    VStack(spacing: 0) {
                        row("外观", "circle.lefthalf.filled") { EmptyView() }
                        HStack(spacing: 0) {
                            ForEach(["跟随系统", "浅色", "深色"], id: \.self) { appearance in
                                Button { model.settings.appearance = appearance } label: {
                                    Text(appearance).font(.system(size: 12, weight: .medium))
                                        .frame(maxWidth: .infinity).frame(height: 29)
                                        .foregroundStyle(model.settings.appearance == appearance ? Color.black : Color.secondary)
                                        .background(model.settings.appearance == appearance ? Color.white.opacity(0.94) : .clear, in: Capsule())
                                }.buttonStyle(.plain)
                                    .accessibilityAddTraits(model.settings.appearance == appearance ? .isSelected : [])
                            }
                        }.padding(3).background(Color.primary.opacity(0.045), in: Capsule())
                            .padding(.horizontal, 14).padding(.bottom, 12)
                        separator
                        row("点缀色", "paintpalette") {
                            HStack(spacing: 10) {
                                ForEach(0..<3) { index in
                                    Button { model.settings.accent = index } label: {
                                        Circle().fill(color(index)).frame(width: 23, height: 23)
                                            .padding(3).overlay(Circle().strokeBorder(model.settings.accent == index ? Color.primary : .clear, lineWidth: 1.5))
                                    }.buttonStyle(.plain).accessibilityLabel(["雾灰", "麦金", "晴蓝"][index])
                                        .accessibilityAddTraits(model.settings.accent == index ? .isSelected : [])
                                }
                            }
                        }
                        separator
                        Button { explain("当前提供默认应用图标，替换图标资源将在后续版本加入。") } label: {
                            row("App 图标", "app.badge") {
                                Image(systemName: "books.vertical.fill").font(.system(size: 19)).foregroundStyle(.white)
                                    .frame(width: 29, height: 29).background(model.settings.tint.gradient, in: RoundedRectangle(cornerRadius: 7))
                                chevron
                            }
                        }.buttonStyle(.plain)
                    }
                }
                group("图形", footer: "游戏显示异常时可尝试切换。当前构建尚未连接渲染器。") {
                    Menu {
                        Picker("渲染方式", selection: $model.settings.renderer) {
                            Text("Metal 原生").tag("Metal 原生")
                            Text("OpenGL ES").tag("OpenGL ES")
                        }
                    } label: { row("渲染方式", "cpu") { Text(model.settings.renderer).foregroundStyle(.secondary); chevron } }
                        .buttonStyle(.plain)
                }
                group("游戏内") {
                    VStack(spacing: 0) {
                        Menu {
                            Picker("背景填充", selection: $model.settings.background) {
                                Text("游戏封面").tag("游戏封面"); Text("纯黑").tag("纯黑")
                            }
                        } label: { row("背景填充", "photo.fill") { Text(model.settings.background).foregroundStyle(.secondary); chevron } }.buttonStyle(.plain)
                        separator
                        row("悬浮球", "pawprint.fill") { Toggle("悬浮球", isOn: $model.settings.floatingButton).labelsHidden() }
                        separator
                        row("闲置透明度", "circle.lefthalf.filled") {
                            Slider(value: $model.settings.idleOpacity, in: 0.1...1).frame(maxWidth: 140).accessibilityLabel("闲置透明度")
                            Text("\(Int(model.settings.idleOpacity * 100))%").font(.system(size: 12)).foregroundStyle(.secondary).monospacedDigit().frame(width: 31)
                        }
                        separator
                        row("三指轻点唤出菜单", "hand.tap") { Toggle("三指轻点唤出菜单", isOn: $model.settings.threeFingerMenu).labelsHidden() }
                        separator
                        row("性能分析", "gauge.with.dots.needle.33percent") { Toggle("性能分析", isOn: $model.settings.performance).labelsHidden() }
                    }
                }
                group("文件与传输") {
                    VStack(spacing: 0) {
                        Button { explain("局域网上传模块尚未连接，当前不会启动网络服务。请通过游戏库右上角的加号导入文件夹。") } label: {
                            row("局域网上传", "wifi") { Text("待接入").font(.subheadline).foregroundStyle(.secondary); chevron }
                        }.buttonStyle(.plain)
                        separator
                        Button {
                            explain("已导入 \(model.games.count) 部作品\n游戏文件：\(AppModel.size(model.games.reduce(0) { $0 + $1.byteCount }))\n\n文件保存在“文件 → 我的 iPhone → Mikage Next → VNPlayer”中。可在此备份游戏及其 savedata 目录。")
                        } label: { row("存储信息", "internaldrive.fill") { chevron } }.buttonStyle(.plain)
                    }
                }
                group("更多") {
                    VStack(spacing: 0) {
                        ShareLink(item: "VNPlayer 0.1\n设备：\(UIDevice.current.model)\n系统：\(UIDevice.current.systemVersion)\n渲染偏好：\(model.settings.renderer)\n问题描述：\n复现步骤：") {
                            row("问题反馈", "envelope.fill") { chevron }
                        }.buttonStyle(.plain)
                        separator
                        Button { explain("VNPlayer 0.1 · 界面与导入原型\n\n按参考截图实现游戏库、设置和游戏内菜单。此构建尚未连接 KRKR、ZIP 解压和 Wi-Fi 传输，不能运行真实游戏。\n\n封面为原创矢量示意图，应用不附带游戏内容。") } label: {
                            row("关于", "info.circle.fill") { chevron }
                        }.buttonStyle(.plain)
                        separator
                        Button { model.demo = true; model.tab = 0 } label: { row("查看界面演示", "play.rectangle") { chevron } }.buttonStyle(.plain)
                    }
                }
                Text("MIKAGE NEXT · 0.1").font(.system(size: 10, weight: .medium)).tracking(3).foregroundStyle(.tertiary)
                    .frame(maxWidth: .infinity).padding(.vertical, 5)
            }.padding(.horizontal, 18).padding(.bottom, 20).frame(maxWidth: 680).frame(maxWidth: .infinity)
        }.alert("VNPlayer", isPresented: $showInfo) { Button("好", role: .cancel) { } } message: { Text(info ?? "") }
    }
    private func explain(_ text: String) { info = text; showInfo = true }
    private var chevron: some View { Image(systemName: "chevron.right").font(.system(size: 12, weight: .semibold)).foregroundStyle(.tertiary) }
    private var separator: some View { Rectangle().fill(Color.primary.opacity(0.055)).frame(height: 0.5).padding(.leading, 56) }
    private func color(_ index: Int) -> Color { var settings = PlayerSettings(); settings.accent = index; return settings.tint }
    private func row<T: View>(_ title: String, _ symbol: String, @ViewBuilder trailing: () -> T) -> some View {
        HStack(spacing: 12) {
            Image(systemName: symbol).font(.system(size: 16, weight: .medium)).frame(width: 30, height: 30)
                .background(Color.primary.opacity(0.065), in: RoundedRectangle(cornerRadius: 8))
            Text(title).font(.system(size: 16)).fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 6)
            trailing()
        }.padding(.horizontal, 14).frame(minHeight: 56).contentShape(Rectangle())
    }
    private func group<T: View>(_ title: String, footer: String? = nil, @ViewBuilder content: () -> T) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.system(size: 11, weight: .medium)).tracking(2).foregroundStyle(.secondary).padding(.leading, 6)
            content().glass(21)
            if let footer { Text(footer).font(.system(size: 11)).foregroundStyle(.secondary).padding(.horizontal, 6) }
        }
    }
}
