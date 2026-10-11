"""Read x64 PE import-time DLL policy without loading executable code."""
import struct


def dependent_load_flags(executable):
    data = executable.read_bytes()
    def word(offset):
        return struct.unpack_from("<H", data, offset)[0]
    def dword(offset):
        return struct.unpack_from("<I", data, offset)[0]
    if data[:2] != b"MZ":
        raise ValueError("Missing DOS header")
    pe = dword(0x3c)
    optional = pe + 24
    if data[pe:pe + 4] != b"PE\0\0" or word(optional) != 0x20b:
        raise ValueError("Expected an x64 PE image")
    if dword(optional + 108) <= 10:
        raise ValueError("Missing load configuration directory")
    rva, size = struct.unpack_from("<II", data, optional + 112 + 10 * 8)
    if not rva or size < 80:
        raise ValueError("Missing dependent DLL load flags")
    sections = optional + word(pe + 20)
    for index in range(word(pe + 6)):
        section = sections + index * 40
        address, raw_size, raw_offset = struct.unpack_from("<III", data, section + 12)
        delta = rva - address
        if 0 <= delta and delta + 80 <= raw_size:
            offset = raw_offset + delta
            if dword(offset) < 80:
                raise ValueError("Load configuration is too short")
            return word(offset + 78)
    raise ValueError("Load configuration not backed by a file section")
