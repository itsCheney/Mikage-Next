$ErrorActionPreference='Stop'
$diagRoot='D:/vn-sim'
$diagCore=(Resolve-Path "$diagRoot/Engine/KRKRRuntime/Source/cpp").Path
$diagLog="$diagRoot/build/native-metal-p2c-c4-baseline/production-syntax.log"
$diagCompiler='C:/Strawberry/c/bin/c++.exe'
Set-Content -LiteralPath $diagLog -Value 'Production C++ translation-unit syntax checks. No extraction or GPU/device simulation.' -Encoding utf8
Add-Content -LiteralPath $diagLog -Value "Working directory: $diagRoot" -Encoding utf8
& $diagCompiler --version 2>&1 | Add-Content -LiteralPath $diagLog -Encoding utf8
Push-Location $diagRoot
try {
    foreach($diagSource in @('core/render/TVPTrans.cpp','core/script/tjsNativeLayer.cpp')) {
        $diagCompileArgs=@('-std=c++17','-fsyntax-only','-DTJS_NO_REGEXP=1','-D_ONLYCONSOLE=1','-D_KRKRSDL3_WINDOWS=1','-ITests/EmotePerformance/NodeStubs')
        foreach($diagDir in @('archive','main','media/font','media/image','media/movie','media/sound','msg','render','script','utils','utils/math')) { $diagCompileArgs+='-I'+$diagCore+'/core/'+$diagDir }
        foreach($diagDir in @('tjs2','environ','plugins')) { $diagCompileArgs+='-I'+$diagCore+'/'+$diagDir }
        $diagCompileArgs+=$diagCore+'/'+$diagSource
        Add-Content -LiteralPath $diagLog -Value ('COMMAND: & '+$diagCompiler+' '+($diagCompileArgs -join ' ')) -Encoding utf8
        & $diagCompiler @diagCompileArgs 2>&1 | Add-Content -LiteralPath $diagLog -Encoding utf8
        $diagExit=$LASTEXITCODE
        Add-Content -LiteralPath $diagLog -Value "EXIT: $diagExit" -Encoding utf8
        if($diagExit -ne 0) {throw "Production syntax check failed: $diagSource"}
    }
} finally {Pop-Location}
Write-Output "PASS production syntax: TVPTrans.cpp and tjsNativeLayer.cpp. Full commands/compiler output: $diagLog"
