"""Verify the silent process-owned moving-region fixture."""
import json
import subprocess
import os
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
result = json.loads((root / "artifacts/region-result.json").read_text(encoding="utf-8"))
engine = root / "tools/ffmpeg/bin/ffmpeg.exe"
if not engine.exists():
    engine = Path(os.environ["LOCALAPPDATA"]) / "Programs/FARecorder/bin/ffmpeg.exe"
decoded = subprocess.run([str(engine), "-hide_banner", "-nostdin", "-nostats", "-progress", "pipe:1",
    "-i", result["file"], "-map", "0:v:0", "-f", "null", "-"], capture_output=True, timeout=20)
assert decoded.returncode == 0, decoded.stderr.decode("utf-8", errors="replace")
metadata = decoded.stderr.decode("utf-8", errors="replace").split("Stream mapping:")[0]
assert len(re.findall(r"Stream #[^\n]*Video:", metadata)) == 1
assert not re.search(r"Stream #[^\n]*Audio:", metadata)
assert re.search(rf"\b{result['width']}x{result['height']}\b", metadata)
times = re.findall(rb"out_time_us=(\d+)", decoded.stdout)
assert times and 2.0 < int(times[-1]) / 1000000 < 2.5
assert result["recoveryCleaned"], "Saved recording left its Recovery session behind"
samples = []
for time, expected in ((0.3, "red"), (1.0, "blue"), (1.8, "red")):
    pixels = subprocess.check_output([
        str(engine), "-v", "error", "-ss", str(time),
        "-i", result["file"], "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"
    ])
    assert len(pixels) == result["width"] * result["height"] * 3
    selected = sum(
        (r > 180 and g < 80 and b < 80) if expected == "red" else
        (b > 180 and g < 80 and r < 80)
        for r, g, b in zip(pixels[::3], pixels[1::3], pixels[2::3])
    ) / (len(pixels) / 3)
    assert selected > 0.98, (time, expected, selected)
    samples.append({"seconds": time, "expected": expected, "fraction": selected})
report = {"passed": True, "width": result["width"], "height": result["height"],
          "audioTracks": 0, "samples": samples, "dragVerified": result["dragVerified"]}
if result.get("nativeGpu"):
    assert result["compactCpuReadbacks"] == 0, result
report.update(nativeGpu=result.get("nativeGpu", False), compactCpuReadbacks=result.get("compactCpuReadbacks"))
(root / "artifacts/region-verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
