<#
    Fails when code adds a failure-handling pattern that the failure-handling
    rules forbid (docs/scic-cli/plan.md, section 6.8):

      empty-catch-all          a "catch (...)" whose body is empty or holds only comments
                               and ";", which hides a failure
      throw-std-exception      "throw std::exception(...)", a Microsoft-only form;
                               throw sci::DataError or return a sci::Result instead
      afxmessagebox-in-engine  AfxMessageBox in Src\Core, Src\Compile or Src\Resources;
                               return a sci::Result and let the GUI show it
      appstate-in-engine       the GUI object appState, or the functions of
                               AppSession.h (AppSession, AppResourceMap,
                               AppVersion), in Src\Core, Src\Compile,
                               Src\Resources or Src\Util; take the session, the
                               resource map or the helper as a parameter (plan
                               section 3.2). Src\Util also holds GUI code, so
                               its GUI files are in the allowlist.

    The sites that existed before the rules are in CheckFailureHandling.allow.txt,
    one line per file: "<rule> <path> <count>". A file must have exactly its
    count: more is a new site, and fewer means the allowlist is stale (a new
    site could later take the freed place unnoticed). When you remove old
    sites, run with -Update to lower the counts, and commit the allowlist.

    Usage:
      .\UnitTests\Tools\CheckFailureHandling.ps1           # check; exit 1 on a new site
      .\UnitTests\Tools\CheckFailureHandling.ps1 -Update   # rewrite the allowlist
#>
param([switch]$Update)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$srcRoot = Join-Path $repoRoot "SCICompanionLib\Src"
$allowPath = Join-Path $PSScriptRoot "CheckFailureHandling.allow.txt"

# Third-party code keeps its own style.
$excluded = @("CrystalEdit", "GIFLIB", "CppFormat", "r8brain", "CRC32")

$rules = @(
    @{ Name = "empty-catch-all"; Pattern = 'catch\s*\(\s*\.\.\.\s*\)\s*\{(?:\s|;|//[^\n]*|/\*[\s\S]*?\*/)*\}'; Folders = $null },
    @{ Name = "throw-std-exception"; Pattern = 'throw\s+std::exception\s*\('; Folders = $null },
    @{ Name = "afxmessagebox-in-engine"; Pattern = '\bAfxMessageBox\s*\('; Folders = @("Core", "Compile", "Resources") },
    @{ Name = "appstate-in-engine"; Pattern = '\bappState\b|\bApp(Session|ResourceMap|Version)\s*\('; Folders = @("Core", "Compile", "Resources", "Util") }
)

$found = @{}
$files = Get-ChildItem -Path $srcRoot -Recurse -File -Include *.cpp, *.h
foreach ($file in $files) {
    $relative = $file.FullName.Substring($srcRoot.Length + 1)
    $topFolder = ($relative -split "\\")[0]
    if ($excluded -contains $topFolder) { continue }
    $text = [IO.File]::ReadAllText($file.FullName)
    foreach ($rule in $rules) {
        if ($rule.Folders -and -not ($rule.Folders -contains $topFolder)) { continue }
        $count = ([regex]::Matches($text, $rule.Pattern)).Count
        if ($count -gt 0) {
            $found["$($rule.Name) $relative"] = $count
        }
    }
}

if ($Update) {
    $lines = $found.Keys | Sort-Object | ForEach-Object { "$_ $($found[$_])" }
    $header = @(
        "# Sites that existed before the rules (see CheckFailureHandling.ps1).",
        "# Format: <rule> <path under SCICompanionLib\Src> <count>. Lower a count when you remove a site."
    )
    [IO.File]::WriteAllText($allowPath, (($header + $lines) -join "`r`n") + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
    "Wrote $($lines.Count) entries to $allowPath"
    exit 0
}

$allowed = @{}
if (Test-Path $allowPath) {
    foreach ($line in [IO.File]::ReadAllLines($allowPath)) {
        if ($line.Trim() -eq "" -or $line.StartsWith("#")) { continue }
        $parts = $line.Trim() -split "\s+"
        $allowed["$($parts[0]) $($parts[1])"] = [int]$parts[2]
    }
}

$failures = @()
$lower = @()
foreach ($key in ($found.Keys | Sort-Object)) {
    $limit = if ($allowed.ContainsKey($key)) { $allowed[$key] } else { 0 }
    if ($found[$key] -gt $limit) {
        $failures += "NEW: $key (found $($found[$key]), allowed $limit)"
    } elseif ($found[$key] -lt $limit) {
        $lower += "lower the count: $key (found $($found[$key]), allowed $limit)"
    }
}
foreach ($key in $allowed.Keys) {
    if (-not $found.ContainsKey($key)) { $lower += "remove the entry: $key (none left)" }
}

$lower | ForEach-Object { $_ }
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { $_ }
    "Failure-handling check FAILED: $($failures.Count) new site(s). See the rules at the top of this script, and docs/scic-cli/plan.md (section 6; section 3.2 for appState)."
    exit 1
}
if ($lower.Count -gt 0) {
    "Failure-handling check FAILED: the allowlist is stale. Run this script with -Update and commit the allowlist."
    exit 1
}
"Failure-handling check passed ($($found.Count) allowed site group(s))."
exit 0
