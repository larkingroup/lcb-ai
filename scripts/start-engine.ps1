param(
    [Parameter(Mandatory)][string]$Server,
    [Parameter(Mandatory)][string]$Model,
    [ValidateRange(1,65535)][int]$Port = 8080
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Server -PathType Leaf)) { throw 'Engine executable not found.' }
if (!(Test-Path -LiteralPath $Model -PathType Leaf)) { throw 'Model not found. Check the model drive.' }
if ([IO.Path]::GetExtension($Model) -ne '.gguf') { throw 'This adapter expects a GGUF model.' }
if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
    throw "Port $Port is already in use. No existing process was stopped."
}
$engineArgs = @('--model', $Model, '--alias', 'local', '--host', '127.0.0.1',
    '--port', "$Port", '--ctx-size', '4096', '--parallel', '1', '--fit', 'on',
    '--gpu-layers', 'auto', '--cache-type-k', 'q8_0', '--cache-type-v', 'q8_0',
    '--no-webui', '--no-agent', '--no-ui-mcp-proxy', '--cors-origins', 'localhost',
    '--no-cors-credentials')
Write-Host "Starting local engine on 127.0.0.1:$Port. Press Ctrl+C to stop it."
$resolvedServer = (Resolve-Path -LiteralPath $Server).Path
# The engine discovers dynamically loaded acceleration backends beside its binary.
# Resolve the model before changing directory so relative model paths still work.
$engineArgs[1] = (Resolve-Path -LiteralPath $Model).Path
Push-Location ([IO.Path]::GetDirectoryName($resolvedServer))
try { & $resolvedServer @engineArgs; $engineExit = $LASTEXITCODE }
finally { Pop-Location }
exit $engineExit
