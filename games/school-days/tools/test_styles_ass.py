"""Validate the committed School Days subtitle palette without external deps.

Run: python -m unittest games/school-days/tools/test_styles_ass.py
The real asset is read-only. Synthetic engine tests live in C++ separately.
"""

from __future__ import annotations

import re
from pathlib import Path
import unittest


STYLES_PATH = Path(__file__).resolve().parents[1] / 'game-assets/styles.ass'
FIELDS = (
    'Name', 'Fontname', 'Fontsize', 'PrimaryColour', 'SecondaryColour',
    'OutlineColour', 'BackColour', 'Bold', 'Italic', 'Underline',
    'StrikeOut', 'ScaleX', 'ScaleY', 'Spacing', 'Angle', 'BorderStyle',
    'Outline', 'Shadow', 'Alignment', 'MarginL', 'MarginR', 'MarginV',
    'Encoding',
)

# Original 14 styles from SHA-256 a55f735609a0d41c...; only Setsuna changes.
ORIGINAL_COLORS = {
    'Default': ('&H00F8F5FF', '&H00201A2E'),
    'Hikari': ('&H00FFF3E0', '&H00803A20'),
    'Kotonoha': ('&H00FFF0F6', '&H00B03A64'),
    'Kumi': ('&H00EFF7FA', '&H003D5666'),
    'Makoto': ('&H00FFF4E8', '&H00A85A2A'),
    'Minami': ('&H00F2F0FA', '&H004A3E6E'),
    'Nanami': ('&H00F7EFF4', '&H007A4E66'),
    'Natsumi': ('&H00FFF2E4', '&H007A5230'),
    'Otome': ('&H00F0F2FF', '&H004A4E8C'),
    'Sekai': ('&H00EAF7F0', '&H00376B52'),
    'Setsuna': ('&H00FFF0E4', '&H00A85A2A'),
    'Taisuke': ('&H00EAF6FF', '&H002E6C9C'),
    'Kokoro': ('&H00EFFAF7', '&H003E7A70'),
    'Itaru': ('&H00F2EFFA', '&H006A5494'),
}
SETSUNA_NEW = ('&H00EEEEF9', '&H003F43BE')

# Authoritative literal request: order is intentional and non-normalized.
# The RGB outline string here is documentation only; the ASS value is tested.
SLOTS = (
    ('SekaiMother', ("Sekai's Mother", 'Youko', 'Mãe da Sekai'), '&H00F7F1F3', '&H00AB6077', '#7760AB'),
    ('KotonohaMother', ("Kotonoha's Mother",), '&H00F0ECF4', '&H005D2C8A', '#8A2C5D'),
    ('MakotoMother', ("Makoto's Mother",), '&H00E9F1F1', '&H000A6065', '#65600A'),
    ('KotonohaFather', ("Kotonoha's Father",), '&H00EBEDF4', '&H001E3886', '#86381E'),
    ('Kasannoin', ('Kasannoin',), '&H00EEECF5', '&H00402B90', '#902B40'),
    ('Teacher', ('Teacher',), '&H00EEF1F5', '&H003D5E8C', '#8C5E3D'),
    ('Roka', ('Roka',), '&H00F1F3EB', '&H00627B1F', '#1F7B62'),
    ('Ruka', ('Ruka',), '&H00F5F0F6', '&H0093539B', '#9B5393'),
    ('Natsuki', ('Natsuki',), '&H00F8F2EA', '&H00AE7216', '#1672AE'),
    ('Mugi', ('Mugi',), '&H00E9F0ED', '&H000C5936', '#36590C'),
    ('Ashikaga', ('Ashikaga',), '&H00E9EFF2', '&H000D4B69', '#694B0D'),
    ('Kazuha', ('Kazuha',), '&H00F6F3E8', '&H00987605', '#057698'),
    ('Futaba', ('Futaba',), '&H00EDF1E8', '&H00336801', '#016833'),
    ('ExtraGirl', ('Girl A', 'Girl B', 'Girl C', 'Girl D', 'Woman A', 'Woman B', 'Little Girl'), '&H00EEF0EE', '&H00455540', '#405545'),
    ('ExtraMan', ('Man A', 'Man B', 'Man C', 'Boy A', 'Niño'), '&H00F6F4F4', '&H00998186', '#868199'),
    ('Fairy', ('Fairy A', 'Fairy B', 'Ameria', 'Chris'), '&H00F4F4F2', '&H0083896E', '#6E8983'),
    ('System', ('Cellphone', 'Screen', 'School PA', 'Creditos'), '&H00F3F3F6', '&H00757D9C', '#9C7D75'),
    ('Role', ('Class Rep', 'Committee Rep', 'Periodista'), '&H00F0EFEF', '&H005B4E51', '#514E5B'),
    ('Pair', ('Sekai&Makoto', 'Makoto&Sekai', 'Makoto Makoto・Sekai Sekai'), '&H00F2F1E9', '&H00696406', '#066469'),
)

# Proven aliases were verified by same LxH ORS scene/timestamps across locales.
# Boy B is a generic ExtraMan label in T1; not a locale translation.
ADDITIONAL = (
    ('Credits', 'System'),
    ('Reporter', 'Role'),
    ('Child', 'ExtraMan'),
    ('Boy B', 'ExtraMan'),
)


def expected_new_styles():
    result = {}
    group_colors = {}
    for slot, names, fill, outline, rgb in SLOTS:
        assert slot not in group_colors
        assert re.fullmatch(r'&H[0-9A-F]{8}', fill)
        assert re.fullmatch(r'&H[0-9A-F]{8}', outline)
        # ASS color &HAABBGGRR -> #RRGGBB.
        assert '#' + outline[8:10] + outline[6:8] + outline[4:6] == rgb
        group_colors[slot] = (fill, outline)
        for name in names:
            assert name not in result and ',' not in name and '\n' not in name
            result[name] = (fill, outline)
    for name, slot in ADDITIONAL:
        assert name not in result and ',' not in name
        result[name] = group_colors[slot]
    return result


def style_fields(name, fill, outline):
    return [name, 'Noto Sans JP', '46', fill, '&H000000FF', outline,
            '&H96000000', '0', '0', '0', '0', '100', '100', '0.2',
            '0', '1', '2.6', '1.2', '2', '60', '60', '42', '1']


class TestSchoolDaysStyles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.content = STYLES_PATH.read_bytes()
        cls.text = cls.content.decode('utf-8', 'strict')
        cls.lines = cls.text.splitlines()
        cls.rows = []
        inside = False
        for line in cls.lines:
            if line == '[V4+ Styles]':
                inside = True
            elif line.startswith('['):
                inside = False
            if inside and line.startswith('Style: '):
                cls.rows.append(line[len('Style: '):].split(','))

    def test_utf8_no_bom_crlf_only(self):
        self.assertFalse(self.content.startswith(b'\xef\xbb\xbf'))
        self.assertNotIn(b'\n', self.content.replace(b'\r\n', b''))
        self.assertNotIn(b'\r', self.content.replace(b'\r\n', b''))
        self.assertTrue(self.content.endswith(b'\r\n'))

    def test_layout_and_uniqueness(self):
        self.assertIn('Format: ' + ', '.join(FIELDS), self.lines)
        self.assertTrue(all(len(row) == 23 for row in self.rows))
        names = [row[0] for row in self.rows]
        self.assertEqual(len(names), len(set(names)))
        self.assertNotIn('', names)
        self.assertEqual(len(names), len(ORIGINAL_COLORS) + len(expected_new_styles()))

    def test_setsuna_exactly_two_changes(self):
        rows = {r[0]: r for r in self.rows}
        self.assertEqual(rows['Setsuna'], style_fields('Setsuna', *SETSUNA_NEW))

    def test_other_13_original_styles_unchanged(self):
        rows = {r[0]: r for r in self.rows}
        for name, colors in ORIGINAL_COLORS.items():
            if name != 'Setsuna':
                with self.subTest(style=name):
                    self.assertEqual(rows[name], style_fields(name, *colors))

    def test_all_requested_and_proven_alias_colors(self):
        rows = {r[0]: r for r in self.rows}
        for name, colors in expected_new_styles().items():
            with self.subTest(speaker=name):
                self.assertEqual(rows[name], style_fields(name, *colors))
        self.assertEqual(set(rows), set(ORIGINAL_COLORS) | set(expected_new_styles()))

    def test_all_original_lines_preserve_sequence(self):
        names = [r[0] for r in self.rows]
        self.assertEqual(names[:len(ORIGINAL_COLORS)], list(ORIGINAL_COLORS))
        self.assertEqual(names[len(ORIGINAL_COLORS):], list(expected_new_styles()))

    def test_three_approved_outlines_preserve_measured_contrast(self):
        # The upstream >=6.0:1 darkening proposal conflicts with the user's
        # explicitly approved literal ASS colours. Assert the actual contrast
        # of that stable palette, rather than quietly changing any colour.
        def luminance(color):
            r, g, b = (int(color[i:i + 2], 16) / 255 for i in (8, 6, 4))

            def linear(x):
                return x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4

            return .2126 * linear(r) + .7152 * linear(g) + .0722 * linear(b)

        groups = {slot: (fill, outline) for slot, _, fill, outline, _ in SLOTS}
        approved = {'ExtraMan': 3.4055051, 'Fairy': 3.4157023,
                    'System': 3.3963868}
        for slot, expected in approved.items():
            a, b = sorted(map(luminance, groups[slot]))
            with self.subTest(slot=slot):
                self.assertAlmostEqual((b + .05) / (a + .05), expected, places=5)

    def test_all_locale_aliases_preserved(self):
        names = {row[0] for row in self.rows}
        for name in ('Mãe da Sekai', 'Niño', 'Credits', 'Reporter', 'Roka',
                     'Ruka', 'Natsuki', 'Mugi', 'Ameria', 'Chris', 'Boy B'):
            self.assertIn(name, names)
        expected = expected_new_styles()
        self.assertEqual(expected['Ameria'], expected['Chris'])
        self.assertEqual(expected['Ameria'], expected['Fairy A'])


if __name__ == '__main__':
    unittest.main()
