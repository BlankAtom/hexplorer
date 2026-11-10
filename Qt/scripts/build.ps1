param(
    [string]$QtRoot = (Join-Path $PSScriptRoot '..\..\.tools\Qt\6.8.3\msvc2022_64'),
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$DeployDirectory = (Join-Path $PSScriptRoot '..\dist\HExplorer'),
    [switch]$Test,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qt = (Resolve-Path $QtRoot).Path
$pythonScripts = Join-Path $env:APPDATA 'Python\Python312\Scripts'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) { $cmake = $cmake.Source } else { $cmake = Join-Path $pythonScripts 'cmake.exe' }
$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if ($ninja) { $ninja = $ninja.Source } else { $ninja = Join-Path $pythonScripts 'ninja.exe' }
if (!(Test-Path $cmake) -or !(Test-Path $ninja)) {
    throw 'Install CMake and Ninja, then add them to PATH.'
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ desktop build tools are required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $root 'build'
$commands = @(
    "set `"PATH=$(Split-Path $vswhere);%PATH%`"",
    "call `"$vcvars`" >nul",
    "`"$cmake`" -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_PREFIX_PATH=`"$qt`" -DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_INSTALL_BINDIR:STRING=. -DHEXPLORER_CORE_ONLY=OFF -DBUILD_TESTING=ON",
    "`"$cmake`" --build `"$build`" --parallel 4"
)
if ($Test) {
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    $commands += "set `"PATH=$qt\bin;%PATH%`""
    $commands += "`"$ctest`" --test-dir `"$build`" --output-on-failure"
}
if ($Package) {
    $cpack = Join-Path (Split-Path $cmake) 'cpack.exe'
    $deploy = [IO.Path]::GetFullPath($DeployDirectory)
    $commands += "`"$cmake`" --install `"$build`" --prefix `"$deploy`""
    $commands += "`"$cpack`" --config `"$build\CPackConfig.cmake`" -B `"$root\dist`""
}
& $env:ComSpec /d /c ($commands -join ' && ')
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
