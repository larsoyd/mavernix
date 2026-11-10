import os
import sys
import tempfile
import unittest
from pathlib import Path

EMOJI_DIR = Path(os.environ["EMOJI_SOURCE_DIR"])
sys.path.insert(0, str(EMOJI_DIR))
import make_emoji_list as maker

TEST_DATA = """\
# group: Smileys & Emotion
# subgroup: face-smiling
1F600                                                  ; fully-qualified     # 😀 E1.0 grinning face
263A FE0F                                              ; fully-qualified     # ☺️ E0.6 smiling face
263A                                                   ; unqualified         # ☺ E0.6 smiling face

# group: People & Body
1F44B                                                  ; fully-qualified     # 👋 E0.6 waving hand
1F44B 1F3FB                                            ; fully-qualified     # 👋🏻 E1.0 waving hand: light skin tone
1F44B 1F3FF                                            ; fully-qualified     # 👋🏿 E1.0 waving hand: dark skin tone
261D FE0F                                              ; fully-qualified     # ☝️ E0.6 index pointing up
261D 1F3FD                                             ; fully-qualified     # ☝🏽 E1.0 index pointing up: medium skin tone
1F9D1 200D 1F91D 200D 1F9D1                            ; fully-qualified     # 🧑‍🤝‍🧑 E12.0 people holding hands
1F9D1 1F3FB 200D 1F91D 200D 1F9D1 1F3FB                ; fully-qualified     # 🧑🏻‍🤝‍🧑🏻 E12.0 people holding hands: light skin tone
1F9D1 1F3FB 200D 1F91D 200D 1F9D1 1F3FC                ; fully-qualified     # 🧑🏻‍🤝‍🧑🏼 E12.1 people holding hands: light skin tone, medium-light skin tone

# group: Component
1F3FB                                                  ; component           # 🏻 E1.0 light skin tone

# group: Flags
1F1F3 1F1F4                                            ; fully-qualified     # 🇳🇴 E2.0 flag: Norway
"""

ANNOTATIONS = """\
<ldml><annotations>
<annotation cp="😀">face | grin | grinning face | happy</annotation>
<annotation cp="😀" type="tts">grinning face</annotation>
<annotation cp="☺">face | outlined | relaxed | smile</annotation>
<annotation cp="👋">hand | hello | wave | waving</annotation>
</annotations></ldml>
"""

DERIVED = """\
<ldml><annotations>
<annotation cp="👋🏿">dark skin tone | hand | wave</annotation>
<annotation cp="🇳🇴">flag</annotation>
<annotation cp="🇳🇴" type="tts">flag: Norway</annotation>
</annotations></ldml>
"""


class EmojiListTests(unittest.TestCase):
    def build(self):
        with tempfile.TemporaryDirectory() as folder:
            paths = []
            for name, text in (("test.txt", TEST_DATA), ("a.xml", ANNOTATIONS), ("d.xml", DERIVED)):
                path = Path(folder) / name
                path.write_text(text, encoding="utf-8")
                paths.append(path)
            return maker.build(*paths)

    def test_groups_keep_unicode_order_without_components(self):
        groups = self.build()
        self.assertEqual([group for group, _ in groups], ["smileys", "people", "flags"])

    def test_only_fully_qualified_entries_are_listed(self):
        smileys = dict(self.build())["smileys"]
        self.assertEqual([entry.text for entry in smileys], ["😀", "☺️"])

    def test_skin_tones_fold_into_their_base(self):
        people = dict(self.build())["people"]
        self.assertEqual([entry.name for entry in people],
                         ["waving hand", "index pointing up", "people holding hands"])
        self.assertEqual(people[0].tones, ["👋🏻", "👋🏿"])
        self.assertEqual(people[1].tones, ["☝🏽"])

    def test_mixed_tones_are_left_out(self):
        holding = dict(self.build())["people"][2]
        self.assertEqual(len(holding.tones), 1)
        self.assertIn("\U0001F3FB", holding.tones[0])
        self.assertNotIn("\U0001F3FC", holding.tones[0])

    def test_keywords_come_from_annotations_without_the_name(self):
        smileys = dict(self.build())["smileys"]
        self.assertEqual(smileys[0].words, ["grin", "happy"])
        self.assertEqual(smileys[1].words, ["outlined", "relaxed", "smile"])

    def test_flag_name_drops_its_prefix(self):
        flag = dict(self.build())["flags"][0]
        self.assertEqual(flag.name, "Norway")
        self.assertEqual(flag.words, ["flag"])

    def test_written_lines_read_back(self):
        text = maker.written(self.build())
        lines = [line for line in text.splitlines() if not line.startswith("# ")]
        self.assertEqual(lines[0], "@smileys")
        self.assertEqual(lines[1], "😀\tgrinning face\tgrin happy\t")
        self.assertIn("👋\twaving hand\thello wave\t👋🏻 👋🏿", lines)


class ShippedListTests(unittest.TestCase):
    def setUp(self):
        self.lines = (EMOJI_DIR / "emoji.tsv").read_text(encoding="utf-8").splitlines()

    def test_every_line_is_a_note_a_group_or_four_fields(self):
        for number, line in enumerate(self.lines, 1):
            if line.startswith("# ") or line.startswith("@"):
                continue
            self.assertEqual(line.count("\t"), 3, f"line {number}: {line!r}")

    def test_groups_are_the_nine_the_shell_names(self):
        groups = [line[1:] for line in self.lines if line.startswith("@")]
        self.assertEqual(groups, ["smileys", "people", "nature", "food", "travel", "activities",
                                  "objects", "symbols", "flags"])

    def test_no_emoji_is_listed_twice(self):
        seen = [line.split("\t")[0] for line in self.lines if "\t" in line]
        self.assertIn("#\ufe0f\u20e3", seen)
        self.assertGreater(len(seen), 1500)
        self.assertEqual(len(seen), len(set(seen)))

    def test_license_notice_is_kept(self):
        self.assertTrue(any("Unicode" in line and "©" in line for line in self.lines[:12]))


if __name__ == "__main__":
    unittest.main()
