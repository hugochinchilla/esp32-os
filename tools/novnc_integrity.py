#!/usr/bin/env python3
"""Writes core/src/net/novnc_files.inc: the hash of every noVNC file the
/novnc page loads, as the "integrity" part of an import map. The browser
then refuses any of those files if the CDN serves something else.

  git clone https://github.com/novnc/noVNC.git && git -C noVNC checkout <commit>
  python3 tools/novnc_integrity.py noVNC <commit>

Run it again, with the new commit, whenever the pinned noVNC changes; the
commit in the page (core/src/net/viewer_page.cpp) must be the same one.
"""
import base64
import hashlib
import os
import posixpath
import re
import subprocess
import sys

IMPORT = re.compile(r"""(?:^|[;\s])(?:import|export)\s[^'";]*?from\s*['"]([^'"]+)['"]|(?:^|[;\s])import\s*['"]([^'"]+)['"]|import\(\s*['"]([^'"]+)['"]\s*\)""", re.M)


def main():
    clone, commit = sys.argv[1], sys.argv[2]
    head = subprocess.run(["git", "-C", clone, "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
    assert head.stdout.strip() == commit, "the clone is at %s" % head.stdout.strip()
    dirty = subprocess.run(["git", "-C", clone, "status", "--porcelain"], capture_output=True, text=True, check=True)
    assert not dirty.stdout.strip(), "the clone has changes"
    found, todo = {}, ["core/rfb.js"]
    while todo:
        name = todo.pop()
        if name in found:
            continue
        data = open(os.path.join(clone, name), "rb").read()
        found[name] = "sha384-" + base64.b64encode(hashlib.sha384(data).digest()).decode()
        for match in IMPORT.finditer(data.decode()):
            target = match.group(1) or match.group(2) or match.group(3)
            assert target.startswith("."), "%s imports %s" % (name, target)
            todo.append(posixpath.normpath(posixpath.join(posixpath.dirname(name), target)))
    base = "https://cdn.jsdelivr.net/gh/novnc/noVNC@%s/" % commit
    lines = ['"%s%s":"%s"' % (base, name, found[name]) for name in sorted(found)]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core", "src", "net", "novnc_files.inc")
    with open(out, "w") as f:
        f.write("// Made by tools/novnc_integrity.py from noVNC at commit\n// %s (%d files). Do not edit.\n" % (commit, len(found)))
        f.write('R"files(' + ",\n".join(lines) + '\n)files"\n')
    print("%d files" % len(found))


if __name__ == "__main__":
    main()
