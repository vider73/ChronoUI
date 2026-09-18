<#
.SYNOPSIS
    Launch a ChronoUI app, wait for its window, capture it to a PNG, close it.

.DESCRIPTION
    The screenshot helper behind docs/screenshots/. It starts an executable
    (optionally with environment overrides so an app runs against a scratch
    profile instead of the user's real %APPDATA%), waits for its main window,
    parks it at a fixed position, optionally replays a small script of typing /
    key presses / clicks / pauses (so a chat can be driven to a real state), and
    saves the window's own pixels (PrintWindow, so a covered window still
    captures correctly and nothing else on screen can leak in) as PNG.

    Works on scaled monitors: the script makes itself per-monitor DPI aware so
    window rectangles are physical pixels, and the capture is cropped in the
    window's own (unscaled) pixels, so a DPI-unaware app comes out crisp.

    Synthetic input is guarded: every click, drag and key press first checks
    that the window under the cursor and the foreground window are the one we
    launched, and the step is refused (the script throws) when they are not -
    a browser in front of the example must never receive our clicks.

.PARAMETER Exe
    Path to the executable to launch.
.PARAMETER Out
    PNG file to write.
.PARAMETER Wait
    Seconds to wait after the window appears before shooting (default 3).
.PARAMETER Width / Height
    If given, the window is resized to this client-ish size before the shot.
.PARAMETER Env
    Hashtable of environment overrides for the child process only
    (e.g. @{ APPDATA = 'C:\Temp\demo\appdata' }).
.PARAMETER Steps
    Array of hashtables replayed in order once the window is up and focused:
      @{ type = 'text' }            SendKeys text (SendKeys escaping applies)
      @{ keys = '{ENTER}' }         SendKeys key expression
      @{ wait = 5 }                 sleep seconds
      @{ click = @(x, y) }          left click at client-relative coordinates
      @{ drag = @(x0,y0,x1,y1) }    press, glide, release (add hold = $true to keep
                                    the button down, then @{ release = $true } later)
      @{ shot = 'name.png' }        intermediate capture (same folder as -Out)
      @{ close = $true }            close the window early (WM_CLOSE)
      @{ record = 'x.gif'; fps = 12; width = 800 }   start recording frames
      @{ stop = $true }             stop and assemble the GIF (tools/gif.py)
.PARAMETER Keep
    Leave the app running after the final capture.

.EXAMPLE
    .\tools\shoot.ps1 -Exe build\Release\Welcome.exe -Out docs\screenshots\welcome.png

.EXAMPLE
    .\tools\shoot.ps1 -Exe build\Release\Chat.exe -Out docs\screenshots\chat.png `
        -Env @{ APPDATA = 'C:\Temp\chronochat_demo' } -Width 1400 -Height 860 `
        -Steps @(@{ type = 'What time is it?' }, @{ keys = '{ENTER}' }, @{ wait = 12 })
#>
[CmdletBinding()]
param(
    [string]$Exe = '',
    [Parameter(Mandatory)] [string]$Out,
    [double]$Wait = 3,
    [int]$Width = 0,
    [int]$Height = 0,
    [int]$X = 40,
    [int]$Y = 40,
    [hashtable]$Env = @{},
    [object[]]$Steps = @(),
    [string]$Args = '',
    [string]$Title = '',
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type -Namespace Shoot -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
public static string Describe(IntPtr h) { var t = new System.Text.StringBuilder(256); GetWindowTextW(h, t, 256); var c = new System.Text.StringBuilder(256); GetClassNameW(h, c, 256); uint pid; GetWindowThreadProcessId(h, out pid); return h + ":" + c + ":'" + t + "':pid" + pid; }
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
[DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
[DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int cb);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
[DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr mon, int type, out uint dx, out uint dy);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
public static IntPtr FindByTitle(string title) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
        var sb = new System.Text.StringBuilder(512); GetWindowTextW(h, sb, 512);
        if (sb.ToString() == title) { found = h; return false; }
        return true;
    }, IntPtr.Zero);
    return found;
}
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
public static System.Collections.Generic.List<IntPtr> TopLevelWindowsOf(uint pid) {
    var list = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, l) => {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p == pid && IsWindowVisible(h) && GetWindow(h, 4) == IntPtr.Zero) list.Add(h);   // GW_OWNER == 4
        return true;
    }, IntPtr.Zero);
    return list;
}
'@

# Per-monitor DPI aware v2 (-4) so every rect below is in physical pixels.
[void][Shoot.Native]::SetProcessDpiAwarenessContext([IntPtr](-4))

function Pick-Largest($wins) {
    $best = [IntPtr]::Zero; $bestA = -1
    foreach ($w in $wins) {
        $r = New-Object Shoot.Native+RECT; [void][Shoot.Native]::GetWindowRect($w, [ref]$r)
        $a = ($r.R - $r.L) * ($r.B - $r.T)
        if ($a -gt $bestA) { $bestA = $a; $best = $w }
    }
    return $best
}
function Resolve-Full([string]$p) { [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $p)) }
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Resolve-Full $Out }
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$proc = $null; $hwnd = [IntPtr]::Zero; $wasHidden = $false
if ($Title) {
    # Attach to a window that already exists (e.g. a tray app's panel).
    $hwnd = [Shoot.Native]::FindByTitle($Title)
    if ($hwnd -eq [IntPtr]::Zero) { throw "no window titled '$Title'" }
    $wasHidden = -not [Shoot.Native]::IsWindowVisible($hwnd)
    if ($wasHidden) { [void][Shoot.Native]::ShowWindow($hwnd, 5); Start-Sleep -Milliseconds 600 }   # SW_SHOW
    if (-not [Shoot.Native]::IsWindowVisible($hwnd)) {
        # Some apps (tray panels) hide themselves again on deactivate. Never
        # CopyFromScreen a rect that holds someone else's desktop.
        throw "window '$Title' is not visible - open it by hand and re-run with -Title"
    }
} else {
$exePath = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { Resolve-Full $Exe }
if (-not (Test-Path $exePath)) { throw "exe not found: $exePath" }

# --- launch with environment overrides scoped to the child -----------------
$saved = @{}
foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, [string]$Env[$k]) }
try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath
    $psi.Arguments = $Args
    $psi.WorkingDirectory = Split-Path $exePath
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
} finally {
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
}

# --- wait for a visible top-level window ------------------------------------
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    if ($proc.HasExited) { throw "process exited early (code $($proc.ExitCode))" }
    $wins = [Shoot.Native]::TopLevelWindowsOf([uint32]$proc.Id)
    if ($wins.Count -gt 0) { $hwnd = Pick-Largest $wins; break }
}
if ($hwnd -eq [IntPtr]::Zero) { $proc.Kill(); throw "no window appeared within 30 s" }
}

# Park + size. SWP_NOZORDER=4, SWP_SHOWWINDOW=0x40; keep size when 0.
$flags = 0x44; if ($Width -le 0 -or $Height -le 0) { $flags = $flags -bor 0x1 }   # SWP_NOSIZE
[void][Shoot.Native]::ShowWindow($hwnd, 9)   # SW_RESTORE (in case the app starts maximized)
[void][Shoot.Native]::SetWindowPos($hwnd, [IntPtr]::Zero, $X, $Y, $Width, $Height, $flags)
[void][Shoot.Native]::SetForegroundWindow($hwnd)
Start-Sleep -Seconds $Wait

# --- frame recording (for GIFs) ---------------------------------------------
# @{ record = 'name.gif'; fps = 12; width = 800 } starts capturing the window
# every 1/fps s while later steps run (waits, drags, clicks all tick it);
# @{ stop = $true } ends it and assembles the GIF with tools/gif.py (Pillow).
$script:rec = $null
function Rec-Tick([IntPtr]$h) {
    if (-not $script:rec) { return }
    $now = $script:rec.watch.Elapsed.TotalSeconds
    if ($now - $script:rec.last -lt (1.0 / $script:rec.fps)) { return }
    $script:rec.last = $now
    $file = Join-Path $script:rec.dir ('{0:D4}.png' -f $script:rec.n)
    $script:rec.n++
    Save-Frame $h $file
}
function Rec-Wait([IntPtr]$h, [double]$seconds) {
    if (-not $script:rec) { Start-Sleep -Seconds $seconds; return }
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) { Rec-Tick $h; Start-Sleep -Milliseconds 15 }
}

function Get-Bounds([IntPtr]$h) {
    $r = New-Object Shoot.Native+RECT
    $hr = [Shoot.Native]::DwmGetWindowAttribute($h, 9, [ref]$r, [System.Runtime.InteropServices.Marshal]::SizeOf($r))  # DWMWA_EXTENDED_FRAME_BOUNDS
    if ($hr -ne 0) { [void][Shoot.Native]::GetWindowRect($h, [ref]$r) }
    return $r
}
# Grab the WINDOW's own pixels with PrintWindow(PW_RENDERFULLCONTENT), not the
# screen region under it: an app that gets covered mid-run (the user opened a
# browser on top) still yields its own frame, and nothing else ever leaks into
# a capture. The result is cropped to the DWM extended-frame bounds so the
# invisible resize borders are gone.
function Grab-Window([IntPtr]$h) {
    $wr = New-Object Shoot.Native+RECT; [void][Shoot.Native]::GetWindowRect($h, [ref]$wr)
    $w = $wr.R - $wr.L; $hh = $wr.B - $wr.T
    if ($w -le 0 -or $hh -le 0) { return $null }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [Shoot.Native]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    if (-not $ok) { $bmp.Dispose(); throw "PrintWindow failed for the window" }
    # A DPI-unaware app (ChronoUI's default) paints at 96 dpi and DWM scales it
    # on screen; PrintWindow gives the unscaled surface in the top-left corner of
    # the physical-size bitmap. Crop in the window's own pixels: native, crisp.
    $mdpi = [uint32]96; $dy = [uint32]0
    $mon = [Shoot.Native]::MonitorFromWindow($h, 2)
    if ([Shoot.Native]::GetDpiForMonitor($mon, 0, [ref]$mdpi, [ref]$dy) -ne 0) { $mdpi = 96 }
    $k = [Shoot.Native]::GetDpiForWindow($h) / [double]$mdpi
    if ($k -le 0 -or $k -gt 1) { $k = 1 }
    $er = Get-Bounds $h
    $crop = New-Object System.Drawing.Rectangle ([int][Math]::Round(($er.L - $wr.L) * $k)), ([int][Math]::Round(($er.T - $wr.T) * $k)), ([int][Math]::Round(($er.R - $er.L) * $k)), ([int][Math]::Round(($er.B - $er.T) * $k))
    if ($crop.X -lt 0 -or $crop.Y -lt 0 -or $crop.Right -gt $w -or $crop.Bottom -gt $hh) { return $bmp }
    $out = $bmp.Clone($crop, $bmp.PixelFormat); $bmp.Dispose()
    return $out
}
function Save-Frame([IntPtr]$h, [string]$file) {
    $bmp = Grab-Window $h
    if ($bmp) { $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose() }
}
function Save-Shot([IntPtr]$h, [string]$file) {
    $r = Get-Bounds $h
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if (($w -le 0 -or $hh -le 0) -and $script:proc) {
        # The window we latched onto (a splash, say) is gone: re-pick.
        $again = [Shoot.Native]::TopLevelWindowsOf([uint32]$script:proc.Id)
        if ($again.Count -gt 0) {
            $script:hwnd = Pick-Largest $again; $h = $script:hwnd
            [void][Shoot.Native]::SetWindowPos($h, [IntPtr]::Zero, $X, $Y, $Width, $Height, $flags)
            [void][Shoot.Native]::SetForegroundWindow($h); Start-Sleep -Milliseconds 400
            $r = Get-Bounds $h; $w = $r.R - $r.L; $hh = $r.B - $r.T
        }
    }
    if ($w -le 0 -or $hh -le 0) { throw "empty window rect" }
    $bmp = Grab-Window $h
    if (-not $bmp) { throw "could not grab the window" }
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host ("shot  {0}  ({1}x{2})" -f $file, $bmp.Width, $bmp.Height)
    $bmp.Dispose()
}
# Press at (x0,y0), glide to (x1,y1) over ~0.4 s, release unless -hold (then a
# later @{ release = $true } step lets a shot capture the mid-drag state).
function Drag-To([IntPtr]$h, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [bool]$hold, [double]$seconds = 0.4) {
    $p = New-Object Shoot.Native+POINT; $p.X = $x0; $p.Y = $y0; [void][Shoot.Native]::ClientToScreen($h, [ref]$p)
    $q = New-Object Shoot.Native+POINT; $q.X = $x1; $q.Y = $y1; [void][Shoot.Native]::ClientToScreen($h, [ref]$q)
    Assert-Ours $h $p.X $p.Y
    [Shoot.Native]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
    $steps = [Math]::Max(8, [int]($seconds * 60))
    for ($i = 1; $i -le $steps; $i++) {
        $t = $i / $steps
        $t = $t * $t * (3 - 2 * $t)                                   # ease in-out, like a hand
        [void][Shoot.Native]::SetCursorPos([int]($p.X + ($q.X - $p.X) * $t), [int]($p.Y + ($q.Y - $p.Y) * $t))
        Rec-Tick $h
        Start-Sleep -Milliseconds ([int](1000 * $seconds / $steps))
    }
    Rec-Wait $h 0.15
    if (-not $hold) { [Shoot.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200 }   # LEFTUP
}
# SAFETY: synthetic input goes to whatever is in front, and the user's own
# windows may be in front of the one we launched (a browser over the example
# ate a click, and another run found the cursor moved by a hand on the mouse).
# So before pressing anything: bring our window forward, put the cursor where
# the step wants it, and verify that the window under the cursor AND the
# foreground window are ours. If not, the step is refused - never pressed.
function Assert-Ours([IntPtr]$h, [int]$sx, [int]$sy) {
    for ($try = 0; $try -lt 3; ++$try) {
        [void][Shoot.Native]::SetForegroundWindow($h)
        [void][Shoot.Native]::SetCursorPos($sx, $sy)
        Start-Sleep -Milliseconds 120
        $q = New-Object Shoot.Native+POINT; [void][Shoot.Native]::GetCursorPos([ref]$q)
        $under = [Shoot.Native]::GetAncestor([Shoot.Native]::WindowFromPoint($q), 2)   # GA_ROOT
        $fg = [Shoot.Native]::GetForegroundWindow()
        if ($q.X -eq $sx -and $q.Y -eq $sy -and $under -eq $h -and $fg -eq $h) { return }
        Start-Sleep -Milliseconds 300
    }
    throw ("input refused: cursor at ({0},{1}) wanted ({2},{3}); under = {4}; foreground = {5}. Another window is in the way or the mouse is in use." -f $q.X, $q.Y, $sx, $sy, [Shoot.Native]::Describe($under), [Shoot.Native]::Describe($fg))
}
function Assert-Foreground([IntPtr]$h) {
    for ($try = 0; $try -lt 3; ++$try) {
        [void][Shoot.Native]::SetForegroundWindow($h)
        Start-Sleep -Milliseconds 80
        if ([Shoot.Native]::GetForegroundWindow() -eq $h) { return }
        Start-Sleep -Milliseconds 300
    }
    throw ("keys refused: foreground is {0}, not our window." -f [Shoot.Native]::Describe([Shoot.Native]::GetForegroundWindow()))
}
function Click-At([IntPtr]$h, [int]$cx, [int]$cy) {
    $p = New-Object Shoot.Native+POINT; $p.X = $cx; $p.Y = $cy
    [void][Shoot.Native]::ClientToScreen($h, [ref]$p)
    Assert-Ours $h $p.X $p.Y
    [Shoot.Native]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
    [Shoot.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
    Start-Sleep -Milliseconds 200
}

# --- replay the script -------------------------------------------------------
$outDir = Split-Path $outPath
# A refused step throws; the app we launched must still go away (a stray
# instance keeps its .exe locked and the next build fails at link).
try {
foreach ($s in $Steps) {
    if ($s.ContainsKey('type'))  { Assert-Foreground $hwnd; [System.Windows.Forms.SendKeys]::SendWait([string]$s.type); continue }
    if ($s.ContainsKey('keys'))  { Assert-Foreground $hwnd; [System.Windows.Forms.SendKeys]::SendWait([string]$s.keys); Rec-Tick $hwnd; continue }
    if ($s.ContainsKey('wait'))  { Rec-Wait $hwnd ([double]$s.wait); continue }
    if ($s.ContainsKey('record')) {
        $dir = Join-Path $env:TEMP ('shoot_rec_' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
        New-Item -ItemType Directory -Force $dir | Out-Null
        $fps = if ($s.ContainsKey('fps')) { [double]$s.fps } else { 12.0 }
        $script:rec = @{ dir = $dir; out = (Join-Path $outDir ([string]$s.record)); fps = $fps;
                         width = $(if ($s.ContainsKey('width')) { [int]$s.width } else { 800 });
                         watch = [System.Diagnostics.Stopwatch]::StartNew(); last = -1.0; n = 0 }
        Rec-Tick $hwnd
        continue
    }
    if ($s.ContainsKey('stop')) {
        if ($script:rec) {
            Rec-Tick $hwnd
            $r = $script:rec; $script:rec = $null
            & python (Join-Path $PSScriptRoot 'gif.py') $r.dir $r.out --fps $r.fps --width $r.width
            Remove-Item -Recurse -Force $r.dir -ErrorAction SilentlyContinue
        }
        continue
    }
    if ($s.ContainsKey('click')) { Click-At $hwnd ([int]$s.click[0]) ([int]$s.click[1]); continue }
    if ($s.ContainsKey('drag'))  { Drag-To $hwnd ([int]$s.drag[0]) ([int]$s.drag[1]) ([int]$s.drag[2]) ([int]$s.drag[3]) ([bool]$s.hold) $(if ($s.ContainsKey('seconds')) { [double]$s.seconds } else { 0.4 }); continue }
    if ($s.ContainsKey('release')) { [Shoot.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Rec-Wait $hwnd 0.2; continue }
    if ($s.ContainsKey('shot'))  { Save-Shot $hwnd (Join-Path $outDir ([string]$s.shot)); continue }
    if ($s.ContainsKey('close')) { [void][Shoot.Native]::PostMessage($hwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Seconds 1; continue }
}
} catch {
    if ($proc -and -not $Keep -and -not $proc.HasExited) { $proc.Kill() }
    if ($script:rec) { Remove-Item -Recurse -Force $script:rec.dir -ErrorAction SilentlyContinue }
    throw
}
# Guard the process-exit checks when we attached instead of launching.
if (-not $proc) { $proc = [pscustomobject]@{ HasExited = $false } }

if (-not $proc -or -not $proc.HasExited) {
    [void][Shoot.Native]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 300
    Save-Shot $hwnd $outPath
}

if ($Title) {
    if ($wasHidden -and -not $Keep) { [void][Shoot.Native]::ShowWindow($hwnd, 0) }   # SW_HIDE, leave the app as found
} elseif (-not $Keep -and -not $proc.HasExited) {
    [void][Shoot.Native]::PostMessage($hwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
    if (-not $proc.WaitForExit(5000)) { $proc.Kill() }
}
