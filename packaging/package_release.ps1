# package_release.ps1 — build and stage a gbarecomp game for Windows (MinGW).
#
# Current pipeline: raylib is linked statically; stage the executable plus the
# consumer's payload and emit a portable folder/zip. No SDL DLL collection or
# recomp-ui assets are involved.
#
#   .\gbarecompx\packaging\package_release.ps1 `
#       -Target FFTARecomp -Name FFTARecomp -Version v1.7 `
#       -Payload game.toml,bios,mods,assets
#
# Output:
#   release-stage\<name>-<version>-windows-x64\
#   release-stage\<name>-<version>-windows-x64.zip
#
# The consumer owns ROM/BIOS content pins, signing, upload, and the strict
# coverage smoke. This script applies the portable hygiene check (no
# ROM/save/APK extensions) and the PE import guard (no third-party DLLs).
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Name = "",
    [string]$Version = "",
    [string]$BuildDir = "build-release",
    [string]$Root = "",
    [string[]]$Payload = @(),
    [string[]]$PayloadDir = @(),
    [string]$MingwPrefix = "C:\msys64\mingw64",
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"

if (-not $Root) {
    $Root = Resolve-Path (Join-Path $PSScriptRoot "..")
}
$Root = (Resolve-Path $Root).Path
if (-not $Name) { $Name = $Target }
if (-not $Version) {
    $Version = (git -C $Root describe --tags --always --dirty 2>$null)
    if (-not $Version) { $Version = "dev" }
}
$Version = $Version -replace '[\\/ ]', '-'
if (-not $Name -or $Name -match '^[.-]' -or $Name -match '[\\/]') {
    throw "Name must be a visible filename component without slashes"
}
if (-not (Test-Path $MingwPrefix)) {
    throw "MinGW prefix not found: $MingwPrefix"
}

$env:PATH = "$MingwPrefix\bin;$env:PATH"
$BuildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $Root $BuildDir
}

# Static runtime link: -static-libgcc/-static-libstdc++ must reach the parent
# executable through CMAKE_EXE_LINKER_FLAGS (add_link_options in the framework
# subdir cannot), and --no-insert-timestamp keeps the PE reproducible.
$ConfigureArgs = @(
    "-S", $Root,
    "-B", $BuildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DGBARECOMP_STATIC_RELEASE=ON",
    "-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp"
)
Write-Host "==> configure ($BuildDir)"
& cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

$Jobs = [Environment]::ProcessorCount
Write-Host "==> build $Target ($Jobs jobs)"
& cmake --build $BuildPath --target $Target --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$Exe = Join-Path $BuildPath "$Target.exe"
if (-not (Test-Path $Exe)) { throw "built executable not found: $Exe" }

$StageRoot = Join-Path $Root "release-stage"
$Out = Join-Path $StageRoot "$Name-$Version-windows-x64"
Write-Host "==> stage $Out"
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Copy-Item $Exe (Join-Path $Out "$Target.exe")

foreach ($dir in @("assets", "mods")) {
    $src = Join-Path $BuildPath $dir
    if (Test-Path $src) {
        Copy-Item $src -Destination $Out -Recurse
        Write-Host "    + $dir/"
    }
}

foreach ($spec in $Payload) {
    $parts = $spec -split "=", 2
    $src = $parts[0]
    if (-not [System.IO.Path]::IsPathRooted($src)) { $src = Join-Path $Root $src }
    if (-not (Test-Path $src)) { throw "payload source not found: $src" }
    $dst = if ($parts.Length -eq 2 -and $parts[1]) { $parts[1] } else { Split-Path -Leaf $src }
    $dstPath = Join-Path $Out $dst
    $parent = Split-Path -Parent $dstPath
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    if (Test-Path $src -PathType Container) {
        Copy-Item $src -Destination $dstPath -Recurse
    } else {
        Copy-Item $src -Destination $dstPath
    }
    Write-Host "    + $dst"
}

foreach ($dir in $PayloadDir) {
    $src = if ([System.IO.Path]::IsPathRooted($dir)) { $dir } else { Join-Path $Root $dir }
    if (-not (Test-Path $src -PathType Container)) {
        throw "payload directory not found: $src"
    }
    Get-ChildItem -Force $src | Copy-Item -Destination $Out -Recurse
    Write-Host "    + $dir/ contents"
}

$forbidden = Get-ChildItem -Recurse -File $Out | Where-Object {
    $_.Extension -match '^\.(gba|agb|sav|srm|apk)$'
}
if ($forbidden) {
    throw "payload contains a ROM/save/APK extension: $($forbidden -join ', ')"
}

@"
$Name $Version - Windows (x64)

Run $Target.exe from this directory. No ROM or BIOS is included: supply your
own legally dumped cartridge; the launch screen prompts for it and remembers
the pick beside this executable.

Settings, keybinds, and saves are written beside the executable; extract to a
writable folder.
"@ | Set-Content -Encoding UTF8 (Join-Path $Out "README.txt")

# PE import guard: only Windows system DLLs are legal in the static release.
$Objdump = Join-Path $MingwPrefix "bin\x86_64-w64-mingw32-objdump.exe"
if (Test-Path $Objdump) {
    $imports = (& $Objdump -p (Join-Path $Out "$Target.exe") |
        Select-String 'DLL Name:\s*(\S+)' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.ToUpperInvariant() })
    $bad = $imports | Where-Object {
        $_ -match '^(LIBSTDCPP|LIBGCC_S|LIBWINPTHREAD|FREETYPE|HARFBUZZ|ZLIB1|SDL2)'
    }
    if ($bad) {
        throw "third-party DLL imports in the static release: $($bad -join ', ')"
    }
    Write-Host "    imports: $($imports.Count) system DLLs"
} else {
    Write-Warning "objdump not found at $Objdump; PE import guard skipped"
}

if (-not $NoZip) {
    $Zip = "$Out.zip"
    if (Test-Path $Zip) { Remove-Item -Force $Zip }
    Compress-Archive -Path $Out -DestinationPath $Zip
    Write-Host "==> release-stage\$Name-$Version-windows-x64.zip"
}

Write-Host ""
Write-Host "Built: $Out"
