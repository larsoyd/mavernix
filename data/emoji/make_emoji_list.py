#!/usr/bin/env python3
"""Writes emoji.tsv from Unicode's emoji test list and CLDR's English annotations.

    make_emoji_list.py EMOJI_TEST ANNOTATIONS DERIVED_ANNOTATIONS OUT

Run it again when Unicode adds emoji. Each line of the result is an emoji,
its name, its search words and its skin tones, under a line naming its group.
"""

import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field

GROUPS = {
    "Smileys & Emotion": "smileys",
    "People & Body": "people",
    "Animals & Nature": "nature",
    "Food & Drink": "food",
    "Travel & Places": "travel",
    "Activities": "activities",
    "Objects": "objects",
    "Symbols": "symbols",
    "Flags": "flags",
}
TONES = {chr(point) for point in range(0x1F3FB, 0x1F400)}
PRESENTATION = "️"
ENTRY = re.compile(r"^([0-9A-F ]+?)\s*; fully-qualified\s*# \S+ E[\d.]+ (.+)$")

NOTICE = """\
# Emoji with their names, groups and search words, written by make_emoji_list.py
# from the Unicode emoji test list and the CLDR English annotations.
# © 2025 Unicode, Inc. Used under the Unicode License v3,
# https://www.unicode.org/license.txt
# emoji, name, search words and skin tones, parted by tabs
"""


@dataclass
class Entry:
    text: str
    name: str
    words: list = field(default_factory=list)
    tones: list = field(default_factory=list)


def bare(text):
    return "".join(letter for letter in text if letter not in TONES and letter != PRESENTATION)


def annotations(*paths):
    """Keywords by emoji, without presentation selectors."""
    found = {}
    for path in paths:
        for node in ElementTree.parse(path).iter("annotation"):
            toned = any(letter in TONES for letter in node.get("cp"))
            if node.get("type") != "tts" and not toned:
                found[bare(node.get("cp"))] = [word.strip() for word in node.text.split("|")]
    return found


def build(emoji_test, *annotation_files):
    keywords = annotations(*annotation_files)
    groups = []
    bases = {}
    listed = None
    with open(emoji_test, encoding="utf-8") as lines:
        for line in lines:
            if line.startswith("# group:"):
                group = GROUPS.get(line.split(":", 1)[1].strip())
                listed = None
                if group is not None:
                    listed = []
                    groups.append((group, listed))
                continue
            match = ENTRY.match(line)
            if listed is None or match is None:
                continue
            text = "".join(chr(int(point, 16)) for point in match.group(1).split())
            name = match.group(2)
            tones = [letter for letter in text if letter in TONES]
            if tones:
                # one tone for everyone in it, the mixed ones are too many to offer
                if len(set(tones)) == 1 and bare(text) in bases:
                    bases[bare(text)].tones.append(text)
                continue
            name = name.removeprefix("flag: ")
            spoken = set(re.split(r"[\s:,-]+", name.lower()))
            words = []
            for phrase in keywords.get(bare(text), []):
                for word in phrase.lower().split():
                    if word not in spoken and word not in words:
                        words.append(word)
            entry = Entry(text, name, words)
            bases[bare(text)] = entry
            listed.append(entry)
    return groups


def written(groups):
    lines = [NOTICE.rstrip("\n")]
    for group, entries in groups:
        lines.append("@" + group)
        for entry in entries:
            lines.append("\t".join([entry.text, entry.name, " ".join(entry.words), " ".join(entry.tones)]))
    return "\n".join(lines) + "\n"


def main(arguments):
    if len(arguments) != 4:
        sys.exit(__doc__)
    groups = build(*arguments[:3])
    with open(arguments[3], "w", encoding="utf-8") as out:
        out.write(written(groups))
    for group, entries in groups:
        print(f"[emoji] group={group} count={len(entries)} with_tones={sum(1 for entry in entries if entry.tones)}")


if __name__ == "__main__":
    main(sys.argv[1:])
