"""Fetch an immutable build dependency and reject changed cached source."""
from pathlib import Path
import re
import subprocess


def checkout(url: str, commit: str, destination: Path):
    """Build from the expected commit only; never reset a modified cache silently."""
    destination = Path(destination)
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("dependency must use an immutable full Git commit")
    if not (destination / '.git').exists():
        if destination.exists() and any(destination.iterdir()):
            raise RuntimeError(f"nonempty dependency directory: {destination}")
        destination.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', '-C', str(destination), 'init', '-q'], check=True)
        subprocess.run(['git', '-C', str(destination), 'fetch', '--depth', '1', url, commit], check=True)
        subprocess.run(['git', '-C', str(destination), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)
    head = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
    status = subprocess.check_output(['git', '-C', str(destination), 'status', '--porcelain'], text=True)
    if head != commit or status.strip():
        raise RuntimeError(f"dependency cache has the wrong commit or modified source: {destination}")
    return head
