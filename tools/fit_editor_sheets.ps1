$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot '..\examples\click_command.generated.cpp'
$src = Get-Content -LiteralPath $path -Raw

$old = 'float cell=36;int c=(int)((mx-px)/cell),r=(int)((my-py)/cell);'
$new = 'float cell=std::max(12.0f,std::min(36.0f,(panel-28.0f)/(float)cols));int c=(int)((mx-px)/cell),r=(int)((my-py)/cell);'
if (-not $src.Contains($old)) { throw 'fit_editor_sheets: selector grid not found' }
$src = $src.Replace($old,$new)

$old = 'float cell=36,py=94;int maxRows='
$new = 'float cell=std::max(12.0f,std::min(36.0f,(panel-28.0f)/(float)cols)),py=94;float pad=cell>18?2.0f:1.0f;int maxRows='
if (-not $src.Contains($old)) { throw 'fit_editor_sheets: render grid not found' }
$src = $src.Replace($old,$new)

$old = 'e.drawTextureRegion(t,srcCell(c,r,inf.tile),{x+2,y+2,32,32});'
$new = 'e.drawTextureRegion(t,srcCell(c,r,inf.tile),{x+pad,y+pad,cell-pad*2,cell-pad*2});'
if (-not $src.Contains($old)) { throw 'fit_editor_sheets: tile preview not found' }
$src = $src.Replace($old,$new)
$src = $src.Replace('e.drawRect({x,y,36,36},{255,220,70,255});','e.drawRect({x,y,cell,cell},{255,220,70,255});')
$src = $src.Replace('e.drawRect({x,y,36,36},{60,75,63,160});','e.drawRect({x,y,cell,cell},{60,75,63,160});')

Set-Content -LiteralPath $path -Value $src -Encoding UTF8
Write-Host 'Editor Mystic Woods: todas las columnas de cada hoja caben en la paleta.'
