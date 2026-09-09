import XCTest

final class ReplicaUITests: XCTestCase {
    func testLibraryAndSettingsScreenshots() {
        let app = XCUIApplication()
        app.launchArguments = ["--krkr-smoke"]
        app.launch()
        XCTAssertTrue(app.staticTexts["游戏库"].firstMatch.waitForExistence(timeout: 15))
        attach("01-library")

        app.buttons["显示方式"].tap()
        app.buttons["列表视图"].tap()
        XCTAssertTrue(app.buttons["运行 KRKR Smoke A"].exists)

        let search = app.searchFields["搜索游戏"]
        search.tap()
        search.typeText("no-match")
        XCTAssertTrue(app.staticTexts["没有找到游戏"].exists)
        search.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: 8))

        app.buttons["设置"].tap()
        XCTAssertTrue(app.staticTexts["渲染方式"].waitForExistence(timeout: 5))
        attach("02-settings")
        app.buttons["游戏库"].tap()
        XCTAssertTrue(app.searchFields["搜索游戏"].waitForExistence(timeout: 5))
    }

    func testKRKRRuntimeCanRestartInOneProcess() {
        let app = XCUIApplication()
        app.launchArguments = ["--krkr-smoke"]
        app.launch()
        XCTAssertTrue(app.staticTexts["游戏库"].firstMatch.waitForExistence(timeout: 15))

        for title in ["KRKR Smoke A", "KRKR Smoke B", "KRKR Smoke A"] {
            let launch = app.buttons["运行 \(title)"]
            XCTAssertTrue(launch.waitForExistence(timeout: 15), "Missing smoke game: \(title)")
            launch.tap()
            XCTAssertTrue(
                app.staticTexts["游戏库"].firstMatch.waitForExistence(timeout: 30),
                "KRKR did not return to the library after running \(title)"
            )
            XCTAssertFalse(app.alerts.firstMatch.waitForExistence(timeout: 2))
        }
    }

    private func attach(_ name: String) {
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}
