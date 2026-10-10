#!/usr/bin/env python3
"""Check first-party Markdown links and canonical navigation without network access."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from fnmatch import fnmatchcase
from html.parser import HTMLParser
import json
from pathlib import Path
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit
from urllib.request import Request, urlopen

from markdown_it import MarkdownIt

ROOT = Path(__file__).resolve().parents[1]
PARSER = MarkdownIt("commonmark")


def matches(path, patterns):
    return any(fnmatchcase(path, pattern) for pattern in patterns)


def slug(text):
    """GitHub heading IDs preserve Unicode letters, digits, marks, underscores/dashes."""
    text = text.lower()
    return ''.join('-' if char.isspace() else char for char in text
                   if char.isspace() or char in '_-' or
                   unicodedata.category(char)[0] in 'LNM')


class HTMLLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if attrs.get('id'):
            self.anchors.add(attrs['id'])
        if tag == 'a' and attrs.get('name'):
            self.anchors.add(attrs['name'])
        for attr in ('href', 'src'):
            if attrs.get(attr):
                self.links.append(attrs[attr])


def parse(text):
    tokens = PARSER.parse(text)
    anchors, links, headings = set(), [], []
    used = set()
    html = HTMLLinks()

    def inline(children):
        for child in children or []:
            if child.type == 'link_open':
                links.append(child.attrGet('href'))
            elif child.type == 'image':
                links.append(child.attrGet('src'))
            elif child.type == 'html_inline':
                html.feed(child.content)
            if child.children:
                inline(child.children)

    for index, token in enumerate(tokens):
        if token.type == 'heading_open':
            next_token = tokens[index + 1]
            title = ''.join(c.content for c in next_token.children or []
                            if c.type in ('text', 'code_inline', 'image'))
            base = slug(title)
            anchor, suffix = base, 0
            while anchor in used:
                suffix += 1
                anchor = f'{base}-{suffix}'
            used.add(anchor)
            anchors.add(anchor)
            headings.append((int(token.tag[1:]), title))
        elif token.type == 'html_block':
            html.feed(token.content)
        elif token.type == 'inline':
            inline(token.children)
    return anchors | html.anchors, links + html.links, headings


def documents(root):
    result = subprocess.run(['git', 'ls-files', '--cached', '--others',
                             '--exclude-standard', '-z', '--', '*.md', 'INSTALL', 'tools/README', 'debian/README.*'],
                            cwd=root, capture_output=True, check=True)
    # CLAUDE.md and .claude/skills are aliases, never separate canonical sources.
    return sorted({Path(p.decode()) for p in result.stdout.split(b'\0')
                   if p and (root / p.decode()).is_file()
                   and not (root / p.decode()).is_symlink()
                   and not p.decode().startswith('.claude/')})


def local_target(root, source, href):
    url = urlsplit(href)
    if url.scheme or url.netloc:
        return None
    path = unquote(url.path)
    target = root / path.lstrip('/') if path.startswith('/') else root / source.parent / path
    if not path:
        target = root / source
    return target.resolve(), unquote(url.fragment)


def check(root, paths, config):
    root = root.resolve()
    pages, errors, edges, external = {}, [], {}, set()
    for path in paths:
        name = path.as_posix()
        if matches(name, config.get('excluded', {})):
            continue
        pages[path] = parse((root / path).read_text(encoding='utf-8'))
    for source, (anchors, links, headings) in pages.items():
        name = source.as_posix()
        if name.startswith('docs/') and not matches(name, config.get('structure_exempt', {})):
            if not headings or headings[0][0] != 1 or sum(level == 1 for level, _ in headings) != 1:
                errors.append(f'{source}: expected one leading H1 title')
        edges[source] = []
        for href in links:
            if not href or matches(href, config.get('ignored_targets', {})):
                continue
            try:
                result = local_target(root, source, href)
            except ValueError as exc:
                errors.append(f'{source}: invalid target {href!r}: {exc}')
                continue
            if result is None:
                if href.startswith(('https://', 'http://')):
                    external.add(href)
                continue
            target, fragment = result
            try:
                relative = target.relative_to(root)
            except ValueError:
                errors.append(f'{source}: target escapes repository: {href}')
                continue
            if not target.exists():
                errors.append(f'{source}: missing target: {href}')
                continue
            if relative in pages:
                edges[source].append(relative)
            if fragment and (target.suffix == '.md' or relative in pages):
                target_anchors = pages[relative][0] if relative in pages else parse(target.read_text(encoding='utf-8'))[0]
                if fragment not in target_anchors:
                    errors.append(f'{source}: missing anchor: {href}')

    entry = Path('docs/README.md')
    if entry not in pages and (config.get('categories') or any(p.parts[0] == 'docs' for p in paths)):
        errors.append(f'{entry}: required main index missing from checked inventory')
    if entry in pages:
        visited, pending = set(), [entry]
        while pending:
            node = pending.pop()
            if node not in visited:
                visited.add(node)
                pending.extend(edges.get(node, []))
        for path in pages:
            if path.parts[0] == 'docs' and path not in visited and not matches(path.as_posix(), config.get('reachability_exempt', {})):
                errors.append(f'{path}: unreachable from docs/README.md')
        destinations = edges[entry]
        previous = -1
        for category in config.get('categories', []):
            hub = Path('docs') / category / 'README.md'
            if hub not in destinations:
                errors.append(f'{entry}: missing category index link: {hub}')
            elif destinations.index(hub) <= previous:
                errors.append(f'{entry}: category index out of order: {hub}')
            else:
                previous = destinations.index(hub)
    return errors, external, len(pages)


def check_external(urls):
    """Optional best-effort audit; network results are never part of default CI."""
    def audit(url):
        try:
            request = Request(url, headers={'User-Agent': 'Glob2 documentation audit'}, method='GET')
            with urlopen(request, timeout=10) as response:
                response.read(1)
        except Exception as exc:
            return f'{url}: {exc}'
        return None

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = pool.map(audit, sorted({u.split('#', 1)[0] for u in urls}))
        return [failure for failure in results if failure]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--external', action='store_true', help='also audit external links (network-dependent)')
    args = parser.parse_args()
    config = json.loads((args.root / 'tools/docs/navigation.json').read_text())
    errors, external, count = check(args.root, documents(args.root), config)
    if args.external:
        errors.extend(check_external(external))
    for error in errors:
        print(error, file=sys.stderr)
    print(f'Checked {count} Markdown documents; {len(errors)} errors; {len(external)} external links.')
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
