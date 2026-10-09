"""Regression for the PE field that constrains imports before wmain runs."""
from pathlib import Path
import struct
import tempfile
import unittest
from pe_policy import dependent_load_flags


class PePolicyTests(unittest.TestCase):
    def test_read_real_load_config_field_and_reject_missing_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "test.exe"
            data = bytearray(1024)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3c, 0x80)
            data[0x80:0x84] = b"PE\0\0"
            struct.pack_into("<H", data, 0x86, 1)
            struct.pack_into("<H", data, 0x94, 240)
            optional = 0x98
            struct.pack_into("<H", data, optional, 0x20b)
            struct.pack_into("<I", data, optional + 108, 16)
            struct.pack_into("<II", data, optional + 112 + 80, 0x1000, 80)
            struct.pack_into("<III", data, optional + 240 + 12, 0x1000, 512, 512)
            struct.pack_into("<I", data, 512, 80)
            for flags in [0, 0x800, 0xA00]:
                struct.pack_into("<H", data, 512 + 78, flags)
                image.write_bytes(data)
                self.assertEqual(dependent_load_flags(image), flags)
            struct.pack_into("<II", data, optional + 112 + 80, 0, 0)
            image.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "Missing dependent"):
                dependent_load_flags(image)


if __name__ == "__main__":
    unittest.main()
