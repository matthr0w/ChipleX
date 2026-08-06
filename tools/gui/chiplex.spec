# PyInstaller spec for the ChipleX GUI release bundle.
#
# Bundles the GUI together with a staged "framework" tree (sim, SystemC, headers,
# configs, setups, vendored yaml-cpp, cycle-estimation tool). The framework
# tree is added as opaque data so the prebuilt sim keeps its $ORIGIN rpath; at
# runtime it lands at <_MEIPASS>/framework, which Project.discover() locates.

import os
from pathlib import Path

repo_root = Path(os.environ.get("REPO_ROOT", Path.cwd())).resolve()
stage = Path(os.environ.get("RELEASE_STAGE", repo_root / "dist" / "framework")).resolve()
gui_dir = repo_root / "tools" / "gui"

a = Analysis(
    [str(gui_dir / "main.py")],
    pathex=[str(gui_dir)],
    binaries=[],
    datas=[(str(stage), "framework")],
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
    name="chiplex",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
    disable_windowed_traceback=False,
)
