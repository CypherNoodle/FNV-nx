"""Extract a verified upstream source ZIP, preserving executable script bits."""
import argparse, hashlib, stat, zipfile
from pathlib import Path, PurePosixPath

def extract(archive, destination, expected):
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected.lower():
        raise ValueError("Source archive SHA256 mismatch")
    destination.mkdir(parents=True,exist_ok=True)
    base = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            rel = PurePosixPath(entry.filename)
            mode = entry.external_attr >> 16
            if rel.is_absolute() or ".." in rel.parts or stat.S_ISLNK(mode):
                raise ValueError("Unsafe source archive entry")
            target = destination.joinpath(*rel.parts)
            if not target.resolve().is_relative_to(base):
                raise ValueError("Source archive escape")
            if entry.is_dir(): target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                if not target.exists(): target.write_bytes(source.read(entry))
                target.chmod(0o755 if mode & 0o111 else 0o644)

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path); p.add_argument("destination",type=Path); p.add_argument("--sha256",required=True)
    a=p.parse_args(); extract(a.archive,a.destination,a.sha256)
