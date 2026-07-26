param(
  [ValidateSet("vulkan")]
  [string]$Backend = "vulkan",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$testsDirectory = $PSScriptRoot
$repositoryRoot = Split-Path -Parent $testsDirectory

Push-Location $testsDirectory
try {
  $premake = (Get-Command premake5 -ErrorAction Stop).Source
  & $premake vs2022
  $project = Join-Path $testsDirectory "VelosRhiTests.vcxproj"
  $msbuild = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
  & $msbuild $project /m /p:Configuration=$Configuration /p:Platform=x64
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $binary = Join-Path $repositoryRoot "bin\$Configuration-windows-x86_64\VelosRhiTests\VelosRhiTests.exe"
  & $binary --backend $Backend --report (Join-Path $testsDirectory "last-run.html")
  exit $LASTEXITCODE
}
finally {
  Pop-Location
}
