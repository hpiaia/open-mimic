param(
    [int]$ProcessId,
    [ValidateSet('Activate','Click','Keys','Drop')][string]$Action = 'Activate',
    [int]$X = 0,
    [int]$Y = 0,
    [string]$Text = ''
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OpenMimicWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")] public static extern IntPtr GlobalAlloc(uint flags, UIntPtr bytes);
    [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr memory);
    [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr memory);
}
'@

$process = Get-Process -Id $ProcessId -ErrorAction Stop
[OpenMimicWin32]::ShowWindow($process.MainWindowHandle, 3) | Out-Null
[OpenMimicWin32]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 250

if ($Action -eq 'Click') {
    [OpenMimicWin32]::SetCursorPos($X, $Y) | Out-Null
    [OpenMimicWin32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [OpenMimicWin32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}
elseif ($Action -eq 'Keys') {
    [System.Windows.Forms.SendKeys]::SendWait($Text)
}
elseif ($Action -eq 'Drop') {
    # DROPFILES (20 bytes) followed by a UTF-16 double-NUL-terminated file list.
    $fileBytes = [System.Text.Encoding]::Unicode.GetBytes($Text + "`0`0")
    $size = 20 + $fileBytes.Length
    $memory = [OpenMimicWin32]::GlobalAlloc(0x0042, [UIntPtr]::new([uint64]$size))
    if ($memory -eq [IntPtr]::Zero) { throw 'GlobalAlloc failed' }
    $pointer = [OpenMimicWin32]::GlobalLock($memory)
    [Runtime.InteropServices.Marshal]::WriteInt32($pointer, 0, 20)
    [Runtime.InteropServices.Marshal]::WriteInt32($pointer, 4, $X)
    [Runtime.InteropServices.Marshal]::WriteInt32($pointer, 8, $Y)
    [Runtime.InteropServices.Marshal]::WriteInt32($pointer, 12, 0)
    [Runtime.InteropServices.Marshal]::WriteInt32($pointer, 16, 1)
    [Runtime.InteropServices.Marshal]::Copy($fileBytes, 0, [IntPtr]::Add($pointer, 20), $fileBytes.Length)
    [OpenMimicWin32]::GlobalUnlock($memory) | Out-Null
    [OpenMimicWin32]::PostMessage($process.MainWindowHandle, 0x0233, $memory, [IntPtr]::Zero) | Out-Null
}
