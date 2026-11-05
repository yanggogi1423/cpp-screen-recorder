"""Verify the pinned official NSIS toolchain; extract only into this workspace cache."""
import argparse
import hashlib
import html
import io
from pathlib import Path, PurePosixPath
import re
import urllib.parse
import urllib.request
import zipfile

SHA256 = 'ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8'
URL = 'https://sourceforge.net/projects/nsis/files/NSIS%203/3.13/nsis-3.13.zip/download'

def official(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.username or parsed.password or not parsed.hostname or not (
        parsed.hostname == 'sourceforge.net' or parsed.hostname.endswith('.sourceforge.net')):
        raise ValueError('NSIS tool download left the official HTTPS source')

class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, address):
        official(address)
        return super().redirect_request(request, fp, code, message, headers, address)

def check_path(path):
    for item in (path, *path.parents):
        try:
            reparse = bool(getattr(item.lstat(), 'st_file_attributes', 0) & 0x400)
        except FileNotFoundError:
            reparse = False
        if item.is_symlink() or reparse:
            raise ValueError('Linked tool cache refused')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, help='Optional existing official ZIP; still verified')
    args = parser.parse_args()
    cache = Path(__file__).resolve().parents[1] / '.deps/nsis'
    check_path(cache)
    archive = args.archive or cache / 'nsis-3.13-verified.zip'
    check_path(archive.absolute())
    if archive.exists():
        data = archive.read_bytes()
    else:
        address = URL
        for _ in range(4):
            official(address)
            with urllib.request.build_opener(Redirects()).open(address, timeout=60) as response:
                official(response.url)
                data = response.read(8 * 1024 * 1024 + 1)
            if data.startswith(b'PK\x03\x04'):
                break
            match = re.search(r'<meta\b[^>]*http-equiv=["\']refresh["\'][^>]*content=["\'][^"\']*?url=([^"\']+)', data.decode('utf-8'), re.I)
            if not match:
                raise ValueError('Official source returned HTML instead of a tool ZIP')
            address = html.unescape(match[1])
        else:
            raise ValueError('Official source did not return the tool ZIP')
    if len(data) != 2363925 or hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Pinned NSIS size/SHA256 mismatch; no tool executed')
    cache.mkdir(parents=True, exist_ok=True)
    saved = cache / 'nsis-3.13-verified.zip'
    if not saved.exists():
        saved.write_bytes(data)
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        for entry in package.infolist():
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or '..' in name.parts or '\\' in entry.filename or name.parts[0] != 'nsis-3.13':
                raise ValueError('Unsafe tool archive path')
            target = cache.joinpath(*name.parts)
            check_path(target)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            contents = package.read(entry)
            if target.exists():
                if target.read_bytes() != contents:
                    raise ValueError('Existing tool differs from verified official ZIP')
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(contents)
    print('PASS pinned official NSIS 3.13 and all extracted tool files')

if __name__ == '__main__':
    main()
