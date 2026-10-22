import SwiftUI

/// Settings › Floating Button: the menu button, its idle opacity and the
/// quick actions that float beside it.
struct FloatingButtonSettingsView: View {
    @EnvironmentObject private var model: AppModel
    /// Mirrors `settings.idleOpacity` while dragging; committed on release.
    @State private var idleOpacity: Double = 0
    @State private var positionsReset = false

    private var actions: [QuickActionKind] { model.settings.quickActions }
    private var available: [QuickActionKind] {
        QuickActionKind.allCases.filter { !actions.contains($0) }
    }

    var body: some View {
        Form {
            Section {
                FloatingButtonPreview(
                    enabled: model.settings.floatingButton,
                    opacity: idleOpacity,
                    actions: actions
                )
                .listRowInsets(EdgeInsets())
                .listRowBackground(Color.clear)
            }

            Section {
                Toggle(isOn: $model.settings.floatingButton) {
                    SettingLabel("Show Floating Button", systemImage: "moon.stars.fill")
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
            } footer: {
                Text("The button fades to this opacity after a few seconds without a touch. Drag it anywhere; it snaps to the nearest edge, or into the bar beside the game.")
            }

            Section {
                if actions.isEmpty {
                    Text("No quick actions")
                        .foregroundStyle(.secondary)
                }
                ForEach(actions) { action in
                    HStack {
                        QuickActionLabel(action: action)
                        Spacer()
                        Button {
                            model.settings.quickActions.removeAll { $0 == action }
                        } label: {
                            Image(systemName: "minus.circle.fill")
                                .symbolRenderingMode(.palette)
                                .foregroundStyle(.white, .red)
                                .imageScale(.large)
                        }
                        .buttonStyle(.borderless)
                        .accessibilityLabel(Text("Remove \(action.displayName)"))
                    }
                }
            } header: {
                HStack {
                    Text("Quick Actions")
                    Spacer()
                    Text(verbatim: "\(actions.count)/\(QuickActionKind.limit)")
                        .monospacedDigit()
                }
            } footer: {
                Text("Quick actions float on their own beside the game and run with one tap. Skip here also skips unread text, like holding Ctrl.")
            }

            Section("Available") {
                ForEach(available) { action in
                    HStack {
                        QuickActionLabel(action: action)
                        Spacer()
                        Button {
                            model.settings.quickActions.append(action)
                        } label: {
                            Image(systemName: "plus.circle.fill")
                                .symbolRenderingMode(.palette)
                                .foregroundStyle(.white, .green)
                                .imageScale(.large)
                        }
                        .buttonStyle(.borderless)
                        .disabled(actions.count >= QuickActionKind.limit)
                        .accessibilityLabel(Text("Add \(action.displayName)"))
                    }
                }
            }

            Section {
                Button {
                    FloatingControlPositions.reset()
                    positionsReset = true
                } label: {
                    SettingLabel("Reset Positions", systemImage: "arrow.counterclockwise")
                }
                .disabled(positionsReset)
            } footer: {
                Text("Puts the menu button and quick actions back in their corners.")
            }
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .navigationTitle("Floating Button")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { idleOpacity = model.settings.idleOpacity }
    }
}

private struct QuickActionLabel: View {
    let action: QuickActionKind

    var body: some View {
        Label {
            Text(action.displayName)
                .foregroundStyle(.primary)
        } icon: {
            Image(systemName: action.symbol)
                .foregroundStyle(.tint)
        }
    }
}

/// A landscape screen with the buttons where they start: the menu button
/// top right, quick actions in the bottom corners.
private struct FloatingButtonPreview: View {
    let enabled: Bool
    let opacity: Double
    let actions: [QuickActionKind]

    private static let accent = Color(FloatingButtonStyle.accent)

    var body: some View {
        RoundedRectangle(cornerRadius: 18, style: .continuous)
            .fill(LinearGradient(
                colors: [Color(white: 0.22), Color(white: 0.08)],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            ))
            .overlay {
                // The game, letterboxed.
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color.white.opacity(0.06))
                    .padding(.horizontal, 56)
                    .padding(.vertical, 10)
            }
            .overlay(alignment: .topTrailing) {
                if enabled {
                    button(symbol: "moon.stars.fill", size: 34, fill: Self.accent.opacity(0.7))
                        .opacity(opacity)
                        .padding(10)
                }
            }
            .overlay(alignment: .bottomTrailing) {
                if let action = actions.first {
                    button(symbol: action.symbol, size: 30, fill: Color.black.opacity(0.55))
                        .opacity(opacity)
                        .padding(10)
                }
            }
            .overlay(alignment: .bottomLeading) {
                if actions.count > 1 {
                    button(symbol: actions[1].symbol, size: 30, fill: Color.black.opacity(0.55))
                        .opacity(opacity)
                        .padding(10)
                }
            }
            .aspectRatio(19.5 / 9, contentMode: .fit)
            .padding(.vertical, 4)
            .accessibilityHidden(true)
    }

    private func button(symbol: String, size: CGFloat, fill: Color) -> some View {
        Image(systemName: symbol)
            .font(.system(size: size * 0.42, weight: .semibold))
            .foregroundStyle(.white)
            .frame(width: size, height: size)
            .background(Circle().fill(fill))
            .overlay(Circle().strokeBorder(Color.white.opacity(0.18)))
    }
}
