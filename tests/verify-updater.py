"""Install only into unique disposable fixture directories, never the real app."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

helper, good, bad = (Path(arg).resolve() for arg in sys.argv[1:4])
fixture_root = Path(__file__).resolve().parents[1] / "artifacts" / "updater-tests"
fixture_root.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="FARecorder update 검사 & ", dir=str(fixture_root)))
windows = os.name == "nt"
package = "FARecorder-Windows-x64" if windows else "FARecorder.app"
exe = Path("bin/FARecorder.exe" if windows else "Contents/MacOS/FARecorder")
creation = {"creationflags": subprocess.CREATE_NO_WINDOW} if windows else {}

def run(arguments, expected=0):
    result = subprocess.run([str(arg) for arg in arguments], capture_output=True, text=True,
                            timeout=45, **creation)
    assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
    return result

def make_app(path, version, binary):
    (path / exe).parent.mkdir(parents=True)
    shutil.copy2(binary, path / exe)
    (path / "farecorder-package.txt").write_text(f"FARecorder\n{version}\n", encoding="utf-8")
    run([helper, "--write-manifest", path, "--quiet"])

def archive(path, extra=None):
    target = path.parent / "download.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as output:
        for file in path.rglob("*"):
            if file.is_file():
                info = zipfile.ZipInfo(f"{package}/{file.relative_to(path).as_posix()}")
                info.create_system = 3
                info.external_attr = (file.stat().st_mode & 0xFFFF) << 16
                output.writestr(info, file.read_bytes())
        if extra:
            output.writestr(extra, b"should never be extracted")
    return target

results = []
for case in ("success", "checksum", "traversal", "rollback", "user-collision", "canceled", "live-parent", "parent-mismatch"):
    work = root / case
    work.mkdir()
    installed = work / "installed application"
    make_app(installed, "0.4.0", good)
    user = installed / "My recordings" / "user data.txt"
    user.parent.mkdir()
    user.write_text("preserve me", encoding="utf-8")
    if case == "user-collision":
        (installed / "new-file.txt").write_text("user-owned", encoding="utf-8")
    new = work / package
    make_app(new, "0.5.0-beta.1", bad if case == "rollback" else good)
    if case == "user-collision":
        (new / "new-file.txt").write_text("new application data", encoding="utf-8")
        run([helper, "--write-manifest", new, "--quiet"])
    downloaded = archive(new, "../escaped.txt" if case == "traversal" else None)
    updater = work / helper.name
    shutil.copy2(helper, updater)
    if case == "canceled":
        Path(str(updater) + ".cancel").write_text("cancel")
    checksum = hashlib.sha256(downloaded.read_bytes()).hexdigest()
    if case == "checksum":
        checksum = "0" * 64
    # An already-exited fixture is the only parent; no user's process is touched.
    parent = subprocess.Popen([str(installed / exe)] + ([] if case == "live-parent" else ["--exit-now"]), **creation)
    if case != "live-parent":
        parent.wait(timeout=10)
    args = [str(arg) for arg in [updater, "--apply", downloaded, installed, checksum,
            "0.5.0-beta.1", os.getpid() if case == "parent-mismatch" else parent.pid, "--quiet"]]
    if case == "live-parent":
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **creation)
        deadline = time.monotonic() + 10
        while not Path(str(updater) + ".ready").exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        assert process.poll() is None and parent.poll() is None, "Updater did not wait for the live app"
        assert "0.4.0" in (installed / "farecorder-package.txt").read_text()
        Path(str(updater) + ".cancel").write_text("cancel")
        process.communicate(timeout=15)
        assert process.returncode == 1
        parent.wait(timeout=10)
    else:
        run(args, expected=0 if case == "success" else 1)
    assert user.read_text(encoding="utf-8") == "preserve me", case
    marker = (installed / "farecorder-package.txt").read_text(encoding="utf-8")
    assert ("0.5.0-beta.1" if case == "success" else "0.4.0") in marker, case
    assert not (work / "escaped.txt").exists(), case
    if case == "success":
        backups = list(work.glob("installed application.backup-*"))
        assert len(backups) == 1 and "0.4.0" in (backups[0] / "farecorder-package.txt").read_text()
    if case == "user-collision":
        assert (installed / "new-file.txt").read_text() == "user-owned"
    results.append(case)

print(json.dumps({"passed": results, "fixtures": str(root)}, ensure_ascii=False))
