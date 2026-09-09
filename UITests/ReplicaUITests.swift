import XCTest

final class ReplicaUITests: XCTestCase {
    func testLibrarySettingsAndLandscapeMenu() {
        let app = XCUIApplication()
        app.launchArguments = ["--demo"]
        app.launch()
        XCTAssertTrue(app.staticTexts["游戏库"].firstMatch.waitForExistence(timeout: 10))
        attach("01-library")
        app.buttons["显示方式"].tap()
        app.buttons["列表视图"].tap()
        XCTAssertTrue(app.buttons["运行 青空下的加缪"].exists)
        let search = app.searchFields["搜索游戏"]
        search.tap(); search.typeText("no-match")
        XCTAssertTrue(app.staticTexts["没有找到游戏"].exists)
        search.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: 8))
        app.buttons["设置"].tap()
        XCTAssertTrue(app.staticTexts["渲染方式"].waitForExistence(timeout: 5))
        attach("02-settings")
        app.buttons["游戏库"].tap()
        app.buttons["继续上次：青空下的加缪"].tap()
        XCUIDevice.shared.orientation = .landscapeLeft
        XCTAssertTrue(app.buttons["关闭菜单"].waitForExistence(timeout: 5))
        attach("03-player-menu")
        app.buttons["关闭菜单"].tap()
        app.buttons["唤出游戏菜单"].tap()
        app.buttons["退出"].tap()
        app.buttons["返回游戏库"].tap()
        XCUIDevice.shared.orientation = .portrait
        XCTAssertTrue(app.searchFields["搜索游戏"].waitForExistence(timeout: 5))
    }
    private func attach(_ name: String) {
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name; attachment.lifetime = .keepAlways
        add(attachment)
    }
}
