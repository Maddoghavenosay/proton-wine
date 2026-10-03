#!/usr/bin/env python3
"""Carry the media registrations of one prefix pack into another.

The x86_64 prefix pack this layer ships (GameNative/bionic-prefix-files, 2025-07-02) was
generated before winegstreamer, the MP4/ASF byte-stream handlers and the audio/video MFTs
registered themselves, so a prefix made from it has no "Windows Media Foundation"
ByteStreamHandlers, none of winegstreamer's classes and no MFT entries: Media Foundation
finds no handler for a .wmv/.mp4 and a game's cutscenes are skipped (Ninja Gaiden Sigma).
The arm64ec pack (2025-07-28) has them, and the registry is architecture-neutral, so this
copies every media-related section the target lacks from the donor pack's system.reg into
the target's. Nothing else is touched (no device, PnP, Steam or app keys), and the pack is
written back in the same layout (`.wine/` at the root, same file modes).

    prefixpack-media-registry.py TARGET.txz DONOR.txz OUT.txz
"""
import collections
import io
import lzma
import re
import sys
import tarfile

MEDIA_DLLS = {
    "winegstreamer.dll", "mfsrcsnk.dll", "mfmp4srcsnk.dll", "mfasfsrcsnk.dll",
    "msauddecmft.dll", "mfh264enc.dll", "msvdsp.dll", "vidreszr.dll", "windowscodecs.dll",
    "mf.dll", "mfplat.dll", "mfreadwrite.dll", "mfmediaengine.dll", "wmvcore.dll",
    "winedmo.dll", "mp3dmod.dll", "wmadmod.dll", "wmvdecod.dll", "quartz.dll", "devenum.dll",
}


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


def media_sections(donor, target):
    target_lc = {k.lower() for k in target}
    clsid_dll = {}
    for k in donor:
        m = re.match(r"Software\\\\Classes\\\\(?:Wow6432Node\\\\)?CLSID\\\\(\{[^}]*\})\\\\InprocServer32$", k)
        if m:
            clsid_dll[m.group(1).lower()] = inproc_dll(donor, k)

    def wanted(k):
        kl = k.lower()
        if not k or kl in target_lc:
            return False
        if "windows media foundation" in kl:
            return True
        if re.match(r"software\\\\classes\\\\(wow6432node\\\\)?(mediafoundation|directshow)\\\\", kl):
            return True
        m = re.search(r"clsid\\\\(\{[^}]*\})", kl)
        if m:
            if "\\\\instance\\\\" in kl:   # component-category membership (DirectShow filters, WIC codecs)
                return True
            return clsid_dll.get(m.group(1)) in MEDIA_DLLS
        return False

    return [k for k in donor if wanted(k)], clsid_dll


def read_system_reg(path):
    with tarfile.open(path, "r:xz") as tf:
        for m in tf.getmembers():
            if m.name.endswith("system.reg"):
                return m.name, tf.extractfile(m).read().decode("utf-8", "replace")
    raise SystemExit(f"{path}: no system.reg")


def main(target_path, donor_path, out_path):
    reg_name, target_text = read_system_reg(target_path)
    _, donor_text = read_system_reg(donor_path)
    target, donor = parse(target_text), parse(donor_text)
    add, clsid_dll = media_sections(donor, target)
    if not add:
        print("nothing to add")
    extra = "".join("".join(donor[k]) + ("" if donor[k][-1].endswith("\n\n") or donor[k][-1] == "\n" else "\n") for k in add)
    new_text = target_text.rstrip("\n") + "\n\n" + extra

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

    by = collections.Counter()
    for k in add:
        m = re.search(r"clsid\\\\(\{[^}]*\})", k.lower())
        if m and "\\\\instance\\\\" in k.lower():
            by["category membership"] += 1
        elif m:
            by[clsid_dll.get(m.group(1)) or "class"] += 1
        else:
            by["\\".join(k.split("\\\\")[:3])] += 1
    print(f"added {len(add)} sections to {reg_name}:")
    for name, n in by.most_common():
        print(f"  {n:4} {name}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
