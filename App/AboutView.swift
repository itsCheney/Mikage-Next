import SwiftUI
import UIKit

/// Settings › About Mikage: the app, its version, where its source lives,
/// and the engine runtimes and libraries it is built from.
struct AboutView: View {
    @EnvironmentObject private var model: AppModel
    @State private var copied = false
    @State private var showingWelcome = false

    private let buildInfo = AppBuildInfo()

    var body: some View {
        Form {
            Section {
                header
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets())
            }

            Section {
                Link(destination: AboutCredits.sourceURL) {
                    HStack {
                        SettingLabel("Source Code", systemImage: "chevron.left.forwardslash.chevron.right")
                        Spacer()
                        Text(verbatim: "GitHub")
                            .foregroundStyle(.secondary)
                    }
                }
                ShareLink(item: feedbackTemplate) {
                    SettingLabel("Send Feedback", systemImage: "envelope")
                }
                Button {
                    showingWelcome = true
                } label: {
                    SettingLabel("Welcome Guide", systemImage: "sparkles")
                }
            }

            Section {
                ForEach(AboutCredits.engines) { engine in
                    HStack(spacing: 12) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(verbatim: engine.name)
                            Text(verbatim: engine.runtime)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        LicenseBadge(license: engine.license)
                    }
                    .padding(.vertical, 2)
                    .accessibilityElement(children: .combine)
                }
            } header: {
                Text("Engines")
            } footer: {
                Text("Each engine is a separate open-source runtime built into the app.")
            }

            Section {
                NavigationLink {
                    LicensesView()
                } label: {
                    HStack {
                        Text("Open-Source Licenses")
                        Spacer()
                        Text(verbatim: "\(AboutCredits.licenseCount)")
                            .foregroundStyle(.secondary)
                    }
                }
            } footer: {
                VStack(spacing: 4) {
                    Text("Mikage is free software under GPL-3.0-or-later.")
                    Text("The app includes no game content.")
                }
                .frame(maxWidth: .infinity)
                .multilineTextAlignment(.center)
                .padding(.top, 14)
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("About Mikage")
        .navigationBarTitleDisplayMode(.inline)
        .sheet(isPresented: $showingWelcome) {
            WelcomeGuideView().environmentObject(model)
        }
    }

    private var header: some View {
        VStack(spacing: 0) {
            AppMark()
                .frame(width: 92, height: 92)
            Text(verbatim: "Mikage")
                .font(.title.weight(.heavy))
                .padding(.top, 16)
            Text("A visual novel player for many engines")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .padding(.top, 2)
            Button {
                UIPasteboard.general.string = "\(buildInfo.versionLine)\n\(buildInfo.headLine)"
                withAnimation { copied = true }
                Task {
                    try? await Task.sleep(nanoseconds: 1_600_000_000)
                    withAnimation { copied = false }
                }
            } label: {
                HStack(spacing: 8) {
                    Text(verbatim: versionText)
                        .font(.system(.footnote, design: .monospaced))
                        .foregroundStyle(.primary)
                    Image(systemName: copied ? "checkmark" : "doc.on.doc")
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(.tint)
                }
                .padding(.horizontal, 14)
                .frame(height: 34)
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: Capsule())
                .contentShape(Capsule())
            }
            .buttonStyle(.plain)
            .padding(.top, 14)
            .accessibilityLabel(Text("Copy Version"))
            .accessibilityValue(Text(verbatim: versionText))
            Text("Copied version and HEAD")
                .font(.caption)
                .foregroundStyle(.green)
                .opacity(copied ? 1 : 0)
                .padding(.top, 6)
                .accessibilityHidden(!copied)
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 8)
    }

    /// The same report template as Settings › Send Feedback.
    private var feedbackTemplate: String {
        String(localized: "Mikage 0.1\nDevice: \(UIDevice.current.model)\nSystem: \(UIDevice.current.systemVersion)\nRenderer: \(model.settings.renderer.displayName)\nFull diagnostic log: Settings → Advanced → turn on Full Diagnostic Log, reproduce the issue, then export\nDescription:\nSteps to reproduce:")
    }

    private var versionText: String {
        let revision = buildInfo.sourceRevision == "unknown" ? "" : " · \(buildInfo.sourceRevision)"
        return "\(buildInfo.version) (\(buildInfo.build))\(revision)"
    }
}

/// The app's mark: a white tile with the accent moon.
private struct AppMark: View {
    var body: some View {
        GeometryReader { proxy in
            let side = proxy.size.width
            RoundedRectangle(cornerRadius: side * 0.24, style: .continuous)
                .fill(Color.white)
                .overlay(
                    RoundedRectangle(cornerRadius: side * 0.24, style: .continuous)
                        .strokeBorder(Color.black.opacity(0.08), lineWidth: 0.5)
                )
                .overlay(
                    Image(systemName: "moon.stars.fill")
                        .font(.system(size: side * 0.5, weight: .semibold))
                        .foregroundStyle(Color(FloatingButtonStyle.accent))
                )
                .shadow(color: .black.opacity(0.12), radius: 10, y: 4)
        }
        .aspectRatio(1, contentMode: .fit)
        .accessibilityHidden(true)
    }
}

private struct LicenseBadge: View {
    let license: String

    var body: some View {
        Text(verbatim: license)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.tint)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .background(Color.accentColor.opacity(0.12), in: RoundedRectangle(cornerRadius: 6, style: .continuous))
    }
}

/// About › Open-Source Licenses: each component and a link to its license.
private struct LicensesView: View {
    var body: some View {
        Form {
            Section("App") {
                row(AboutCredits.app)
            }
            Section("Engine Runtimes") {
                ForEach(AboutCredits.engines) { row($0) }
            }
            Section {
                ForEach(AboutCredits.libraries) { row($0) }
            } header: {
                Text("Libraries")
            } footer: {
                Text("Tap a row to read its full license text.")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Licenses")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func row(_ item: AboutCredits.Item) -> some View {
        Link(destination: item.licenseURL) {
            HStack {
                Text(verbatim: item.runtime)
                    .foregroundStyle(.primary)
                Spacer()
                LicenseBadge(license: item.license)
            }
        }
    }
}

enum AboutCredits {
    struct Item: Identifiable {
        var id: String { runtime }
        /// The engine a runtime plays, or the component's own name.
        let name: String
        let runtime: String
        let license: String
        let licenseURL: URL
    }

    static let sourceURL = URL(string: "https://github.com/itsCheney/Mikage-Next")!

    static let app = item("Mikage", "Mikage", "GPL-3.0+", "https://github.com/itsCheney/Mikage-Next/blob/HEAD/LICENSE")

    static let engines: [Item] = [
        item("KiriKiri", "KRKRSDL3", "BSD-style", "https://github.com/itsCheney/krkrsdl3/blob/HEAD/LICENSE"),
        item("ONScripter", "ONScripter Yuri", "GPL-2.0+", "https://github.com/itsCheney/OnscripterYuri/blob/HEAD/LICENSE"),
        item("Artemis", "openArtemis", "MIT", "https://github.com/itsCheney/openartemis/blob/HEAD/LICENSE"),
        item("Ren’Py", "Ren’Py 8", "MIT", "https://www.renpy.org/doc/html/license.html"),
        item("SiglusEngine", "siglus_rs", "MPL-2.0", "https://github.com/itsCheney/siglus_rs/blob/HEAD/LICENSE-MPL-2.0"),
        item("FVP", "rfvp", "MPL-2.0", "https://github.com/itsCheney/rfvp/blob/HEAD/LICENSE"),
        item("BGI", "ethornell-rs", "MPL-2.0", "https://www.mozilla.org/MPL/2.0/"),
        item("YU-RIS", "MikageYURIS", "GPL-3.0+", "https://github.com/itsCheney/MikageYURIS/blob/HEAD/LICENSE"),
    ]

    static let libraries: [Item] = [
        item("SDL3", "SDL3", "zlib", "https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt"),
        item("sdl2-compat", "sdl2-compat", "zlib", "https://github.com/libsdl-org/sdl2-compat/blob/main/LICENSE.txt"),
        item("MetalANGLE", "MetalANGLE", "BSD-3", "https://github.com/kakashidinho/metalangle/blob/master/LICENSE"),
        item("FreeType", "FreeType", "FTL", "https://freetype.org/license.html"),
    ]

    static var licenseCount: Int { 1 + engines.count + libraries.count }

    private static func item(_ name: String, _ runtime: String, _ license: String, _ url: String) -> Item {
        Item(name: name, runtime: runtime, license: license, licenseURL: URL(string: url)!)
    }
}
