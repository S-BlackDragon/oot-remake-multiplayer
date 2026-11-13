"""Screenshot of a process's main window without focusing it (Win32 PrintWindow with PW_RENDERFULLCONTENT, which
copies what DWM composes, so a Vulkan swapchain shows up too). No third-party modules.

    python -I tools/harness/captura.py <pid> <out.png>

Exit 0 and the PNG written, or exit 1 with a reason (no visible window for that pid, PrintWindow failed).

Used by --capture-marks in r03_pareja.py.
"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, tempfile
from pathlib import Path

user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
PW_RENDERFULLCONTENT = 0x2


def main_window(pid: int) -> int:
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            if r.right - r.left > 200 and r.bottom - r.top > 100:
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else 0


def capture(hwnd: int, out: Path) -> str:
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    ok = user32.PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)
    if not ok:
        return "PrintWindow failed"

    class BMPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BMPINFOHEADER(ctypes.sizeof(BMPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    size = w * h * 4
    buf = ctypes.create_string_buffer(size)
    if gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0) == 0:
        return "GetDIBits failed"
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    # BMP on disk (14-byte file header + the info header + BGRA rows), then PNG via .NET, which every Windows has
    bmp_path = Path(tempfile.gettempdir()) / (out.stem + ".bmp")
    header = b"BM" + (14 + 40 + size).to_bytes(4, "little") + b"\0\0\0\0" + (54).to_bytes(4, "little")
    bmp_path.write_bytes(header + bytes(bi) + buf.raw)
    ps = (f"Add-Type -AssemblyName System.Drawing; $b=[System.Drawing.Bitmap]::FromFile('{bmp_path}'); "
          f"$b.Save('{out}', [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()")
    p = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True)
    bmp_path.unlink(missing_ok=True)
    return "" if p.returncode == 0 and out.exists() else f"png conversion failed: {p.stderr[-200:]}"


if __name__ == "__main__":
    pid, out = int(sys.argv[1]), Path(sys.argv[2])
    hwnd = main_window(pid)
    if not hwnd:
        print(f"captura: no visible window for pid {pid}")
        sys.exit(1)
    err = capture(hwnd, out)
    print(f"captura: {out}" if not err else f"captura: {err}")
    sys.exit(1 if err else 0)
