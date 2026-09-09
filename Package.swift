// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "VNCore",
    platforms: [.macOS(.v13), .iOS(.v16)],
    products: [.library(name: "VNCore", targets: ["VNCore"])],
    targets: [
        .target(name: "VNCore", path: "Sources/VNCore"),
        .testTarget(name: "VNCoreTests", dependencies: ["VNCore"], path: "Tests/VNCoreTests")
    ]
)
