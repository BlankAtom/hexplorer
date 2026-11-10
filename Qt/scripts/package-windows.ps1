param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '..\dist\HExplorer'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Iscc = '',
    [string]$ExpectedTag = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION.txt') -Raw).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
    throw 'VERSION must contain major.minor.patch.'
}
if ($ExpectedTag -and $ExpectedTag -cne "v$version") {
    throw "Tag $ExpectedTag does not match VERSION v$version."
}
$source = (Resolve-Path $SourceDirectory).Path
$exe = Join-Path $source 'HExplorer.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing deployed executable: $exe" }
$actual = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($actual -ne $version) { throw "Deployed EXE version $actual does not match VERSION $version." }
if (!$Iscc) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $Iscc = $command.Source }
    elseif (Test-Path "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") {
        $Iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    } elseif (Test-Path (Join-Path $root '..\.tools\InnoSetup\ISCC.exe')) {
        $Iscc = (Resolve-Path (Join-Path $root '..\.tools\InnoSetup\ISCC.exe')).Path
    } else { throw 'Inno Setup 6 is required. Install it or pass -Iscc with the compiler path.' }
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
& $Iscc "/DAppVersion=$version" "/DSourceDir=$source" "/DOutputDir=$output" (Join-Path $root 'packaging\windows.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
$installer = Join-Path $output "HExplorer-$version-windows-x64-setup.exe"
if (!(Test-Path -LiteralPath $installer)) { throw 'The Windows installer was not generated.' }
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$installer.sha256", "$hash  $([IO.Path]::GetFileName($installer))`n", [Text.UTF8Encoding]::new($false))
Write-Output "Installer: $installer"
