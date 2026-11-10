param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\..\.tools\InnoSetup')
)
$ErrorActionPreference = 'Stop'
$version = '6.7.4'
$destinationPath = [IO.Path]::GetFullPath($Destination)
$compiler = Join-Path $destinationPath 'ISCC.exe'
if ((Test-Path -LiteralPath $compiler) -and (Get-Item $compiler).VersionInfo.ProductVersion -like "$version*") {
    Write-Output $compiler
    return
}
$download = Join-Path ([IO.Path]::GetTempPath()) "HExplorer-InnoSetup-$([guid]::NewGuid()).exe"
try {
    Invoke-WebRequest -Uri "https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-$version.exe" -OutFile $download
    $signature = Get-AuthenticodeSignature -LiteralPath $download
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|, )CN=Pyrsys B\.V\.(,|$)') {
        throw 'Inno Setup publisher signature verification failed. The downloaded program will not be run.'
    }
    $process = Start-Process -FilePath $download -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR=`"$destinationPath`"" -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Inno Setup compiler installation failed: $($process.ExitCode)" }
    if (!(Test-Path -LiteralPath $compiler)) { throw 'Inno Setup did not install its compiler.' }
    Write-Output $compiler
}
finally {
    if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download }
}
