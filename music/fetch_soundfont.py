"""Download the exact GeneralUser GS bank recorded in soundfonts/manifest.json."""
from pathlib import Path
import hashlib
import json
import urllib.request

def main():
    root = Path(__file__).resolve().parent/'soundfonts'
    manifest = json.loads((root/'manifest.json').read_text())
    target = root/manifest['file']
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == manifest['sha256']:
        print('Sound bank already verified')
    else:
        temporary = target.with_suffix('.download')
        try:
            with urllib.request.urlopen(manifest['url'],timeout=60) as response, temporary.open('wb') as output:
                while chunk := response.read(1024*1024): output.write(chunk)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != manifest['sha256']:
                raise ValueError('Sound bank checksum mismatch')
            temporary.replace(target)
            print('Downloaded and verified',target.name)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
