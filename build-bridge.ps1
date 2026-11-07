# Compila y publica JabraBridge junto al ejecutable de MicroSIP.
#
# Requiere el SDK de .NET (probado con .NET SDK 9) y conexion a NuGet.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $root 'integration\JabraBridge\JabraBridge.csproj'
$output = Join-Path $root 'dist\MicroSIP-Jabra\JabraBridge'

Write-Host "Publicando JabraBridge en $output ..."
dotnet publish $project -c Release -f net472 -o $output --nologo

if ($LASTEXITCODE -ne 0) {
    throw "La publicacion fallo con codigo $LASTEXITCODE"
}

# Avisos de licencia junto al binario (GPL de MicroSIP + Apache-2.0 del puente).
$pkg = Split-Path -Parent $output
Copy-Item (Join-Path $root 'LICENSE') $pkg -Force
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.md') $pkg -Force
Copy-Item (Join-Path $root 'LICENSES') $pkg -Recurse -Force
Copy-Item (Join-Path $root 'integration\JabraBridge\NOTICE') $output -Force

Write-Host ''
Write-Host 'Listo. La carpeta dist\MicroSIP-Jabra contiene:'
Write-Host '  microsip.exe'
Write-Host '  JabraBridge\JabraBridge.exe'
Write-Host '  JabraBridge\Jabra.Utilities\...'
Write-Host '  LICENSE, THIRD-PARTY-NOTICES.md y LICENSES\'
