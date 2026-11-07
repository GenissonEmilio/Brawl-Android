"""Ensure the Android validator embeds the public Brawl corpus; no runtime asset is required."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BrawlModuleMetadataTests(unittest.TestCase):
    def test_embedded_android_metadata_matches_public_layout(self):
        reference = json.loads((ROOT / 'wiicompiled/projects/brawl/module_layout.json').read_text())
        java = (ROOT / 'wiicompiled/android/app/src/main/java/com/wither/nsmbw/BrawlInstallValidator.java').read_text()
        match = re.search(r'private static final String MODULES = "([^"]+)";', java)
        self.assertIsNotNone(match, 'Android metadata must be embedded in the compiled class')
        rows = [row.split(':') for row in match[1].split(' ')]
        embedded = {name: {'id': int(identity), 'fix_size': int(fixed, 16),
                           'bss_size': int(bss, 16), 'crc32': int(crc, 16)}
                    for name, identity, fixed, bss, crc in rows}
        expected = {name: {'id': values['id'], **{key: int(values[key], 16)
                     for key in ('fix_size', 'bss_size', 'crc32')}} for name, values in reference.items()}
        self.assertEqual(len(rows), 126)
        self.assertEqual(len(embedded), len(rows), 'duplicate module constants')
        self.assertEqual(embedded, expected)


if __name__ == '__main__':
    unittest.main()
