"""Package native Apple Info.plist localizations alongside engine catalogs."""
from pathlib import Path
import shutil


def localization_files(directories):
    """Return declared .lproj resources, failing rather than dropping a locale."""
    files = []
    for directory in directories:
        directory = Path(directory)
        if directory.suffix != '.lproj':
            continue
        resource = directory / 'InfoPlist.strings'
        if not resource.is_file():
            raise ValueError('Missing Apple localization: ' + str(resource))
        files.append(resource)
    return files


def copy_localizations(directories, destination):
    for resource in localization_files(directories):
        target = Path(destination) / resource.parent.name / resource.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(resource, target)
