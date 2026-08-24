import AppKit
import Foundation
import UniformTypeIdentifiers

private struct StrategySpot {
    let key: UInt64
    let probabilities: [Double]
}

private struct LabelKey: Hashable {
    let key: UInt64
    let action: Int
}

private struct SpotLabel {
    let label: String
    let street: String
    let board: String
    let runout: String
    let position: String
    let pot: Double?
    let nextKey: UInt64?
}

private struct SessionEvent: Codable {
    let key: String
    let selected: Int
    let best: Int
    let selectedProbability: Double
    let bestProbability: Double
}

private struct SessionDocument: Codable {
    let schema: String
    let solution: String
    let answered: Int
    let bestAnswers: Int
    let probabilityLoss: Double
    let events: [SessionEvent]
}

private enum TrainerLoadError: LocalizedError {
    case invalidSolution(String)
    case unreadableLabels

    var errorDescription: String? {
        switch self {
        case .invalidSolution(let message): return message
        case .unreadableLabels: return "Impossible de lire le fichier de labels CSV."
        }
    }
}

private final class TrainerData {
    var spots: [StrategySpot] = []
    var labels: [LabelKey: SpotLabel] = [:]
    var solutionPath = ""
    var labelsPath = ""

    func loadSolution(_ url: URL) throws {
        let data = try Data(contentsOf: url)
        guard data.count >= 32,
              String(data: data.subdata(in: 0..<8), encoding: .ascii) == "PESOL001"
        else {
            throw TrainerLoadError.invalidSolution("Le fichier n'est pas un .pe_sol valide.")
        }
        let version = readUInt32(data, at: 8)
        guard version == 1 else {
            throw TrainerLoadError.invalidSolution("Version .pe_sol non supportée : \(version).")
        }
        let count = readUInt64(data, at: 16)
        guard count <= UInt64(Int.max) else {
            throw TrainerLoadError.invalidSolution("Le nombre d'infosets est trop grand.")
        }
        var offset = 32
        var loaded: [StrategySpot] = []
        loaded.reserveCapacity(Int(count))
        for _ in 0..<Int(count) {
            guard offset + 12 <= data.count else {
                throw TrainerLoadError.invalidSolution(".pe_sol tronqué pendant la lecture des infosets.")
            }
            let key = readUInt64(data, at: offset)
            let actionCount = Int(readUInt32(data, at: offset + 8))
            offset += 12
            guard actionCount > 0, actionCount <= 256,
                  actionCount <= (data.count - offset) / 2 else {
                throw TrainerLoadError.invalidSolution("Nombre d'actions invalide pour l'infoset 0x\(String(key, radix: 16)).")
            }
            var probabilities: [Double] = []
            probabilities.reserveCapacity(actionCount)
            for action in 0..<actionCount {
                let quantized = readUInt16(data, at: offset + action * 2)
                probabilities.append(Double(quantized) / 65535.0)
            }
            offset += actionCount * 2
            loaded.append(StrategySpot(key: key, probabilities: probabilities))
        }
        spots = loaded
        solutionPath = url.path
    }

    func loadLabels(_ url: URL) throws {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            throw TrainerLoadError.unreadableLabels
        }
        var loaded: [LabelKey: SpotLabel] = [:]
        for rawLine in text.split(whereSeparator: \.isNewline) {
            let fields = rawLine.split(separator: ",", omittingEmptySubsequences: false).map(String.init)
            guard fields.count >= 3, fields[0].lowercased() != "key",
                  let key = parseUInt64(fields[0]) else { continue }
            var action = 0
            var label = ""
            var street = ""
            var board = ""
            var runout = ""
            var position = ""
            var pot: Double?
            var nextKey: UInt64?
            if fields.count >= 9 {
                street = fields[1]; board = fields[2]; runout = fields[3]; position = fields[4]
                pot = Double(fields[5]); action = Int(fields[6]) ?? 0; label = fields[7]
                nextKey = fields[8].isEmpty ? nil : parseUInt64(fields[8])
            } else if fields.count >= 5 {
                street = fields[1]; board = fields[2]; action = Int(fields[3]) ?? 0; label = fields[4]
                nextKey = fields.count >= 6 && !fields[5].isEmpty ? parseUInt64(fields[5]) : nil
            } else {
                action = Int(fields[1]) ?? 0; label = fields[2]
            }
            loaded[LabelKey(key: key, action: action)] = SpotLabel(
                label: label, street: street, board: board, runout: runout,
                position: position, pot: pot, nextKey: nextKey)
        }
        labels = loaded
        labelsPath = url.path
    }

    func label(for key: UInt64, action: Int) -> SpotLabel? {
        labels[LabelKey(key: key, action: action)]
    }

    func metadata(for key: UInt64) -> SpotLabel? {
        labels.first(where: { $0.key.key == key })?.value
    }
}

private func readUInt16(_ data: Data, at offset: Int) -> UInt16 {
    UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
}

private func readUInt32(_ data: Data, at offset: Int) -> UInt32 {
    UInt32(data[offset]) | (UInt32(data[offset + 1]) << 8) |
        (UInt32(data[offset + 2]) << 16) | (UInt32(data[offset + 3]) << 24)
}

private func readUInt64(_ data: Data, at offset: Int) -> UInt64 {
    var value: UInt64 = 0
    for index in 0..<8 { value |= UInt64(data[offset + index]) << UInt64(index * 8) }
    return value
}

private func parseUInt64(_ text: String) -> UInt64? {
    let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
    if trimmed.lowercased().hasPrefix("0x") {
        return UInt64(trimmed.dropFirst(2), radix: 16)
    }
    return UInt64(trimmed)
}

private func makeLabel(_ text: String, size: CGFloat = 13, color: NSColor = .labelColor) -> NSTextField {
    let label = NSTextField(labelWithString: text)
    label.font = .systemFont(ofSize: size)
    label.textColor = color
    label.lineBreakMode = .byTruncatingTail
    return label
}

private final class TrainerWindowController: NSWindowController {
    private let data = TrainerData()
    private var current: StrategySpot?
    private var currentAnswered = false
    private var rng = SystemRandomNumberGenerator()
    private var score = 0
    private var answered = 0
    private var streak = 0
    private var difficulty = 1
    private var probabilityLoss = 0.0
    private var events: [SessionEvent] = []

    private let solutionLabel = makeLabel("Aucune solution chargée", color: .secondaryLabelColor)
    private let labelsLabel = makeLabel("Labels : aucun", size: 12, color: .secondaryLabelColor)
    private let spotLabel = makeLabel("Ouvrez un fichier .pe_sol pour commencer", size: 18)
    private let contextLabel = makeLabel("", size: 13, color: .secondaryLabelColor)
    private let feedbackLabel = makeLabel("", size: 14)
    private let scoreLabel = makeLabel("Score : 0 / 0 · Difficulté : 1", size: 13, color: .secondaryLabelColor)
    private let actionStack = NSStackView()
    private let nextButton = NSButton(title: "Spot suivant", target: nil, action: nil)

    init() {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 500),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "poker-eval Trainer"
        window.center()
        super.init(window: window)
        window.contentView = makeContentView()
        window.isReleasedWhenClosed = false
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func loadArguments(_ arguments: [String]) {
        var index = 1
        while index + 1 < arguments.count {
            if arguments[index] == "--solution" {
                loadSolution(URL(fileURLWithPath: arguments[index + 1])); index += 2
            } else if arguments[index] == "--labels" {
                loadLabels(URL(fileURLWithPath: arguments[index + 1])); index += 2
            } else { index += 1 }
        }
    }

    private func makeContentView() -> NSView {
        let root = NSStackView()
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 14
        root.edgeInsets = NSEdgeInsets(top: 22, left: 24, bottom: 22, right: 24)

        let title = makeLabel("poker-eval Trainer", size: 26)
        root.addArrangedSubview(title)
        root.addArrangedSubview(makeLabel("Play-vs-solution natif · vos fichiers restent sur cette machine", size: 13, color: .secondaryLabelColor))

        let fileBar = NSStackView()
        fileBar.orientation = .vertical
        fileBar.alignment = .leading
        fileBar.spacing = 5
        let solutionButton = NSButton(title: "Ouvrir une solution .pe_sol…", target: self, action: #selector(openSolution))
        let labelsButton = NSButton(title: "Ouvrir les labels CSV…", target: self, action: #selector(openLabels))
        let buttons = NSStackView(views: [solutionButton, labelsButton])
        buttons.spacing = 8
        fileBar.addArrangedSubview(buttons)
        fileBar.addArrangedSubview(solutionLabel)
        fileBar.addArrangedSubview(labelsLabel)
        root.addArrangedSubview(fileBar)

        let separator = NSBox()
        separator.boxType = .separator
        root.addArrangedSubview(separator)
        root.addArrangedSubview(spotLabel)
        root.addArrangedSubview(contextLabel)

        actionStack.orientation = .horizontal
        actionStack.alignment = .centerY
        actionStack.spacing = 9
        root.addArrangedSubview(actionStack)
        root.addArrangedSubview(feedbackLabel)
        root.addArrangedSubview(scoreLabel)

        nextButton.target = self
        nextButton.action = #selector(nextSpot)
        nextButton.keyEquivalent = " "
        root.addArrangedSubview(nextButton)
        return root
    }

    @objc func openSolution() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "pe_sol") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { loadSolution(url) }
    }

    @objc func openLabels() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType.commaSeparatedText, .plainText]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { loadLabels(url) }
    }

    private func loadSolution(_ url: URL) {
        do {
            try data.loadSolution(url)
            solutionLabel.stringValue = "Solution : \(url.lastPathComponent) · \(data.spots.count) infosets"
            resetSession()
            nextSpot()
        } catch { showError(error) }
    }

    private func loadLabels(_ url: URL) {
        do {
            try data.loadLabels(url)
            labelsLabel.stringValue = "Labels : \(url.lastPathComponent) · \(data.labels.count) actions"
            if current != nil { renderCurrent() }
        } catch { showError(error) }
    }

    private func resetSession() {
        score = 0; answered = 0; streak = 0; difficulty = 1; probabilityLoss = 0; events = []
        current = nil; currentAnswered = false
        updateScore()
    }

    @objc private func nextSpot() {
        guard !data.spots.isEmpty else {
            spotLabel.stringValue = "Ouvrez un fichier .pe_sol pour commencer"
            return
        }
        let hardSpots = difficulty > 1 ? data.spots.filter { ($0.probabilities.max() ?? 1) < 0.8 } : data.spots
        let pool = hardSpots.isEmpty ? data.spots : hardSpots
        current = pool.randomElement(using: &rng)
        currentAnswered = false
        renderCurrent()
    }

    private func renderCurrent() {
        guard let spot = current else { return }
        spotLabel.stringValue = "Infoset 0x\(String(format: "%016llx", spot.key))"
        if let meta = data.metadata(for: spot.key) {
            var values = [meta.street, meta.board, meta.runout, meta.position].filter { !$0.isEmpty }
            if let pot = meta.pot { values.append(String(format: "pot %.2f", pot)) }
            contextLabel.stringValue = values.isEmpty ? "" : values.joined(separator: "  ·  ")
        } else {
            contextLabel.stringValue = "Métadonnées de spot non fournies"
        }
        if !currentAnswered {
            feedbackLabel.stringValue = "Choisissez l'action que vous joueriez."
            feedbackLabel.textColor = .labelColor
        }
        for view in actionStack.arrangedSubviews { actionStack.removeArrangedSubview(view); view.removeFromSuperview() }
        for (action, probability) in spot.probabilities.enumerated() {
            let label = data.label(for: spot.key, action: action)?.label ?? "Action \(action)"
            let button = NSButton(title: "\(label)  ·  \(String(format: "%.1f", probability * 100))%",
                                  target: self, action: #selector(actionPressed(_:)))
            button.tag = action
            button.bezelStyle = .rounded
            button.controlSize = .large
            button.isEnabled = !currentAnswered
            actionStack.addArrangedSubview(button)
        }
        updateScore()
    }

    @objc private func actionPressed(_ sender: NSButton) {
        guard let spot = current, !currentAnswered else { return }
        let best = spot.probabilities.indices.max { spot.probabilities[$0] < spot.probabilities[$1] } ?? 0
        answered += 1
        currentAnswered = true
        let selectedProbability = spot.probabilities[sender.tag]
        let bestProbability = spot.probabilities[best]
        events.append(SessionEvent(key: String(format: "0x%016llx", spot.key), selected: sender.tag,
                                   best: best, selectedProbability: selectedProbability,
                                   bestProbability: bestProbability))
        if sender.tag == best {
            score += 1; streak += 1
            feedbackLabel.stringValue = "Correct · meilleure action : \(String(format: "%.1f", bestProbability * 100))%"
            feedbackLabel.textColor = .systemGreen
        } else {
            streak = 0; probabilityLoss += bestProbability - selectedProbability
            let bestLabel = data.label(for: spot.key, action: best)?.label ?? "Action \(best)"
            feedbackLabel.stringValue = "À revoir : \(bestLabel) · \(String(format: "%.1f", bestProbability * 100))%"
            feedbackLabel.textColor = .systemOrange
        }
        if streak >= 3 { difficulty = min(5, difficulty + 1) }
        if streak == 0 { difficulty = max(1, difficulty - 1) }
        renderCurrent()
    }

    private func updateScore() {
        scoreLabel.stringValue = "Score : \(score) / \(answered)  ·  Difficulté : \(difficulty)"
    }

    @objc func exportSession() {
        guard !data.solutionPath.isEmpty else { showError(TrainerLoadError.invalidSolution("Chargez d'abord une solution.")); return }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "trainer-session.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let document = SessionDocument(schema: "pe-trainer-session/v1", solution: data.solutionPath,
                                       answered: answered, bestAnswers: score,
                                       probabilityLoss: probabilityLoss, events: events)
        do { try JSONEncoder().encode(document).write(to: url) } catch { showError(error) }
    }

    private func showError(_ error: Error) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "poker-eval Trainer"
        alert.informativeText = error.localizedDescription
        alert.runModal()
    }
}

@main
private final class TrainerAppDelegate: NSObject, NSApplicationDelegate {
    private var windowController: TrainerWindowController!

    func applicationDidFinishLaunching(_ notification: Notification) {
        windowController = TrainerWindowController()
        windowController.loadArguments(CommandLine.arguments)
        windowController.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationWillFinishLaunching(_ notification: Notification) {
        let menu = NSMenu()
        let appMenuItem = NSMenuItem()
        menu.addItem(appMenuItem)
        let appMenu = NSMenu()
        appMenuItem.submenu = appMenu
        appMenu.addItem(withTitle: "Quitter poker-eval Trainer", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        let fileItem = NSMenuItem()
        menu.addItem(fileItem)
        let fileMenu = NSMenu(title: "Fichier")
        fileItem.submenu = fileMenu
        fileMenu.addItem(withTitle: "Ouvrir une solution…", action: #selector(TrainerWindowController.openSolution), keyEquivalent: "o")
        fileMenu.addItem(withTitle: "Exporter la session JSON…", action: #selector(TrainerWindowController.exportSession), keyEquivalent: "e")
        NSApp.mainMenu = menu
    }
}
