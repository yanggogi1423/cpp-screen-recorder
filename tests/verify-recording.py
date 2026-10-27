"""Verify only the synthetic --capture-test fixture; no external dependencies."""
import array
import json
import math
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
result_path=Path(sys.argv[1]) if len(sys.argv)>1 else root / "artifacts/capture-result.json"
result = json.loads(result_path.read_text(encoding="utf-8"))
folder = Path(result["file"]).parent
tracks = []
for path in sorted(folder.glob("*.wav")):
    raw = path.read_bytes()
    assert raw[:4] == b"RF64"
    samples = array.array("h")
    samples.frombytes(raw[80:])
    mono = samples[::2]

    def rms(start, end):
        values = mono[int(start * 48000):int(end * 48000)]
        return math.sqrt(sum(x * x for x in values) / max(1, len(values)))

    def amplitude(hz):
        values = mono[48000:54000]
        n = len(values)
        return 2 * abs(sum(x * complex(math.cos(2 * math.pi * hz * i / 48000),
                                       -math.sin(2 * math.pi * hz * i / 48000))
                           for i, x in enumerate(values))) / n

    tracks.append(dict(name=path.name, rms_before=rms(.8, 1.6),
                       rms_after_gain=rms(2, 2.6), rms_mute=rms(3.05, 3.65),
                       rms_unmute=rms(4, 4.6), amp440=amplitude(440),
                       amp880=amplitude(880), frames=len(mono)))

assert len(tracks) == 2
assert .45 < tracks[0]["rms_after_gain"] / tracks[0]["rms_before"] < .55
assert tracks[1]["rms_mute"] < 1
assert tracks[1]["rms_unmute"] > 100
assert tracks[0]["amp440"] > tracks[0]["amp880"] * 20
assert tracks[1]["amp880"] > tracks[1]["amp440"] * 20

probe = subprocess.run([str(root / "tools/ffmpeg/bin/ffprobe.exe"), "-v", "error",
                        "-show_streams", "-show_format", "-of", "json", result["file"]],
                       check=True, capture_output=True, encoding="utf-8")
metadata = json.loads(probe.stdout)
video = [s for s in metadata["streams"] if s["codec_type"] == "video"]
audio = [s for s in metadata["streams"] if s["codec_type"] == "audio"]
assert len(video) == 1 and len(audio) == 3, "Expected video + default mix + two independent tracks"
assert video[0]["width"] == 640 and video[0]["height"] == 360
assert video[0]["r_frame_rate"] == "30/1"
assert 4.9 < float(metadata["format"]["duration"]) < 5.3
assert audio[0]["disposition"]["default"] == 1
mixed = subprocess.run([str(root / "tools/ffmpeg/bin/ffmpeg.exe"), "-v", "error",
                        "-ss", "1", "-i", result["file"], "-map", "0:a:0", "-t", "0.125",
                        "-f", "s16le", "-ac", "2", "-ar", "48000", "-"],
                       check=True, capture_output=True).stdout
mix_samples = array.array("h")
mix_samples.frombytes(mixed)
mix_mono = mix_samples[::2]
mix_amplitudes = {}
for hz in (440, 880):
    amp = 2 * abs(sum(x * complex(math.cos(2 * math.pi * hz * i / 48000),
                                -math.sin(2 * math.pi * hz * i / 48000))
                      for i, x in enumerate(mix_mono))) / len(mix_mono)
    assert amp > 100, "Default mix lost an application route"
    mix_amplitudes[str(hz)] = amp

# Hash every decoded frame to detect a frozen native capture path.
hashes = subprocess.run([str(root / "tools/ffmpeg/bin/ffmpeg.exe"), "-v", "error",
                         "-i", result["file"], "-map", "0:v:0", "-f", "framemd5", "-"],
                        check=True, capture_output=True, encoding="utf-8").stdout
distinct = {line.rsplit(",", 1)[-1].strip() for line in hashes.splitlines() if not line.startswith("#")}
assert len(distinct) > 10, "Native capture did not update animated test frames"
report = dict(passed=True, tracks=tracks, video=video, audio_tracks=len(audio), distinct_frames=len(distinct), mix_amplitudes=mix_amplitudes)
(root / "artifacts/audio-verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
if "pauseObserved" in result:
    assert result["pauseObserved"] and result["encoderPassed"] and result["hotkeysRegistered"]
    manifest=json.loads((folder / "session.json").read_text(encoding="utf-8"))
    assert .85<float(manifest["pausedSeconds"])<1.2
    audition=(root / "artifacts/audition.wav").read_bytes()
    assert audition[:4]==b"RF64" and len(audition)>=80+48000*4*4.9
    report["pausedSeconds"]=manifest["pausedSeconds"]
    report["auditionDuration"]=(len(audition)-80)/192000
    (root / "artifacts/workflow-verification.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps(report, indent=2))
