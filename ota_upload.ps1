<#
============================================================
  ota_upload.ps1
  CAI_MINI - Firmware als OTA-Paket nach ThingsBoard hochladen

  Ablage   : Projektordner (neben platformio.ini)
  Aufruf   : .\ota_upload.ps1           Auswahl, Login, Kontrolle, Upload
             .\ota_upload.ps1 -Liste    nur Übersicht der Environments
  Benötigt : Windows PowerShell 5.1 oder PowerShell 7

  Ablauf:
    1. Firmware auswählen (Environments aus platformio.ini)
    2. Anmelden bei ThingsBoard
    3. Device-Profil und Version je Firmware bestätigen
    4. Kontrolle aller Daten
    5. Upload (optional: Zuweisung ans Device-Profil = Rollout)

  Titel    : Gateways -> LORA-Gateway-FW, alle anderen -> CAI-Mini-FW
  Version  : FW_VERSION aus der Config-Datei des Environments
  Binary   : .pio\build\<env>\firmware.bin
============================================================
#>

param(
    [switch]$Liste
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

# ============================================================
#  Konfiguration
# ============================================================
$TbServer      = 'https://iot.mfsquare.ch'
$TitleDevice   = 'CAI-Mini-FW'
$TitleGateway  = 'LORA-Gateway-FW'

$ProjectDir    = $PSScriptRoot
$IniPath       = Join-Path $ProjectDir 'platformio.ini'
$SettingsPath  = Join-Path $ProjectDir '.ota_upload.json'   # Benutzer, Profil je Env (kein Passwort)

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Net.Http

# ============================================================
#  Ausgabe
# ============================================================
function Write-Title([string]$Text) { Write-Host ''; Write-Host "== $Text ==" -ForegroundColor Cyan; Write-Host '' }
function Write-Ok   ([string]$Text) { Write-Host "[OK]     $Text" -ForegroundColor Green }
function Write-Warn ([string]$Text) { Write-Host "[WARN]   $Text" -ForegroundColor Yellow }
function Write-Err  ([string]$Text) { Write-Host "[FEHLER] $Text" -ForegroundColor Red }

function Read-Default([string]$Prompt, [string]$Default) {
    if ($Default) { $a = Read-Host "$Prompt [$Default]" } else { $a = Read-Host $Prompt }
    if ([string]::IsNullOrWhiteSpace($a)) { return $Default }
    return $a.Trim()
}

function Read-YesNo([string]$Prompt) {
    $a = Read-Host "$Prompt [j/N]"
    return ($a -match '^[jJyY]$')
}

function Format-Size([long]$Bytes)   { return ('{0:N1} KB' -f ($Bytes / 1024)) }
function Format-Time([datetime]$T)   { return $T.ToString('dd.MM.yyyy HH:mm') }

# ============================================================
#  Einstellungen (Benutzer, Profilzuordnung)
# ============================================================
function Get-Settings {
    $s = @{ user = ''; profiles = @{} }
    if (Test-Path $SettingsPath) {
        $j = Get-Content $SettingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($j.user)   { $s.user   = $j.user }
        if ($j.profiles) {
            foreach ($p in $j.profiles.PSObject.Properties) { $s.profiles[$p.Name] = $p.Value }
        }
    }
    return $s
}

function Save-Settings($S) {
    $S | ConvertTo-Json -Depth 5 | Set-Content -Path $SettingsPath -Encoding UTF8
}

# ============================================================
#  platformio.ini lesen
#  Unterstützt extends und ${section.key}
# ============================================================
function Read-PioIni([string]$Path) {
    $ini     = [ordered]@{}
    $section = $null
    $lastKey = $null

    foreach ($raw in Get-Content $Path -Encoding UTF8) {
        $line = $raw.TrimEnd()

        if ($line -match '^\[([^\]]+)\]') {
            $section = $Matches[1].Trim()
            if (-not $ini.Contains($section)) { $ini[$section] = [ordered]@{} }
            $lastKey = $null
            continue
        }
        if ($null -eq $section) { continue }
        if ($line -match '^\s*[;#]' -or $line -eq '') { if ($line -eq '') { $lastKey = $null }; continue }

        if ($line -match '^\s+(\S.*)$' -and $lastKey) {
            $ini[$section][$lastKey] = ($ini[$section][$lastKey] + ' ' + $Matches[1].Trim()).Trim()
            continue
        }
        if ($line -match '^([^=\s][^=]*?)\s*=\s*(.*)$') {
            $lastKey = $Matches[1].Trim()
            $ini[$section][$lastKey] = $Matches[2].Trim()
        }
    }
    return $ini
}

function Get-IniValue($Ini, [string]$Section, [string]$Key, [int]$Depth = 0) {
    if ($Depth -gt 10 -or -not $Ini.Contains($Section)) { return $null }
    $sec = $Ini[$Section]
    $val = $null

    if ($sec.Contains($Key)) {
        $val = $sec[$Key]
    } elseif ($sec.Contains('extends')) {
        foreach ($parent in ($sec['extends'] -split '[,\s]+' | Where-Object { $_ })) {
            $val = Get-IniValue $Ini $parent $Key ($Depth + 1)
            if ($null -ne $val) { break }
        }
    }
    if ($null -eq $val) { return $null }

    for ($i = 0; $i -lt 20; $i++) {
        $m = [regex]::Match($val, '\$\{([^.}]+)\.([^}]+)\}')
        if (-not $m.Success) { break }
        $rep = Get-IniValue $Ini $m.Groups[1].Value $m.Groups[2].Value ($Depth + 1)
        if ($null -eq $rep) { $rep = '' }
        $val = $val.Replace($m.Value, $rep)
    }
    return $val
}

# ============================================================
#  FW_VERSION aus Config-Header auswerten
#  Kleiner Präprozessor: #if/#ifdef/#ifndef/#elif/#else/#endif,
#  #define/#undef, defined(), ==, !=, <, >, &&, ||, !
# ============================================================
function Remove-CComment([string]$S) {
    $sb    = New-Object System.Text.StringBuilder
    $inStr = $false
    for ($i = 0; $i -lt $S.Length; $i++) {
        $c = $S[$i]
        if ($inStr) {
            [void]$sb.Append($c)
            if ($c -eq '\' -and $i + 1 -lt $S.Length) { [void]$sb.Append($S[$i + 1]); $i++; continue }
            if ($c -eq '"') { $inStr = $false }
            continue
        }
        if ($i + 1 -lt $S.Length -and $S.Substring($i, 2) -eq '//') { break }
        if ($i + 1 -lt $S.Length -and $S.Substring($i, 2) -eq '/*') {
            $end = $S.IndexOf('*/', $i + 2)
            if ($end -lt 0) { break }
            $i = $end + 1; [void]$sb.Append(' '); continue
        }
        if ($c -eq '"') { $inStr = $true }
        [void]$sb.Append($c)
    }
    return $sb.ToString().Trim()
}

function Get-PpNumber([string]$T, $D) {
    $T = $T.Trim()
    if ($T -match '^-?\d+$') { return [long]$T }
    if ($D.ContainsKey($T) -and $D[$T] -match '^-?\d+$') { return [long]$D[$T] }
    return 0
}

function Test-PpAtom([string]$A, $D) {
    $A = $A.Trim(); $neg = $false
    while ($A.StartsWith('!') -and -not $A.StartsWith('!=')) { $neg = -not $neg; $A = $A.Substring(1).Trim() }
    while ($A -match '^\((.*)\)$') { $A = $Matches[1].Trim() }

    if ($A -match '^defined\s*\(?\s*([A-Za-z_]\w*)') {
        $res = $D.ContainsKey($Matches[1])
    } elseif ($A -match '^(.*?)(==|!=|>=|<=|>|<)(.*)$') {
        $l = Get-PpNumber $Matches[1] $D; $r = Get-PpNumber $Matches[3] $D
        switch ($Matches[2]) {
            '==' { $res = ($l -eq $r) }
            '!=' { $res = ($l -ne $r) }
            '>=' { $res = ($l -ge $r) }
            '<=' { $res = ($l -le $r) }
            '>'  { $res = ($l -gt $r) }
            '<'  { $res = ($l -lt $r) }
        }
    } else {
        $res = ((Get-PpNumber $A $D) -ne 0)
    }
    if ($neg) { return -not $res }
    return $res
}

function Test-PpCondition([string]$E, $D) {
    foreach ($or in ($E -split '\|\|')) {
        $all = $true
        foreach ($and in ($or -split '&&')) {
            if (-not (Test-PpAtom $and $D)) { $all = $false; break }
        }
        if ($all) { return $true }
    }
    return $false
}

function Resolve-PpString([string]$V, $D, [int]$Depth = 0) {
    if ($Depth -gt 10 -or $null -eq $V) { return $null }
    $out = ''; $found = $false
    foreach ($m in [regex]::Matches($V, '"((?:[^"\\]|\\.)*)"|([A-Za-z_]\w*)')) {
        if ($m.Groups[1].Success) {
            $out += ($m.Groups[1].Value -replace '\\(.)', '$1'); $found = $true
        } else {
            $tok = $m.Groups[2].Value
            if (-not $D.ContainsKey($tok)) { return $null }
            $sub = Resolve-PpString $D[$tok] $D ($Depth + 1)
            if ($null -eq $sub) { return $null }
            $out += $sub; $found = $true
        }
    }
    if ($found) { return $out }
    return $null
}

function Get-HeaderFwVersion([string]$Header, $Defines) {
    $D = @{}
    foreach ($k in $Defines.Keys) { $D[$k] = $Defines[$k] }

    $stack  = New-Object System.Collections.ArrayList   # Einträge: @{parent; taken}
    $active = $true

    foreach ($raw in Get-Content $Header -Encoding UTF8) {
        $line = $raw.Trim()
        if (-not $line.StartsWith('#')) { continue }
        $line = Remove-CComment $line.Substring(1)
        if ($line -notmatch '^([A-Za-z]+)\s*(.*)$') { continue }
        $kw = $Matches[1]; $rest = $Matches[2].Trim()
        $word = ''
        if ($rest -match '^([A-Za-z_]\w*)') { $word = $Matches[1] }

        switch ($kw) {
            { $_ -in 'if', 'ifdef', 'ifndef' } {
                $parent = $active
                if     ($kw -eq 'if')    { $c = Test-PpCondition $rest $D }
                elseif ($kw -eq 'ifdef') { $c = $D.ContainsKey($word) }
                else                     { $c = -not $D.ContainsKey($word) }
                $c = $parent -and $c
                [void]$stack.Add(@{ parent = $parent; taken = $c })
                $active = $c
            }
            'elif' {
                if ($stack.Count -eq 0) { break }
                $top = $stack[$stack.Count - 1]
                $c = $top.parent -and -not $top.taken -and (Test-PpCondition $rest $D)
                if ($c) { $top.taken = $true }
                $active = $c
            }
            'else' {
                if ($stack.Count -eq 0) { break }
                $top = $stack[$stack.Count - 1]
                $active = $top.parent -and -not $top.taken
                $top.taken = $true
            }
            'endif' {
                if ($stack.Count -eq 0) { break }
                $active = $stack[$stack.Count - 1].parent
                $stack.RemoveAt($stack.Count - 1)
            }
            'define' {
                if ($active -and $word) {
                    $v = $rest.Substring($word.Length).Trim()
                    if ($v -eq '') { $v = '1' }
                    $D[$word] = $v
                }
            }
            'undef' {
                if ($active -and $word) { $D.Remove($word) }
            }
        }
    }

    if ($D.ContainsKey('FW_VERSION')) { return Resolve-PpString $D['FW_VERSION'] $D }
    return $null
}

# ============================================================
#  Environment analysieren
# ============================================================
function Get-EnvInfo($Ini, [string]$EnvName) {
    $info = [ordered]@{
        Env     = $EnvName
        Title   = $(if ($EnvName -like '*GATEWAY*') { $TitleGateway } else { $TitleDevice })
        Version = $null
        Config  = $null
        Bin     = [IO.Path]::Combine($ProjectDir, '.pio', 'build', $EnvName, 'firmware.bin')
        Exists  = $false
        Size    = 0
        MTime   = $null
        Warn    = @()
    }

    # ── Binary ───────────────────────────────────────────────
    if (Test-Path $info.Bin) {
        $f = Get-Item $info.Bin
        $info.Exists = $true
        $info.Size   = $f.Length
        $info.MTime  = $f.LastWriteTime
    } else {
        $info.Warn += 'nicht gebaut'
    }

    # ── Quellordner aus build_src_filter ─────────────────────
    $srcDir = Get-IniValue $Ini 'platformio' 'src_dir'
    if (-not $srcDir) { $srcDir = 'src' }
    $filter = Get-IniValue $Ini "env:$EnvName" 'build_src_filter'
    $folder = $null
    if ($filter) {
        foreach ($m in [regex]::Matches($filter, '\+<([^>]+)>')) {
            if ($m.Groups[1].Value -ne '*') { $folder = $m.Groups[1].Value.TrimEnd('/', '\'); break }
        }
    }

    # ── Config-Header aus #include "config_*.h" ──────────────
    if ($folder) {
        $srcPath = Join-Path (Join-Path $ProjectDir $srcDir) $folder
        if (Test-Path $srcPath) {
            $hit = Get-ChildItem $srcPath -Recurse -File -Include *.cpp, *.c, *.h, *.ino -ErrorAction SilentlyContinue |
                   Select-String -Pattern '#include\s*"(config_[^"]+\.h)"' -List | Select-Object -First 1
            if ($hit) {
                $cfgName = $hit.Matches[0].Groups[1].Value
                $cfg = Get-ChildItem $ProjectDir -Recurse -File -Filter $cfgName -ErrorAction SilentlyContinue |
                       Where-Object { $_.FullName -notlike '*\.pio\*' -and $_.FullName -notlike '*/.pio/*' } |
                       Select-Object -First 1
                if ($cfg) { $info.Config = $cfg.FullName }
            }
        }
    }

    # ── FW_VERSION mit Build-Flags des Environments ──────────
    if ($info.Config) {
        $defines = @{}
        $flags = Get-IniValue $Ini "env:$EnvName" 'build_flags'
        if ($flags) {
            foreach ($tok in ($flags -split '\s+')) {
                if ($tok -match '^-D([A-Za-z_]\w*)(?:=(.*))?$') {
                    $val = $Matches[2]
                    if (-not $val) { $val = '1' }
                    $defines[$Matches[1]] = $val
                }
            }
        }
        $info.Version = Get-HeaderFwVersion $info.Config $defines

        if ($info.Exists -and $info.MTime -lt (Get-Item $info.Config).LastWriteTime) {
            $info.Warn += 'Binary älter als Config'
        }
    }
    if (-not $info.Version) { $info.Warn += 'FW_VERSION nicht gefunden' }

    return [pscustomobject]$info
}

# ============================================================
#  ThingsBoard REST
# ============================================================
$script:Server = ''
$script:Token  = ''

function Get-ApiError($ErrRecord) {
    $resp = $ErrRecord.Exception.Response
    if ($null -eq $resp) {
        if ($ErrRecord.Exception.Message -match 'HTTP \d+') { return $ErrRecord.Exception.Message }
        return 'Server nicht erreichbar'
    }
    $code = [int]$resp.StatusCode
    $msg  = ''
    try {
        if ($ErrRecord.ErrorDetails -and $ErrRecord.ErrorDetails.Message) {
            $msg = ($ErrRecord.ErrorDetails.Message | ConvertFrom-Json).message
        } else {
            $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
            $msg = ($reader.ReadToEnd() | ConvertFrom-Json).message
        }
    } catch { }
    if ($msg) { return "HTTP $code - $msg" }
    return "HTTP $code"
}

function Invoke-Tb([string]$Method, [string]$Path, $Body = $null) {
    $params = @{
        Method  = $Method
        Uri     = "$($script:Server)$Path"
        Headers = @{ 'X-Authorization' = "Bearer $($script:Token)"; 'Accept' = 'application/json' }
    }
    if ($null -ne $Body) {
        $json = $Body | ConvertTo-Json -Depth 30 -Compress
        $params.Body        = [Text.Encoding]::UTF8.GetBytes($json)
        $params.ContentType = 'application/json; charset=utf-8'
    }
    return Invoke-RestMethod @params
}

function Connect-Tb([string]$User, [string]$Password) {
    $json = @{ username = $User; password = $Password } | ConvertTo-Json -Compress
    $r = Invoke-RestMethod -Method Post -Uri "$($script:Server)/api/auth/login" `
            -Body ([Text.Encoding]::UTF8.GetBytes($json)) -ContentType 'application/json; charset=utf-8'
    $script:Token = $r.token
}

function Get-TbProfiles {
    $list = @(); $page = 0
    do {
        $r = Invoke-Tb GET "/api/deviceProfiles?pageSize=100&page=$page&sortProperty=name&sortOrder=ASC"
        foreach ($p in $r.data) { $list += [pscustomobject]@{ Id = $p.id.id; Name = $p.name } }
        $page++
    } while ($r.hasNext)
    return $list
}

function Test-TbPackageExists([string]$ProfileId, [string]$Title, [string]$Version) {
    # Rückgabe: 'existiert' | 'bereit' | 'unbekannt'
    try {
        $r = Invoke-Tb GET "/api/otaPackages/$ProfileId/FIRMWARE?pageSize=500&page=0"
        foreach ($p in $r.data) {
            if ($p.title -eq $Title -and $p.version -eq $Version) { return 'existiert' }
        }
        return 'bereit'
    } catch {
        return 'unbekannt'
    }
}

function New-TbPackage([string]$Title, [string]$Version, [string]$ProfileId, [string]$Description) {
    $body = [ordered]@{
        title           = $Title
        version         = $Version
        tag             = "$Title $Version"
        type            = 'FIRMWARE'
        deviceProfileId = @{ entityType = 'DEVICE_PROFILE'; id = $ProfileId }
        additionalInfo  = @{ description = $Description }
    }
    return (Invoke-Tb POST '/api/otaPackage' $body).id.id
}

function Send-TbBinary([string]$PackageId, [string]$File, [string]$Sha256) {
    $client = New-Object System.Net.Http.HttpClient
    try {
        $client.Timeout = [TimeSpan]::FromMinutes(10)
        $client.DefaultRequestHeaders.Add('X-Authorization', "Bearer $($script:Token)")

        $content = New-Object System.Net.Http.MultipartFormDataContent
        $bytes   = [IO.File]::ReadAllBytes($File)
        $part    = [System.Net.Http.ByteArrayContent]::new($bytes)
        $part.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('application/octet-stream')
        $content.Add($part, 'file', [IO.Path]::GetFileName($File))

        $uri  = "$($script:Server)/api/otaPackage/$PackageId`?checksumAlgorithm=SHA256&checksum=$Sha256"
        $resp = $client.PostAsync($uri, $content).GetAwaiter().GetResult()
        if (-not $resp.IsSuccessStatusCode) {
            $txt = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            $msg = ''
            try { $msg = ($txt | ConvertFrom-Json).message } catch { }
            throw "HTTP $([int]$resp.StatusCode)$(if ($msg) { " - $msg" })"
        }
    } finally {
        $client.Dispose()
    }
}

function Remove-TbPackage([string]$PackageId) {
    try { [void](Invoke-Tb DELETE "/api/otaPackage/$PackageId") } catch { }
}

function Set-TbProfileFirmware([string]$ProfileId, [string]$PackageId) {
    $prof = Invoke-Tb GET "/api/deviceProfile/$ProfileId"
    $prof | Add-Member -NotePropertyName firmwareId `
                       -NotePropertyValue @{ entityType = 'OTA_PACKAGE'; id = $PackageId } -Force
    [void](Invoke-Tb POST '/api/deviceProfile' $prof)
}

# ============================================================
#  Übersicht aller Environments
# ============================================================
function Show-EnvTable($Infos) {
    Write-Host ('  {0,-4} {1,-52} {2,-26} {3,-10} {4,-17}' -f 'Nr', 'Environment', 'Version', 'Grösse', 'Build') -ForegroundColor DarkGray
    $i = 1
    foreach ($e in $Infos) {
        $ver  = $(if ($e.Version) { $e.Version } else { '-' })
        $size = $(if ($e.Exists)  { Format-Size $e.Size }  else { '-' })
        $time = $(if ($e.Exists)  { Format-Time $e.MTime } else { '-' })
        Write-Host ('  {0,-4} {1,-52} {2,-26} {3,-10} {4,-17} ' -f "$i)", $e.Env, $ver, $size, $time) -NoNewline
        if ($e.Warn.Count -gt 0) {
            $col = $(if (-not $e.Exists) { 'Red' } else { 'Yellow' })
            Write-Host ($e.Warn -join ', ') -ForegroundColor $col
        } else {
            Write-Host ''
        }
        $i++
    }
}

# ============================================================
#  Auswahl parsen: "1 3 5", "2-4", "a"
# ============================================================
function ConvertFrom-Selection([string]$Text, [int]$Max) {
    if ($Text -match '^\s*[aA]\s*$') { return 1..$Max }
    $nums = New-Object System.Collections.Generic.List[int]
    foreach ($part in ($Text -split '[,\s]+' | Where-Object { $_ })) {
        if ($part -match '^(\d+)-(\d+)$') {
            foreach ($n in ([int]$Matches[1])..([int]$Matches[2])) { if (-not $nums.Contains($n)) { $nums.Add($n) } }
        } elseif ($part -match '^\d+$') {
            if (-not $nums.Contains([int]$part)) { $nums.Add([int]$part) }
        } else {
            return @()
        }
    }
    return @($nums | Where-Object { $_ -ge 1 -and $_ -le $Max })
}

# ============================================================
#  Hauptprogramm
# ============================================================
function Invoke-Main {
    if (-not (Test-Path $IniPath)) { throw 'platformio.ini nicht gefunden - Skript in den Projektordner legen.' }

    $ini  = Read-PioIni $IniPath
    $envs = @($ini.Keys | Where-Object { $_ -like 'env:*' } | ForEach-Object { $_.Substring(4) })
    if ($envs.Count -eq 0) { throw 'Keine Environments in platformio.ini gefunden.' }

    $infos = @($envs | ForEach-Object { Get-EnvInfo $ini $_ })

    if ($Liste) {
        Write-Title 'Environments'
        Show-EnvTable $infos
        Write-Host ''
        return
    }

    # ════════════════════════════════════════════════════════
    #  1. Firmware auswählen
    # ════════════════════════════════════════════════════════
    Write-Title '1/4  Firmware auswählen'
    Show-EnvTable $infos
    Write-Host ''
    Write-Host 'Auswahl: Nummern (z.B. 1 3 5 oder 2-4), a = alle'

    do {
        $nums = @(ConvertFrom-Selection (Read-Host '>') $infos.Count)
        if ($nums.Count -eq 0) { Write-Warn 'Ungültige Auswahl.' }
    } while ($nums.Count -eq 0)

    $sel = @()
    foreach ($n in $nums) {
        $e = $infos[$n - 1]
        if (-not $e.Exists) {
            Write-Warn "$($e.Env): nicht gebaut - übersprungen (zuerst: pio run -e $($e.Env))"
            continue
        }
        if ($e.Warn.Count -gt 0) { Write-Warn "$($e.Env): $($e.Warn -join ', ')" }
        $sel += [pscustomobject]@{
            Env = $e.Env; Title = $e.Title; Version = $e.Version; Bin = $e.Bin
            Size = $e.Size; MTime = $e.MTime; ProfileId = $null; ProfileName = $null
            Sha = $null; Status = $null
        }
    }
    if ($sel.Count -eq 0) { throw 'Keine hochladbare Firmware ausgewählt.' }

    # ════════════════════════════════════════════════════════
    #  2. Anmelden
    # ════════════════════════════════════════════════════════
    Write-Title '2/4  Anmelden bei ThingsBoard'

    $settings = Get-Settings
    $script:Server = $TbServer.TrimEnd('/')
    Write-Host "Server: $($script:Server)"

    $credArgs = @{ Message = "ThingsBoard-Anmeldung ($($script:Server))" }
    if ($settings.user) { $credArgs.UserName = $settings.user }
    $cred = Get-Credential @credArgs
    if ($null -eq $cred) { throw 'Anmeldung abgebrochen.' }

    try {
        Connect-Tb $cred.UserName $cred.GetNetworkCredential().Password
    } catch {
        throw "Anmeldung fehlgeschlagen ($(Get-ApiError $_))"
    }
    Write-Ok "Angemeldet als $($cred.UserName)"
    $settings.user = $cred.UserName
    Save-Settings $settings

    try { $profiles = @(Get-TbProfiles) }
    catch { throw "Device-Profile konnten nicht geladen werden ($(Get-ApiError $_))" }
    if ($profiles.Count -eq 0) { throw 'Keine Device-Profile gefunden.' }

    # ════════════════════════════════════════════════════════
    #  3. Profil + Version je Firmware
    # ════════════════════════════════════════════════════════
    Write-Title '3/4  Device-Profil und Version'

    Write-Host 'Device-Profile:'
    for ($i = 0; $i -lt $profiles.Count; $i++) {
        Write-Host ('  {0,2}) {1}' -f ($i + 1), $profiles[$i].Name)
    }

    foreach ($s in $sel) {
        Write-Host ''
        Write-Host $s.Env -ForegroundColor White

        # ── Profil ────────────────────────────────────────────
        $defIdx = ''
        if ($settings.profiles.ContainsKey($s.Env)) {
            for ($i = 0; $i -lt $profiles.Count; $i++) {
                if ($profiles[$i].Name -eq $settings.profiles[$s.Env]) { $defIdx = "$($i + 1)" }
            }
        }
        while ($true) {
            $prompt = '  Profil-Nr'
            if ($defIdx) { $prompt += " [$defIdx = $($profiles[[int]$defIdx - 1].Name)]" }
            $a = Read-Host $prompt
            if ([string]::IsNullOrWhiteSpace($a)) { $a = $defIdx }
            if ($a -match '^\d+$' -and [int]$a -ge 1 -and [int]$a -le $profiles.Count) {
                $p = $profiles[[int]$a - 1]
                $s.ProfileId = $p.Id; $s.ProfileName = $p.Name
                $settings.profiles[$s.Env] = $p.Name
                break
            }
            Write-Warn 'Bitte eine Nummer aus der Liste eingeben.'
        }

        # ── Version ───────────────────────────────────────────
        do {
            $s.Version = Read-Default '  Version' $s.Version
            if (-not $s.Version) { Write-Warn 'Version darf nicht leer sein.' }
        } while (-not $s.Version)
    }
    Save-Settings $settings

    # ════════════════════════════════════════════════════════
    #  4. Kontrolle
    # ════════════════════════════════════════════════════════
    Write-Title '4/4  Kontrolle'
    Write-Host 'Daten werden gesammelt...'
    Write-Host ''

    foreach ($s in $sel) {
        $s.Sha    = (Get-FileHash $s.Bin -Algorithm SHA256).Hash.ToLower()
        $s.Status = Test-TbPackageExists $s.ProfileId $s.Title $s.Version
    }

    foreach ($s in $sel) {
        $col = switch ($s.Status) { 'existiert' { 'Red' } 'unbekannt' { 'Yellow' } default { 'Green' } }
        Write-Host "+ $($s.Env)" -ForegroundColor White
        Write-Host "|  Titel     : $($s.Title)"
        Write-Host '|  Version   : ' -NoNewline; Write-Host $s.Version -ForegroundColor White
        Write-Host "|  Profil    : $($s.ProfileName)"
        Write-Host "|  Datei     : $($s.Bin.Substring($ProjectDir.Length).TrimStart('\', '/'))"
        Write-Host "|  Grösse    : $(Format-Size $s.Size)"
        Write-Host "|  Build     : $(Format-Time $s.MTime)"
        Write-Host "|  SHA-256   : $($s.Sha)"
        Write-Host '+  Status    : ' -NoNewline; Write-Host $s.Status -ForegroundColor $col
        Write-Host ''
    }

    foreach ($s in $sel | Where-Object { $_.Status -eq 'existiert' }) {
        Write-Warn "$($s.Env): $($s.Title) $($s.Version) existiert bereits - wird übersprungen."
    }
    $todo = @($sel | Where-Object { $_.Status -ne 'existiert' })
    if ($todo.Count -eq 0) { throw 'Nichts hochzuladen.' }

    # ── Zuweisung ────────────────────────────────────────────
    Write-Host ''
    Write-Host 'Zuweisung ans Device-Profil startet den Rollout an ALLE Geräte des Profils.' -ForegroundColor Yellow
    $assign = Read-YesNo 'Pakete nach dem Upload dem Profil zuweisen?'
    if ($assign) {
        $dup = $todo | Group-Object ProfileId | Where-Object { $_.Count -gt 1 } | Select-Object -First 1
        if ($dup) {
            $names = ($dup.Group | ForEach-Object { $_.Env }) -join ' und '
            throw "$names haben dasselbe Profil ($($dup.Group[0].ProfileName)) - Zuweisung nicht eindeutig."
        }
    }

    Write-Host ''
    $q = "$($todo.Count) Paket(e) hochladen"
    if ($assign) { $q += ' und zuweisen' }
    if (-not (Read-YesNo "${q}?")) { throw 'Abgebrochen.' }

    # ════════════════════════════════════════════════════════
    #  Upload
    # ════════════════════════════════════════════════════════
    Write-Title 'Upload'

    $okCount = 0; $failCount = 0
    foreach ($s in $todo) {
        Write-Host "$($s.Env) -> $($s.Title) $($s.Version)" -ForegroundColor White

        try {
            $pkg = New-TbPackage $s.Title $s.Version $s.ProfileId "$($s.Env) - Build $(Format-Time $s.MTime)"
        } catch {
            Write-Err "Paket anlegen fehlgeschlagen ($(Get-ApiError $_))"
            $failCount++; continue
        }
        Write-Host "  Paket angelegt: $pkg"

        try {
            Send-TbBinary $pkg $s.Bin $s.Sha
        } catch {
            Write-Err "Upload fehlgeschlagen ($($_.Exception.Message)) - Paket wird gelöscht"
            Remove-TbPackage $pkg
            $failCount++; continue
        }
        Write-Ok "Binary hochgeladen ($(Format-Size $s.Size))"

        if ($assign) {
            try {
                Set-TbProfileFirmware $s.ProfileId $pkg
                Write-Ok "Profil $($s.ProfileName) zugewiesen - Rollout läuft"
            } catch {
                Write-Err "Zuweisung fehlgeschlagen ($(Get-ApiError $_)) - Paket bleibt ohne Zuweisung"
            }
        }
        $okCount++
    }

    Write-Host ''
    Write-Host 'Fertig: ' -NoNewline
    Write-Host "$okCount erfolgreich" -ForegroundColor Green -NoNewline
    Write-Host ', ' -NoNewline
    Write-Host "$failCount fehlgeschlagen" -ForegroundColor $(if ($failCount) { 'Red' } else { 'Green' })
}

# ============================================================
#  Start
# ============================================================
if ($MyInvocation.InvocationName -ne '.') {
    try {
        Invoke-Main
    } catch {
        Write-Err $_.Exception.Message
    }
    if (-not $Liste) {
        Write-Host ''
        [void](Read-Host 'Enter zum Schliessen')
    }
}
