import uuid

u = uuid.uuid4()

print(
    "SLANG_COM_INTERFACE(0x{}, 0x{}, 0x{}, {{ 0x{}, 0x{}, 0x{}, 0x{}, 0x{}, 0x{}, 0x{}, 0x{} }});".format(
        u.hex[0:8],
        u.hex[8:12],
        u.hex[12:16],
        u.hex[16:18],
        u.hex[18:20],
        u.hex[20:22],
        u.hex[22:24],
        u.hex[24:26],
        u.hex[26:28],
        u.hex[28:30],
        u.hex[30:32],
    )
)
