"""Verify the silent process-owned moving-region fixture."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
result = json.loads((root / "artifacts/region-result.json").read_text(encoding="utf-8"))
probe = json.loads(subprocess.check_output([
    str(root / "tools/ffmpeg/bin/ffprobe.exe"), "-v", "error", "-show_streams",
    "-show_format", "-of", "json", result["file"]
]))
video = [s for s in probe["streams"] if s["codec_type"] == "video"]
assert len(video) == 1 and not any(s["codec_type"] == "audio" for s in probe["streams"])
assert (video[0]["width"], video[0]["height"]) == (result["width"], result["height"])
assert 2.0 < float(probe["format"]["duration"]) < 2.5
samples = []
for time, expected in ((0.3, "red"), (1.0, "blue"), (1.8, "red")):
    pixels = subprocess.check_output([
        str(root / "tools/ffmpeg/bin/ffmpeg.exe"), "-v", "error", "-ss", str(time),
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
(root / "artifacts/region-verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
