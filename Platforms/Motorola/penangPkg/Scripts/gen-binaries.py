#!/usr/bin/env python3
"""Build Mu-Silicium Binaries/<device> from a stock XBL FV dump (UEFIExtract 'all'), using another device's Binaries
tree as the layout: for each template .inf, the stock module with the same FILE_GUID (else the same name) provides the
PE32/TE/RAW and DXE_DEPEX bodies; the .inf is copied with its FILE_GUID kept.
  gen-binaries.py DUMP_DIR TEMPLATE_BINARIES_DIR OUT_DIR"""
import os, re, shutil, sys
dump, tmpl, out = sys.argv[1:4]
mods = {}                                   # guid -> (name, dir)
names = {}
for dp, dn, fn in os.walk(dump):
    if 'info.txt' not in fn:
        continue
    info = open(os.path.join(dp, 'info.txt'), errors='replace').read()
    g = re.search(r'File GUID: ([0-9A-Fa-f-]+)', info)
    m = re.match(r'^\d+ (.+)$', os.path.basename(dp))
    if g and m:
        mods[g.group(1).upper()] = (m.group(1), dp)
        names.setdefault(m.group(1).lower(), dp)

def section(dirpath, kind):
    """body of the first section of a kind (PE32 image, TE image, DXE dependency, Raw) below a file dir"""
    for dp, dn, fn in sorted(os.walk(dirpath)):
        base = os.path.basename(dp)
        if re.match(r'^\d+ %s section$' % kind, base) and 'body.bin' in fn:
            return open(os.path.join(dp, 'body.bin'), 'rb').read()
    return None

kinds = {'PE32': 'PE32 image', 'TE': 'TE image', 'DXE_DEPEX': 'DXE dependency', 'RAW': 'Raw'}
done, missing = 0, []
for dp, dn, fn in os.walk(tmpl):
    for f in fn:
        if not f.endswith('.inf'):
            continue
        inf = open(os.path.join(dp, f)).read()
        guid = re.search(r'FILE_GUID\s*=\s*([0-9A-Fa-f-]+)', inf).group(1).upper()
        base = re.search(r'BASE_NAME\s*=\s*(\S+)', inf).group(1)
        src = mods.get(guid, (None, None))[1] or names.get(base.lower())
        rel = os.path.relpath(dp, tmpl)
        if src is None:
            missing.append(os.path.join(rel, f))
            continue
        dst = os.path.join(out, rel)
        os.makedirs(dst, exist_ok=True)
        ok = True
        for kind, fname in re.findall(r'^\s*(PE32|TE|DXE_DEPEX|RAW)\|([^|\s]+)', inf, re.M):
            body = section(src, kinds[kind])
            if body is None and kind == 'RAW':
                body = section(src, 'Raw')
            if body is None:
                ok = False
                break
            open(os.path.join(dst, fname), 'wb').write(body)
        if ok:
            open(os.path.join(dst, f), 'w').write(inf)
            done += 1
        else:
            missing.append(os.path.join(rel, f) + ' (section missing)')
# raw files (freeform)
os.makedirs(os.path.join(out, 'RawFiles'), exist_ok=True)
for guid, (name, d) in mods.items():
    if re.search(r'\.(cfg|PROVISION|xml)$', name):
        body = section(d, 'Raw')
        if body is not None:
            open(os.path.join(out, 'RawFiles', name), 'wb').write(body)
print('modules', done, 'missing', len(missing))
for m in missing: print('  missing:', m)
