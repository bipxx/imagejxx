param(
    [string]$DependencyPrefix = '',
    [switch]$Build,
    [string[]]$Images = @()
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $workspace 'build\imgui'
$executable = Join-Path $buildDirectory 'Release\imagejxx.exe'
if ($Build -or !(Test-Path -LiteralPath $executable)) {
    if (!$DependencyPrefix) { $DependencyPrefix = Join-Path $workspace '.tools\imgui-sdk' }
    $DependencyPrefix = (Resolve-Path -LiteralPath $DependencyPrefix).Path
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) { $cmakePath = $cmake.Source }
    else { $cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
    & $cmakePath -S $workspace -B $buildDirectory -DIMAGEJXX_BUILD_PYTHON=OFF -DIMAGEJXX_BUILD_CPP_UI=ON "-DCMAKE_PREFIX_PATH=$DependencyPrefix"
    if ($LASTEXITCODE -ne 0) { throw 'Desktop configuration failed' }
    & $cmakePath --build $buildDirectory --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed' }
}
& $executable @Images
exit $LASTEXITCODE
