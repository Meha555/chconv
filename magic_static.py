MAGIC_PATHS = [
    "misc/magic.mgc",
]

for path in MAGIC_PATHS:
    try:
        with open(path, "rb") as f:
            data = f.read()
        break
    except:
        print(f"Failed to open magic database: {path}")
        continue

with open("magic_static.h", "w") as f:
    f.write("static const unsigned char g_magic_database_buffer[%d] = {%s};" % (len(data), ",".join(str(int(b)) for b in data)))