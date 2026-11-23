#!/usr/bin/env python3
"""Fetch the Vectrex games' screen sheets into a folder for the image (#224).

The `Game Overlay` folder of libretro/overlay-borders (`1080 GCE Vectrex`) at
one commit: 38 PNG files, unmodified, each checked against its sha256 below.
Pinned the way tools/fetch-shaders.py pins the shaders to libretro/glsl-shaders,
but not committed to this repository (somebody else's drawings, 3.9 MB): the
image build fetches them with this, and the console itself fetches nothing. docs/PROJECT.md open question 51 is the decision; which
sheet belongs to which game is this project's own table,
frontend/data/vectrex-overlays.json.

    tools/fetch-vectrex-overlays.py <dir>

Leaves <dir> holding exactly the 38 sheets: a file already there with the
right sha256 is kept (build.yml keeps the folder in its Actions cache), any
other file is removed, and a sheet that cannot be fetched or does not match
fails the run. build.yml runs it into image_payload/vectrex;
build_files/install-frontend.sh installs that in /usr/share/cabinetos/vectrex.
Plain HTTPS from GitHub, no `gh`, so it runs on a runner and on a console.
"""

import hashlib
import os
import sys
import time
import urllib.parse
import urllib.request

COMMIT = "385490fa89f3d54e0d249cd1aca39e4e4fd37caf"
FOLDER = "1080 GCE Vectrex/Game Overlay"

SHEETS = [
    ("2D_Narrow_Escape_Hack", "10293c0f3a27cf7c60f23f68c071323bc6a182bf52df04e509896fe45445c78a"),
    ("3D_Crazy_Coaster", "f4cc02ed053f852124b88f0bb095ade2df889360f8ac014ca12633e510fb5745"),
    ("3D_Mine_Storm", "eadc0887351386454e0cc7040e2d4b7160d9f4447029785d1af5e93286bc7296"),
    ("3D_Narrow_Escape", "0e100595b6f7da0c5f7ffd713b1e750303472cf51631f01fd47ee1f4cf6d8dee"),
    ("3D_Pole_Position", "a776280967043b9624c1772fd79186291320bd9c1f2f1a6dda8fe9c7bb3aa783"),
    ("AnimAction", "d41eed94b5dc9d184617642a44a965e04ab639fb25486222e026b30120b332c7"),
    ("ArmorAttack", "a6bc865cd773e78087d5d96d9e9b08d91a9900c5848d45dc47500247b24643bc"),
    ("Art_Master", "6c3f67760cb11aed97622cff249b135d08a34071c375af347e7fa29d5e235c92"),
    ("Bedlam", "93a5c8e0a0d6c816890a16113ee3dcdc4d9fcaf0e6574066fb05b7069a142e60"),
    ("Berzerk", "8d391b710a43670fc6cfef98febc5f492e32ca3e294efc3092aba3c2167b1f01"),
    ("Blitz!_Action_Football", "ce5de0ee11eeb1c1cb97355da6ec30f175cbbac25197ff39d20b34d777e3c8f9"),
    ("Clean_Sweep", "241aa3d0a671c1443f73b237768b41f543373922be9dacef300b0d2925b9db93"),
    ("Cosmic_Chasm", "686de37f997c44f1b06ebf05f9fe3f9673b7ccb34a7a8d5198c34e4f977e7f9c"),
    ("Dark_Tower_Proto", "4ca6cda46e72fb6b13708946b801c76a5020ba48b62c6d58ced1778fdd6197ad"),
    ("Flipper_Pinball", "6b625e8db8c3f2cabcde7168602db0fc62c5cfd8fed7525fe4ea5baad4eab363"),
    ("Fortress_of_Narzod", "76756b9fd48d99e76d7dfde949f01d1a5223d49ec394b3c59c63d8eb12f2e52e"),
    ("Heads-Up_Action_Soccer", "e20c2e0a42e5749c13a78d956ef5a674fa0be3e6ed564e4b93d5b2eb712f8aaf"),
    ("HyperChase_Auto_Race", "1ac0969fd2935adf19adf335b4bd853b533316ed5cc9ad00eb3a61140cc174a9"),
    ("Mail_Plane", "927254d895ac5db5908fed297959186f9f328165aa999496c90e34186706b9dc"),
    ("Melody_Master", "c7866f8ee6ee5b127b3ca4e40f931ad161baaaa9f4cc7024683eb4384846daad"),
    ("Mine_Storm", "60389c2b68db03e9cf75263711c64d1c11bfbddd2fc710fbca2f5ef82abc81bb"),
    ("Mine_Storm_II", "60389c2b68db03e9cf75263711c64d1c11bfbddd2fc710fbca2f5ef82abc81bb"),
    ("Mr_Boston_Clean_Sweep", "241aa3d0a671c1443f73b237768b41f543373922be9dacef300b0d2925b9db93"),
    ("Pitchers_Duel_Proto", "1a9639a61bd0df26a130ec4e5dfbb0c752b32bd224b88f2e36ede2c12e69a489"),
    ("Polar_Rescue", "2afea40d0b6136be916b78a2a935b2837d5270f5510f58463f58ab78fb267cfa"),
    ("Pole_Position", "d5ae43d29c12468f2d0bebc224cde9b68913675ac48da315b5c2bacdced163a2"),
    ("Rip_Off", "3213fc12a6ca88a25fa521736a739e8ebae7669bf258716d429520163b2e2abf"),
    ("Scramble", "4479eddffffc2b46b5452b9164a90b482df748561012c4b32718f114f7f95cc7"),
    ("Solar_Quest", "61b634729b63316eaa8edc7a27759a6b3287e4f7561c969fff6a1c18bdbbf608"),
    ("Space_Wars", "38b18529a53ab6b365adf8d8d4988c60f1ad5caf9622be60dd856fb8c11a3432"),
    ("Spike", "302d9b63fc49492c007c063868f64c89e0904bf38b25046240468b65c5e0837d"),
    ("Spinball", "251798f1acfa5a4f31ea0bf386e6e20a4c38206e67be9b9aaab85addadc93c0f"),
    ("Star_Castle", "88d3334fd4b153c3f08c9ffdb91f0be9337e282b023fd4cc0a916e0ed751ccf8"),
    ("Star_Hawk", "6001742a6efd1aa1c5a79a34bce862b190807f151db806931489c4441cf01a9c"),
    ("Star_Ship", "1b06881c5b2756b0457d4ab87f685dc3b3c0e8ac0d47dc61a04d2ef9eee06530"),
    ("Star_Trek-The_Motion_Picture", "13721cd451b60a95fc385398e618b3cf6cd8b1fefea7c255a9bf5393865b6844"),
    ("Tour_De_France_Proto", "73d6f269aa7a9bf38c017df668d808bfcc0ce05d3e9397e58c7b29741f33cc5f"),
    ("Web_Wars", "f59f34390f3cdf5ee5598d8f30ed4cd421f892b55fb21af975e64951854c8614"),
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def download(name):
    path = urllib.parse.quote(f"{FOLDER}/{name}.png")
    url = f"https://raw.githubusercontent.com/libretro/overlay-borders/{COMMIT}/{path}"
    for attempt in (1, 2, 3):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                return r.read()
        except OSError as e:
            print(f"{name}: download attempt {attempt} failed: {e}", file=sys.stderr)
            time.sleep(5)
    sys.exit(f"{name}: could not be downloaded")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/fetch-vectrex-overlays.py <dir>")
    dest = sys.argv[1]
    os.makedirs(dest, exist_ok=True)
    wanted = {f"{name}.png" for name, _ in SHEETS}
    for f in os.listdir(dest):
        p = os.path.join(dest, f)
        if f not in wanted and os.path.isfile(p):
            os.remove(p)
            print("removed", f)
    fetched = 0
    for name, want in SHEETS:
        out = os.path.join(dest, f"{name}.png")
        if os.path.isfile(out):
            with open(out, "rb") as fh:
                if sha256(fh.read()) == want:
                    continue
        data = download(name)
        got = sha256(data)
        if got != want:
            sys.exit(f"{name}.png has sha256 {got}, expected {want}")
        with open(out + ".part", "wb") as fh:
            fh.write(data)
        os.replace(out + ".part", out)
        fetched += 1
    print(f"{len(SHEETS)} Vectrex sheets in {dest} ({fetched} fetched, "
          f"{len(SHEETS) - fetched} already there) at {COMMIT[:9]}")


main()
