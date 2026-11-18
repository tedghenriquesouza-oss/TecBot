$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$venvPath = Join-Path $projectRoot ".venv-piper"
$pythonPath = Join-Path $venvPath "Scripts\python.exe"
$voiceDirectory = Join-Path $projectRoot "models\voices"
$modelName = "pt_BR-faber-medium.onnx"
$modelUrl = "https://huggingface.co/rhasspy/piper-voices/resolve/c10ece1aade47bb51c153c893d14e5bf8e5b7117/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx"
$configUrl = "https://huggingface.co/rhasspy/piper-voices/resolve/c10ece1aade47bb51c153c893d14e5bf8e5b7117/pt/pt_BR/faber/medium/pt_BR-faber-medium.onnx.json"
$expectedModelSize = 63201294
$expectedModelSha256 = "858555e3a064209c57088fe6bd70c4c3dc54d03eaa00c45d5ecaf43a33f95aa7"
$expectedConfigSize = 4855
$expectedConfigGitSha1 = "b0206c81d70e6e4468b94d36491b4c17fac531b7"

function Get-GitBlobSha1([string]$filePath) {
  $bytes = [System.IO.File]::ReadAllBytes($filePath)
  $prefix = [System.Text.Encoding]::ASCII.GetBytes("blob $($bytes.Length)`0")
  $combined = New-Object byte[] ($prefix.Length + $bytes.Length)
  [Array]::Copy($prefix, 0, $combined, 0, $prefix.Length)
  [Array]::Copy($bytes, 0, $combined, $prefix.Length, $bytes.Length)
  $algorithm = [System.Security.Cryptography.SHA1]::Create()
  $sha1 = $algorithm.ComputeHash($combined)
  $algorithm.Dispose()
  return ([System.BitConverter]::ToString($sha1)).Replace("-", "").ToLowerInvariant()
}

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  throw "Python 3.9 or newer is required. Install Python and run this script again."
}

$pythonVersion = python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
$versionParts = $pythonVersion.Trim().Split(".")
if ([int]$versionParts[0] -lt 3 -or ([int]$versionParts[0] -eq 3 -and [int]$versionParts[1] -lt 9)) {
  throw "Piper requires Python 3.9 or newer; detected $pythonVersion."
}

if (-not (Test-Path -LiteralPath $pythonPath)) {
  python -m venv $venvPath
}
if (-not (Test-Path -LiteralPath $pythonPath)) {
  throw "Could not create the project-local Python environment."
}

& $pythonPath -m pip install --disable-pip-version-check -r (Join-Path $projectRoot "requirements-voice.txt")
if ($LASTEXITCODE -ne 0) {
  throw "Could not install the pinned Piper package."
}

New-Item -ItemType Directory -Path $voiceDirectory -Force | Out-Null
$modelPath = Join-Path $voiceDirectory $modelName
$configPath = "$modelPath.json"

if (Test-Path -LiteralPath $modelPath) {
  if ((Get-Item -LiteralPath $modelPath).Length -ne $expectedModelSize) {
    throw "The existing voice model has an unexpected size; it was left unchanged."
  }
  $existingHash = (Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($existingHash -ne $expectedModelSha256) {
    throw "The existing voice model failed its SHA-256 check; it was left unchanged."
  }
} else {
  $temporaryModelPath = "$modelPath.download"
  if (Test-Path -LiteralPath $temporaryModelPath) {
    Remove-Item -LiteralPath $temporaryModelPath
  }
  Invoke-WebRequest -Uri $modelUrl -OutFile $temporaryModelPath
  if ((Get-Item -LiteralPath $temporaryModelPath).Length -ne $expectedModelSize) {
    Remove-Item -LiteralPath $temporaryModelPath
    throw "The downloaded voice model has an unexpected size."
  }
  $downloadedHash = (Get-FileHash -LiteralPath $temporaryModelPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($downloadedHash -ne $expectedModelSha256) {
    Remove-Item -LiteralPath $temporaryModelPath
    throw "The downloaded voice model failed its SHA-256 check."
  }
  Move-Item -LiteralPath $temporaryModelPath -Destination $modelPath
}

if (-not (Test-Path -LiteralPath $configPath)) {
  $temporaryConfigPath = "$configPath.download"
  if (Test-Path -LiteralPath $temporaryConfigPath) {
    Remove-Item -LiteralPath $temporaryConfigPath
  }
  Invoke-WebRequest -Uri $configUrl -OutFile $temporaryConfigPath
  if ((Get-Item -LiteralPath $temporaryConfigPath).Length -ne $expectedConfigSize) {
    Remove-Item -LiteralPath $temporaryConfigPath
    throw "The downloaded voice configuration has an unexpected size."
  }
  if ((Get-GitBlobSha1 $temporaryConfigPath) -ne $expectedConfigGitSha1) {
    Remove-Item -LiteralPath $temporaryConfigPath
    throw "The downloaded voice configuration failed its integrity check."
  }
  Move-Item -LiteralPath $temporaryConfigPath -Destination $configPath
}

if ((Get-Item -LiteralPath $configPath).Length -ne $expectedConfigSize -or (Get-GitBlobSha1 $configPath) -ne $expectedConfigGitSha1) {
  throw "The existing voice configuration failed its integrity check; it was left unchanged."
}

Write-Output "Piper and pt_BR-faber-medium installed and verified."
Write-Output "Model: $modelPath ($expectedModelSize bytes; SHA-256 verified)."
