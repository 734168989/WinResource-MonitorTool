# patch_timestamp.ps1
# Binary-patches the linked exe: replaces timestamp placeholder bytes
# with actual build time.  Same-length → safe, works on raw bytes.
param([string]$ExeFile)

# Helper: find and replace byte sequence in byte array
function Replace-Bytes($data, $find, $replace) {
    $result = New-Object System.Collections.Generic.List[byte]
    $i = 0
    $n = $data.Length
    $fl = $find.Length
    while ($i -lt $n) {
        $match = $true
        for ($j = 0; $j -lt $fl; $j++) {
            if (($i + $j) -ge $n -or $data[$i + $j] -ne $find[$j]) {
                $match = $false
                break
            }
        }
        if ($match) {
            $result.AddRange($replace)
            $i += $fl
        } else {
            $result.Add($data[$i])
            $i++
        }
    }
    return $result.ToArray()
}

$now       = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$nowShort  = Get-Date -Format "yyyy-MM"

$placeholderFull  = "0000-00-00 00:00:00"
$placeholderShort = "0000-00"

$enc = [System.Text.Encoding]::Unicode

$phFullBytes   = $enc.GetBytes($placeholderFull)
$phShortBytes  = $enc.GetBytes($placeholderShort)
$nowFullBytes  = $enc.GetBytes($now)
$nowShortBytes = $enc.GetBytes($nowShort)

# Read → patch → write (raw bytes, no corrupting string conversion)
$bytes = [System.IO.File]::ReadAllBytes($ExeFile)
$bytes = Replace-Bytes $bytes $phFullBytes  $nowFullBytes
$bytes = Replace-Bytes $bytes $phShortBytes $nowShortBytes
[System.IO.File]::WriteAllBytes($ExeFile, $bytes)

Write-Host "Patched timestamp: $now / $nowShort"
