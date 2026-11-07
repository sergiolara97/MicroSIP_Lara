# Compila MicroSIP (variante sin video ni codecs externos) sobre pjproject 2.15.1.
#
# MicroSIP necesita estar dentro del arbol de pjproject para resolver
# ..\pjlib\include, ..\build\vs\pjproject-vs14-common-config.props, etc.
# Ademas, el proyecto original enlaza contra third_party (ffmpeg, vpx, SDL2,
# openssl, silk, ipp, opus, bcg729, amr) que MicroSIP no publica. Este script
# prepara una copia del codigo y la adapta para compilar con los codecs y el
# video que trae pjproject de serie, anadiendo SQLiteCpp desde fuente.
#
# Resultado: pjproject-2.15.1\microsip\Release\microsip.exe

param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'Win32'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root 'MicroSIP-3.22.16-src'
$pj = Join-Path $root 'pjproject-2.15.1'
$dst = Join-Path $pj 'microsip'
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$platformVersion = '10.0.22621.0'
$toolset = 'v143'

# No incrustar rutas locales del equipo de compilacion en los binarios.
$env:CL = "/experimental:deterministic /pathmap:`"$root`"=. $env:CL".Trim()

if (-not (Test-Path $msbuild)) { throw "No se encuentra MSBuild en $msbuild" }

function Get-Zip([string]$url, [string]$file) {
    if (-not (Test-Path $file)) {
        Write-Host "Descargando $url"
        Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing
    }
}

# --- 0. pjproject + SQLiteCpp + sqlite3 ---
$pjLib = Join-Path $pj 'lib\libpjproject-i386-Win32-vc14-Release-Static.lib'

if (-not (Test-Path $pjLib)) {
    $pjZip = Join-Path $root 'pjproject-2.15.1.zip'
    Get-Zip 'https://github.com/pjsip/pjproject/archive/refs/tags/2.15.1.zip' $pjZip
    if (-not (Test-Path $pj)) {
        Expand-Archive -Path $pjZip -DestinationPath $root -Force
    }

    # pjproject exige un config_site.h
    $site = Join-Path $pj 'pjlib\include\pj\config_site.h'
    if (-not (Test-Path $site)) {
        Copy-Item (Join-Path $pj 'pjlib\include\pj\config_site_sample.h') $site
    }

    Write-Host '== Compilando pjproject (Release-Static|Win32) =='
    & $msbuild (Join-Path $pj 'pjproject-vs14.sln') `
        /p:Configuration=Release-Static /p:Platform=Win32 `
        /p:BuildToolset=$toolset /p:PlatformVersion=$platformVersion `
        /m /v:m /nologo
    if ($LASTEXITCODE -ne 0) { throw "pjproject fallo ($LASTEXITCODE)" }
}

# SQLiteCpp (wrapper C++) + amalgama de sqlite3
$sqCppDir = Join-Path $pj 'third_party\SQLiteCpp'
$sq3Dir = Join-Path $pj 'third_party\sqlite3'

if (-not (Test-Path (Join-Path $sqCppDir 'src\Database.cpp'))) {
    $zip = Join-Path $root 'sqlitecpp.zip'
    Get-Zip 'https://github.com/SRombauts/SQLiteCpp/archive/refs/tags/3.3.2.zip' $zip
    $tmp = Join-Path $env:TEMP 'microsip_sqlitecpp'
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $sqCppDir 'include'), (Join-Path $sqCppDir 'src') | Out-Null
    Copy-Item (Join-Path $tmp 'SQLiteCpp-3.3.2\include\SQLiteCpp') (Join-Path $sqCppDir 'include') -Recurse -Force
    Copy-Item (Join-Path $tmp 'SQLiteCpp-3.3.2\src\*.cpp') (Join-Path $sqCppDir 'src') -Force
}

if (-not (Test-Path (Join-Path $sq3Dir 'sqlite3.c'))) {
    $zip = Join-Path $root 'sqlite-amalgamation.zip'
    Get-Zip 'https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip' $zip
    $tmp = Join-Path $env:TEMP 'microsip_sqlite3'
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    New-Item -ItemType Directory -Force -Path $sq3Dir | Out-Null
    Copy-Item (Join-Path $tmp 'sqlite-amalgamation-3450100\sqlite3.c') $sq3Dir -Force
    Copy-Item (Join-Path $tmp 'sqlite-amalgamation-3450100\sqlite3.h') $sq3Dir -Force
    Copy-Item (Join-Path $tmp 'sqlite-amalgamation-3450100\sqlite3ext.h') $sq3Dir -Force
}

# --- 1. Copiar fuentes ---
Write-Host "== Copiando fuentes a $dst =="
robocopy $src $dst /MIR /XD .git /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fallo ($LASTEXITCODE)" }

# --- 2. Desactivar video (evita ffmpeg, vpx, SDL2, x264) ---
$constH = Join-Path $dst 'const.h'
$text = [IO.File]::ReadAllText($constH)
if ($text -notmatch '//\s*#define _GLOBAL_VIDEO') {
    $text = $text -replace '(?m)^#define _GLOBAL_VIDEO\r?$', '//#define _GLOBAL_VIDEO'
    [IO.File]::WriteAllText($constH, $text, (New-Object Text.UTF8Encoding($false)))
    Write-Host 'const.h: video desactivado'
}

# --- 3. Adaptar el proyecto ---
$vcx = Join-Path $dst 'microsip.vcxproj'
$text = [IO.File]::ReadAllText($vcx)

$text = $text -replace '\.\.\\third_party\\sdl\\include;\.\.\\third_party\\SQLiteCpp;\.\.\\third_party\\openssl\\Win32\\include;\.\.\\third_party;', ''
$text = $text -replace 'c:\\Program Files \(x86\)\\Windows Kits\\10\\Include\\10\.0\.19041\.0\\winrt;', ''
$text = $text -replace '(\.\.\\lib\\;)(?:\.\.\\third_party\\[^;]*;)+', '$1'
$text = $text -replace 'chkstk_ms\.lib;bcg729\.lib;opus\.lib;', ''

# Include dirs de SQLiteCpp/sqlite3
$text = $text -replace '(\.\.\\pjlib-util\\include;)', '$1..\third_party\SQLiteCpp\include;..\third_party\sqlite3;'

$sqliteSources = @(
    '..\third_party\sqlite3\sqlite3.c',
    '..\third_party\SQLiteCpp\src\Backup.cpp',
    '..\third_party\SQLiteCpp\src\Column.cpp',
    '..\third_party\SQLiteCpp\src\Database.cpp',
    '..\third_party\SQLiteCpp\src\Exception.cpp',
    '..\third_party\SQLiteCpp\src\Savepoint.cpp',
    '..\third_party\SQLiteCpp\src\Statement.cpp',
    '..\third_party\SQLiteCpp\src\Transaction.cpp'
)

$entries = ($sqliteSources | ForEach-Object {
    $name = [IO.Path]::GetFileNameWithoutExtension($_)
    "    <ClCompile Include=`"$_`"><PrecompiledHeader>NotUsing</PrecompiledHeader><ObjectFileName>`$(IntDir)ext_$name.obj</ObjectFileName></ClCompile>"
}) -join "`r`n"

$extraGroup = "  <ItemGroup>`r`n$entries`r`n  </ItemGroup>`r`n  <Import Project=`"`$(VCTargetsPath)\Microsoft.Cpp.targets`" />"
$text = $text -replace '  <Import Project="\$\(VCTargetsPath\)\\Microsoft\.Cpp\.targets" />', $extraGroup

[IO.File]::WriteAllText($vcx, $text, (New-Object Text.UTF8Encoding($false)))
Write-Host 'microsip.vcxproj: adaptado (sin third_party, con SQLiteCpp)'

# Database.cpp pide enlazar SQLiteCpp.lib/sqlite3.lib; aqui se compilan sus fuentes.
$dbCpp = Join-Path $dst 'Data\Database.cpp'
$dbText = [IO.File]::ReadAllText($dbCpp)
$dbText = $dbText -replace '(?m)^#pragma comment\(lib, "SQLiteCpp\.lib"\)', '//#pragma comment(lib, "SQLiteCpp.lib")'
$dbText = $dbText -replace '(?m)^#pragma comment\(lib, "sqlite3\.lib"\)', '//#pragma comment(lib, "sqlite3.lib")'
[IO.File]::WriteAllText($dbCpp, $dbText, (New-Object Text.UTF8Encoding($false)))

# --- 4. Compilar ---
Write-Host "== Compilando MicroSIP ($Configuration|$Platform) =="
& $msbuild (Join-Path $dst 'microsip.vcxproj') `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:BuildToolset=$toolset `
    /p:PlatformVersion=$platformVersion `
    /m /v:m /nologo

if ($LASTEXITCODE -ne 0) { throw "MSBuild fallo ($LASTEXITCODE)" }

$exe = Get-ChildItem $dst -Recurse -Filter microsip.exe -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($exe) { Write-Host "OK: $($exe.FullName)" } else { Write-Host 'Compilado, pero no se encontro microsip.exe' }

# Traduccion al espanol (MicroSIP carga langpack_*.txt junto al exe)
$lang = Join-Path $root 'assets\langpack_spanish.txt'
if ((Test-Path $lang) -and $exe) {
    Copy-Item $lang (Join-Path $exe.Directory.FullName 'langpack_spanish.txt') -Force
    Write-Host 'langpack_spanish.txt copiado junto al ejecutable'
}
