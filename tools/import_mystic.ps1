param(
    [string]$ZipPath = (Join-Path $PSScriptRoot '..\assets\vendor\mystic_woods_free_2.2.zip')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$target = Join-Path $repo 'assets\mystic'

if (-not (Test-Path -LiteralPath $ZipPath)) {
    throw "Falta Mystic Woods 2.2: $ZipPath"
}

$tmp = Join-Path $env:TEMP ("click_command_mystic_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
try {
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $tmp -Force
    $sprites = Join-Path $tmp 'sprites'
    if (-not (Test-Path -LiteralPath $sprites)) {
        throw 'El ZIP no contiene la carpeta sprites esperada.'
    }

    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $sprites '*') -Destination $target -Recurse -Force

    $aliases = @(
        @('tilesets\plains.png', 'plains.png'),
        @('tilesets\floors\flooring.png', 'tilesets\flooring.png'),
        @('tilesets\floors\wooden.png', 'tilesets\wooden.png'),
        @('tilesets\walls\walls.png', 'tilesets\walls.png')
    )
    foreach ($pair in $aliases) {
        $from = Join-Path $target $pair[0]
        $to = Join-Path $target $pair[1]
        if (Test-Path -LiteralPath $from) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
            Copy-Item -LiteralPath $from -Destination $to -Force
        }
    }

    Write-Host 'Mystic Woods 2.2 importado completo.'
}
finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
}
