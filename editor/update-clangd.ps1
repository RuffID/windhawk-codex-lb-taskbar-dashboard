param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '../mod.wh.cpp'),
    [string] $CompilerRoot = 'D:/Programs/Windhawk/Compiler'
)
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$source = [IO.File]::ReadAllText($SourcePath, $utf8)
$options = [regex]::Match($source, '(?m)^// @compilerOptions\s+([^\r\n]+)').Groups[1].Value
$tokens = [Collections.Generic.List[string]]::new()
$token = [Text.StringBuilder]::new()
$singleOpen = $false
$doubleOpen = $false
foreach ($character in $options.ToCharArray()) {
    if ($character -eq "'" -and !$doubleOpen) { $singleOpen = !$singleOpen; continue }
    if ($character -eq '"' -and !$singleOpen) { $doubleOpen = !$doubleOpen; continue }
    if (!$singleOpen -and !$doubleOpen -and [char]::IsWhiteSpace($character)) {
        if ($token.Length) { $tokens.Add($token.ToString()); $null = $token.Clear() }
    } else { $null = $token.Append($character) }
}
if ($singleOpen -or $doubleOpen) { throw 'Unclosed compiler option quote' }
if ($token.Length) { $tokens.Add($token.ToString()) }
$includes = @($tokens | Where-Object { $_.StartsWith('-I') })
if ($includes.Count -ne 1) { throw 'Expected one canonical -I project path in @compilerOptions' }
$projectRoot = [IO.Path]::GetFullPath($includes[0].Substring(2))
$scriptProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($projectRoot -ne $scriptProjectRoot) { throw 'Metadata -I does not point to this project' }
$compilerExe = Join-Path $CompilerRoot 'bin/clang++.exe'
if (!(Test-Path -LiteralPath $compilerExe)) { throw 'Windhawk compiler not found' }
$flags = @('-x', 'c++', '-std=c++23', '-target', 'x86_64-w64-mingw32',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008',
    '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', '-DWH_EDITING',
    '-include', 'windhawk_api.h', '-Wall', '-Wextra', '-Wno-unused-parameter',
    '-Wno-missing-field-initializers', '-Wno-cast-function-type-mismatch')
$database = @((Join-Path $projectRoot 'mod.wh.cpp'), $SourcePath, (Join-Path $projectRoot 'tests/regression.cpp')) | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique | ForEach-Object {
    [ordered]@{
        directory = $CompilerRoot.Replace('\', '/')
        file = $_.Replace('\', '/')
        arguments = @($compilerExe.Replace('\', '/')) + $flags + $tokens.ToArray() + @($_.Replace('\', '/'))
    }
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'compile_commands.json'),
    ([regex]::Replace((ConvertTo-Json -InputObject @($database) -Depth 5), '\r?\n', "`r`n") + "`r`n"), $utf8)
$sourceDir = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($SourcePath))
$relativeDb = [IO.Path]::GetRelativePath($sourceDir, $PSScriptRoot).Replace('\', '/')
$clangdConfig = "# Generated from the mod's @compilerOptions; see the project README.`r`n" +
    "CompileFlags:`r`n  CompilationDatabase: " + (ConvertTo-Json $relativeDb -Compress) + "`r`n"
[IO.File]::WriteAllText((Join-Path $sourceDir '.clangd'), $clangdConfig, $utf8)
Write-Output 'Updated clangd configuration; no compiler, editor or mod was started.'
