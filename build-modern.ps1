# Compila MicroSIP Modern (host Win32 + WebView2) y prepara dist\MicroSIP-Modern.
#
# Requisitos: VS 2022 Build Tools (v143), pjproject compilado en
# ..\pjproject-2.15.1 (build-microsip.ps1 lo hace) y el SDK de WebView2 en
# ..\webview2-sdk (lo descarga este script si falta).

param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostDir = Join-Path $root 'MicroSIP-Modern\host'
$pj = Join-Path $root 'pjproject-2.15.1'
$sdk = Join-Path $root 'webview2-sdk'
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'

# No incrustar rutas locales del equipo de compilacion en los binarios.
$env:CL = "/experimental:deterministic /pathmap:`"$root`"=. $env:CL".Trim()

if (-not (Test-Path $msbuild)) { throw "No se encuentra MSBuild en $msbuild" }

$pjLib = Join-Path $pj 'lib\libpjproject-i386-Win32-vc14-Release-Static.lib'
if (-not (Test-Path $pjLib)) {
    throw "Falta pjproject compilado. Ejecuta antes .\build-microsip.ps1"
}

if (-not (Test-Path (Join-Path $sdk 'build\native\include\WebView2.h'))) {
    Write-Host 'Descargando SDK de WebView2...'
    $nupkg = Join-Path $root 'webview2.nupkg'
    $zip = Join-Path $root 'webview2.zip'
    if (-not (Test-Path $nupkg)) {
        Invoke-WebRequest -Uri 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40' -OutFile $nupkg -UseBasicParsing
    }
    Copy-Item $nupkg $zip -Force
    Expand-Archive -Path $zip -DestinationPath $sdk -Force
}

Write-Host "== Compilando MicroSIP Modern ($Configuration|Win32) =="
& $msbuild (Join-Path $hostDir 'MicroSIPModern.vcxproj') `
    /p:Configuration=$Configuration /p:Platform=Win32 /m /v:m /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild fallo ($LASTEXITCODE)" }

$out = Join-Path $hostDir "$Configuration"
$outWeb = Join-Path $out 'web'
if (Test-Path $outWeb) { Remove-Item -Recurse -Force $outWeb }
Copy-Item (Join-Path $hostDir 'web') $outWeb -Recurse -Force

# Tono de llamada entrante (el mismo WAV que usa la app original).
$ringtone = Join-Path $root 'assets\ringtone.wav'
if (Test-Path $ringtone) { Copy-Item $ringtone $out -Force }

$jabra = Join-Path $root 'dist\MicroSIP-Jabra\JabraBridge'
$outJabra = Join-Path $out 'JabraBridge'
if (Test-Path $outJabra) { Remove-Item -Recurse -Force $outJabra }
if (Test-Path $jabra) { Copy-Item $jabra $outJabra -Recurse -Force }

$dist = Join-Path $root 'dist\MicroSIP-Modern'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $out 'MicroSIPModern.exe') $dist -Force
Copy-Item $outWeb (Join-Path $dist 'web') -Recurse -Force
if (Test-Path (Join-Path $out 'ringtone.wav')) { Copy-Item (Join-Path $out 'ringtone.wav') $dist -Force }
if (Test-Path $outJabra) { Copy-Item $outJabra (Join-Path $dist 'JabraBridge') -Recurse -Force }

# Avisos de licencia junto al binario.
Copy-Item (Join-Path $root 'LICENSE') $dist -Force
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.md') $dist -Force
Copy-Item (Join-Path $root 'LICENSES') $dist -Recurse -Force

Write-Host "OK: $(Join-Path $dist 'MicroSIPModern.exe')"
