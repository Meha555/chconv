MAGIC_PATHS = [
    "misc/magic.mgc",
]
data = None
for path in MAGIC_PATHS:
    try:
        with open(path, "rb") as f:
            data = f.read()
        break
    except:
        print(f"Failed to open magic database: {path}")
        continue

if data is not None:
    with open("magic_static.h", "w") as f:
        f.write("#pragma once\n")
        # f.write(f"static const unsigned char g_magic_database_buffer[{len(data)}] = {{{",".join(f"{b}" for b in data)}}};")
        f.write(f"static const unsigned char g_magic_database_buffer[{len(data)}] = {{{",".join(f"0x{b:02x}" for b in data)}}};")
else:
    print("Failed to read magic database from all paths")