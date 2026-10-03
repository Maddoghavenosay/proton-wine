#!/usr/bin/env python3
"""Bring one prefix pack's media registrations in line with another's.

The x86_64 prefix pack this layer ships (GameNative/bionic-prefix-files, 2025-07-02) was
generated with an older Wine: it has no "Windows Media Foundation" ByteStreamHandlers, none
of winegstreamer's classes and no MFT entries, and it still points the Microsoft codec
classes (WMV/WMA/AAC/H.264 decoders, colour converter, MP4 handlers, DirectShow filters) at
winegstreamer.dll, where today's Wine serves them from wmvdecod.dll, wmadmod.dll,
msauddecmft.dll, msmpeg2vdec.dll, colorcnv.dll, mfmp4srcsnk.dll and quartz.dll. A prefix
made from it cannot open a .wmv/.mp4 (no handler), and once it can, winegstreamer's
DllGetClassObject refuses the Microsoft class ids (CLASS_E_CLASSNOTAVAILABLE), so a game's
cutscenes are skipped (Ninja Gaiden Sigma). The arm64ec pack (2025-07-28) is right, and the
registry is architecture-neutral, so this:

  - adds every media-related section the target lacks (handlers, winegstreamer and MFT
    classes, DirectShow/MediaFoundation category entries), and
  - replaces the class registration (all its subkeys, both hives) of every media class whose
    InprocServer32 differs between the two packs,

taking them from the donor pack's system.reg. Nothing else is touched (no device, PnP, Steam
or app keys), and the pack is written back in the same layout (`.wine/` at the root, same
file modes).

    prefixpack-media-registry.py TARGET.txz DONOR.txz OUT.txz
    prefixpack-media-registry.py --reg TARGET-system.reg DONOR-system.reg OUT-system.reg
"""
import collections
import io
import lzma
import re
import sys
import tarfile

MEDIA_DLLS = {
    "winegstreamer.dll", "mfsrcsnk.dll", "mfmp4srcsnk.dll", "mfasfsrcsnk.dll",
    "msauddecmft.dll", "mfh264enc.dll", "mfh264dec.dll", "msmpeg2vdec.dll", "msmpeg2adec.dll",
    "msvdsp.dll", "vidreszr.dll", "colorcnv.dll", "wmvdecod.dll", "wmadmod.dll", "mp3dmod.dll",
    "windowscodecs.dll", "wmphoto.dll", "mf.dll", "mfplat.dll", "mfreadwrite.dll",
    "mfmediaengine.dll", "wmvcore.dll", "winedmo.dll", "quartz.dll", "devenum.dll",
}

CLSID_RE = re.compile(r"software\\\\classes\\\\(?:wow6432node\\\\)?clsid\\\\(\{[^}]*\})", re.I)


def parse(text):
    """Registry text -> ordered {section name: [lines]} (the header block kept under '')."""
    secs = collections.OrderedDict()
    cur = ""
    secs[cur] = []
    for line in text.splitlines(keepends=True):
        if line.startswith("["):
            cur = line.split("]")[0][1:]
            secs[cur] = []
        secs[cur].append(line)
    return secs


def inproc_dll(secs, section):
    for line in secs.get(section, []):
        if line.startswith("@="):
            return line.split("\\\\")[-1].strip().strip('"').lower()
    return None


def clsid_dlls(secs):
    """{clsid (lower): dll} from every (Wow6432Node\\)CLSID\\{..}\\InprocServer32 section."""
    out = {}
    for k in secs:
        m = re.match(r"Software\\\\Classes\\\\(?:Wow6432Node\\\\)?CLSID\\\\(\{[^}]*\})\\\\InprocServer32$", k)
        if m:
            out[m.group(1).lower()] = inproc_dll(secs, k)
    return out


def merge(target_text, donor_text):
    target, donor = parse(target_text), parse(donor_text)
    target_lc = {k.lower() for k in target}
    donor_dll, target_dll = clsid_dlls(donor), clsid_dlls(target)

    # Media classes the target registers to a different DLL than the donor: replaced whole.
    stale = {g for g, d in donor_dll.items()
             if d in MEDIA_DLLS and g in target_dll and target_dll[g] != d}

    def clsid_of(k):
        m = CLSID_RE.search(k)
        return m.group(1).lower() if m else None

    def wanted(k):
        kl = k.lower()
        if not k:
            return False
        g = clsid_of(k)
        if g in stale:
            return True
        if kl in target_lc:
            return False
        if "windows media foundation" in kl:
            return True
        if re.match(r"software\\\\classes\\\\(wow6432node\\\\)?(mediafoundation|directshow)\\\\", kl):
            return True
        if g:
            if "\\\\instance\\\\" in kl:   # component-category membership (DirectShow filters, WIC codecs)
                return True
            return donor_dll.get(g) in MEDIA_DLLS
        return False

    add = [k for k in donor if wanted(k)]
    drop = {k for k in target if clsid_of(k) in stale}

    out = []
    for k, lines in target.items():
        if k in drop:
            continue
        out.extend(lines)
    text = "".join(out).rstrip("\n") + "\n\n"
    for k in add:
        body = "".join(donor[k])
        text += body if body.endswith("\n\n") else body.rstrip("\n") + "\n\n"

    by = collections.Counter()
    for k in add:
        g = clsid_of(k)
        if g in stale:
            by[f"replaced -> {donor_dll[g]}"] += 1
        elif g and "\\\\instance\\\\" in k.lower():
            by["category membership"] += 1
        elif g:
            by[donor_dll.get(g) or "class"] += 1
        else:
            by["\\".join(k.split("\\\\")[:3])] += 1
    return text, add, drop, by


def read_system_reg(path):
    with tarfile.open(path, "r:xz") as tf:
        for m in tf.getmembers():
            if m.name.endswith("system.reg"):
                return m.name, tf.extractfile(m).read().decode("utf-8", "replace")
    raise SystemExit(f"{path}: no system.reg")


def report(add, drop, by, where):
    print(f"{where}: dropped {len(drop)} stale sections, added {len(add)} sections:")
    for name, n in by.most_common():
        print(f"  {n:4} {name}")


def main_pack(target_path, donor_path, out_path):
    reg_name, target_text = read_system_reg(target_path)
    _, donor_text = read_system_reg(donor_path)
    new_text, add, drop, by = merge(target_text, donor_text)

    data = io.BytesIO()
    with tarfile.open(target_path, "r:xz") as tin, tarfile.open(fileobj=data, mode="w") as tout:
        for m in tin.getmembers():
            if m.name == reg_name:
                body = new_text.encode("utf-8")
                m.size = len(body)
                tout.addfile(m, io.BytesIO(body))
            elif m.isfile():
                tout.addfile(m, tin.extractfile(m))
            else:
                tout.addfile(m)
    with lzma.open(out_path, "wb", preset=9) as f:
        f.write(data.getvalue())
    report(add, drop, by, reg_name)


def main_reg(target_path, donor_path, out_path):
    new_text, add, drop, by = merge(open(target_path, encoding="utf-8", errors="replace").read(),
                                    open(donor_path, encoding="utf-8", errors="replace").read())
    open(out_path, "w", encoding="utf-8").write(new_text)
    report(add, drop, by, out_path)


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--reg":
        main_reg(*sys.argv[2:])
    elif len(sys.argv) == 4:
        main_pack(*sys.argv[1:])
    else:
        raise SystemExit(__doc__)
