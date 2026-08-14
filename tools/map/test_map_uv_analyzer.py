#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from map_uv_analyzer import parse_map


MAP_FACE = (
    "( 0 0 0 ) ( 0 1 0 ) ( 1 1 0 ) stone "
    "[ 1 0 0 0 ] [ 0 1 0 0 ] 0 1 1"
)


class ParseMapTest(unittest.TestCase):
    def parse(self, contents: str):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.map"
            path.write_text(contents, encoding="utf-8")
            return parse_map(path, 0.002)

    def test_parses_map_without_optional_comments(self) -> None:
        entities = self.parse(
            "\n".join(
                [
                    "{",
                    '"classname" "worldspawn"',
                    "{",
                    MAP_FACE,
                    "}",
                    "}",
                    "{",
                    '"classname" "func_detail"',
                    "{",
                    MAP_FACE,
                    "}",
                    "}",
                ]
            )
        )

        self.assertEqual([entity.index for entity in entities], [0, 1])
        self.assertEqual(entities[1].properties["classname"], "func_detail")
        self.assertEqual(len(entities[1].brushes), 1)
        self.assertEqual(entities[1].brushes[0].index, 0)
        self.assertEqual(len(entities[1].brushes[0].faces), 1)

    def test_preserves_comment_indices_when_present(self) -> None:
        entities = self.parse(
            "\n".join(
                [
                    "// entity 7",
                    "{",
                    '"classname" "func_detail"',
                    "// brush 12",
                    "{",
                    MAP_FACE,
                    "}",
                    "}",
                ]
            )
        )

        self.assertEqual(len(entities), 1)
        self.assertEqual(entities[0].index, 7)
        self.assertEqual(entities[0].brushes[0].index, 12)


if __name__ == "__main__":
    unittest.main()
