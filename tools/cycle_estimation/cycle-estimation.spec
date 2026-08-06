# PyInstaller spec for the standalone cycle-estimation tool.
#
# Freezes main.py into a self-contained console executable so the bundled GUI
# can run cycle estimation without a system Python. The gem5 data (CPU-model
# manifests and the reference SE-mode config script) is embedded and extracted
# under sys._MEIPASS/gem5 at runtime, where constants.py resolves GEM5_DIR.
# The m5op header and per-ABI shim are read from the user's gem5 source tree at
# run time (GEM5_HOME), so they are not bundled.

import os
from pathlib import Path

tool_dir = Path(os.environ.get("CE_TOOL_DIR", Path.cwd())).resolve()

a = Analysis(
    [str(tool_dir / "main.py")],
    pathex=[str(tool_dir)],
    binaries=[],
    datas=[(str(tool_dir / "gem5"), "gem5")],
    hiddenimports=[],
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="cycle-estimation",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
    disable_windowed_traceback=False,
)
