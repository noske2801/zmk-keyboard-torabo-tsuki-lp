"""Read-only regression checks for the S-only projection of the pinned baseline.

Uses only the Python standard library. No builds, downloads, or device writes.
The fixed fingerprints below were derived from the original 66-key sources at
BASELINE_COMMIT, using OLD_POSITIONS in S order. Optionally set
S_LAYOUT_BASELINE to a directory containing those original repository files to
also compare every binding and position reference directly, without a network
request. Do not regenerate fingerprints from the implementation under test.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHIELD = Path("boards/shields/torabo_tsuki_lp")
BASELINE_COMMIT = "b7da59aae67c30637fc544bb437fd4fcf42a4912"
OLD_POSITIONS = [
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    25, 26, 27, 28, 29, 32, 33, 34, 35, 36,
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
]
NEW_POSITION = {old: new for new, old in enumerate(OLD_POSITIONS)}
BASELINE = None
if os.environ.get("S_LAYOUT_BASELINE"):
    BASELINE = Path(os.environ["S_LAYOUT_BASELINE"]).resolve()

# These values are filled once from the pinned, unmodified baseline.
EXPECTED_LAYER_HASHES = {'DEFAULT': '3be91c4f7be11187a489aa286be883bf6197aa105e0de553ce93b0235a6f2117',
 'ROMAJI': '3fc67faf6eb02ec646a4bb5f28e21cd0493220f9e3b857f2c6b84873b3fc843d',
 'ROMAJI_SHIFT': '7f6f6730ae06e87f593303a9078d4a7317270b4f1e4979abc244a38a84eb1c7d',
 'BLANK': '763c270fa5dc79267455cf4e64ccfa8cb874cb650e778db179acbc55cd638429',
 'LOWER': '282f79c967692b9805b387ee832fbd3bebd6f87f77381cec56d488513acb44ac',
 'RAISE': 'fcdea9abda49f3faa5b107705b46766638f28324b05ca6236472aa6ddc3fe749',
 'EXTRA': 'f3041495574b07103648c25f7a0f366ec8976220ca451fc167145ffad8fc6b47',
 'SYSTEM': 'cc881003a5b2d0b0aec5932f6b0cc07148207d99925899a86c8d402ddcd18a43',
 'MOUSE': '585e7643a263c202214b926f5147b2bdedc64c9e9f97fd093402d94606d2bd69',
 'SCROLL_X': '51e70b8584984a508364914b98d1d4a652dd094e176f3c62d97c0da10ea233cd',
 'SCROLL_Y': '51e70b8584984a508364914b98d1d4a652dd094e176f3c62d97c0da10ea233cd',
 'SCROLL_XY': '51e70b8584984a508364914b98d1d4a652dd094e176f3c62d97c0da10ea233cd'}
EXPECTED_POSITION_HASH = 'b12c652f7854c51287023d8b9b001fc08b17b5f2d6412551213acb2a3681508d'
EXPECTED_SCANNER_HASH = '58ec423d6d8c153a031c2b5f5771baeb8b751e2942424087e1f9ca1ad9626a0f'
EXPECTED_GEOMETRY_HASH = '94df48db4ec362f1b394f156fa2979c7aadb1bd2c9a9991d7dc3a2764d4d47a5'
EXPECTED_EDITOR_HASH = '0baf1c3ff72d7e017202d617df5beca74479bbad88863092bc33f42006450b3c'
EXPECTED_NONPOSITION_HASH = '2af14746825a1482da711587fa8e7885c9d1c84677acc92c76897c178ab63dff'
EXPECTED_UNCHANGED_HASHES = {'.github/workflows/build.yml': 'd07100d5bf5a89a1839c995e3bc7703d70a91721351ade2f66ef9c3b1b27a1a5',
 'CMakeLists.txt': '136735dc5968fba83109a58599995aa2c54cb957e3c48ea482f16338704cff54',
 'build.yaml': '7f6e76bd74dd0a67744ef16a5518b1942d31dbbd06f67ee7dbcf5c198529776a',
 'config/west.yml': '51c9c6250fb9d9bf823952d7b2d59d4d2eb081aa6e2222d5a4aaf6263aba8498',
 'src/board.c': '82ac24a6ca1dd2ae716eb4e30e300b3572e7a4f5500d13424c439dcb517b0947',
 'zephyr/module.yml': '048afda22031611dcbd5d6eba8a821f49b1c14b3801dcfb284b976107677f010',
 'boards/shields/torabo_tsuki_lp/Kconfig.defconfig': '31a8f0b89d94486a5267dc99038c98d81c59127b9881af875238f9e82a28339d',
 'boards/shields/torabo_tsuki_lp/Kconfig.shield': 'bc9de0a80784845c0375ffeb3317f2f904129669e936aee145ff3fb03c5a0909',
 'boards/shields/torabo_tsuki_lp/torabo_tsuki_lp.keymap': '6b537b1e4071de4f6080e330de3aeac35c36fcda0e76a1bb3a4b4fc17f59b7fd',
 'boards/shields/torabo_tsuki_lp/torabo_tsuki_lp.zmk.yml': '54f7975c0cfdf16757822e1d3246dca94744049250f6c54446cfc1a1d78fb453',
 'boards/shields/torabo_tsuki_lp/torabo_tsuki_lp_left.conf': '5cc94fce335820454518b7fa19acc26e8d51fb816728e282ea8d1ff4c009207d',
 'boards/shields/torabo_tsuki_lp/torabo_tsuki_lp_right.conf': '5cc94fce335820454518b7fa19acc26e8d51fb816728e282ea8d1ff4c009207d',
 'boards/shields/torabo_tsuki_lp/torabo_tsuki_lp_left.overlay': '3904f66774745c338e7155ce3ba4ed293397ba4dabfaaa50b016e60b1b571e70'}


def read(path, root=ROOT):
    return (root / path).read_text(encoding="utf-8")


def without_comments(text):
    return re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", text)


def normalized(text):
    return " ".join(without_comments(text).split())


def fingerprint(value):
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def block(text, anchor):
    start = text.index("{", text.index(anchor)) + 1
    depth = 1
    for end in range(start, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end]
    raise AssertionError(f"Unclosed block: {anchor}")


def layer_bindings(text):
    keymap = block(without_comments(text), "keymap {")
    layers = {}
    for match in re.finditer(r"(\w+)\s*\{\s*bindings\s*=\s*<([\s\S]*?)>\s*;", keymap):
        layers[match[1]] = [
            " ".join(binding.split())
            for binding in re.split(r"(?=&)", match[2])
            if binding.strip()
        ]
    return layers


def position_semantics(text):
    source = without_comments(text)
    macros = {
        name: [int(v) for v in re.search(r"#define " + name + r" ([^\n]+)", source)[1].split()]
        for name in ("KEYS_L", "KEYS_R", "THUMBS")
    }
    combos = {}
    for match in re.finditer(r"(\w+)\s*\{([^{}]*)\}", block(source, "combos {")):
        positions = re.search(r"(?<![\w-])key-positions\s*=\s*<([^>]+)>", match[2])
        if positions:
            combos[match[1]] = [int(v) for v in positions[1].split()]
    holds = {}
    for match in re.finditer(r"(\w+)\s*\{([^{}]*)\}", source):
        positions = re.search(r"hold-trigger-key-positions\s*=\s*<([^>]+)>", match[2])
        if positions:
            expanded = []
            for value in positions[1].split():
                expanded.extend(macros[value] if value in macros else [int(value)])
            holds[match[1]] = expanded
    excluded = re.search(r"excluded-positions\s*=\s*<([^>]+)>", source)[1]
    return {
        "macros": macros,
        "combos": combos,
        "holds": holds,
        "excluded": [int(v) for v in excluded.split()],
    }


def project_positions(value):
    if isinstance(value, dict):
        return {key: project_positions(item) for key, item in value.items()}
    if isinstance(value, list):
        return [project_positions(item) for item in value]
    return NEW_POSITION[value]


def matrix_coordinates(source):
    return [
        (int(row), int(col))
        for row, col in re.findall(r"RC\((\d+),\s*(\d+)\)", block(source, "size_s_transform:"))
    ]


def nonposition_keymap_source(text):
    source = without_comments(text)
    source = source[:source.index("    keymap {")]
    source = re.sub(r"#define (?:KEYS_L|KEYS_R|THUMBS) [^\n]+", "", source)
    source = re.sub(
        r"(?<![\w-])(?:key-positions|hold-trigger-key-positions|excluded-positions)\s*=\s*<[^>]+>\s*;",
        "POSITION_PROPERTY;",
        source,
    )
    return " ".join(source.split())


class SLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dtsi = read(SHIELD / "torabo_tsuki_lp.dtsi")
        cls.layouts = read(SHIELD / "torabo_tsuki_lp_layouts.dtsi")
        cls.keymap = read("config/keymap.keymap")

    def test_only_s_layout_is_defined_and_selected(self):
        self.assertIn("zmk,physical-layout = &physical_layout_s;", self.dtsi)
        self.assertEqual(1, self.dtsi.count('compatible = "zmk,physical-layout";'))
        self.assertEqual(1, self.dtsi.count('compatible = "zmk,matrix-transform";'))
        for filename in ("torabo_tsuki_lp.dtsi", "torabo_tsuki_lp_layouts.dtsi", "torabo_tsuki_lp_right.overlay"):
            source = read(SHIELD / filename)
            self.assertNotRegex(source, r"(?:physical_layout|size)_[lm](?:_transform)?\b")
            self.assertNotIn("position_map", source)

    def test_matrix_coordinates_are_unchanged_and_in_bounds(self):
        expected = []
        for row in (1, 2, 3, 4):
            columns = [1, 2, 3, 4, 5]
            if row >= 3:
                columns += [6, 13]
            columns += [12, 11, 10, 9, 8]
            expected.extend((row, col) for col in columns)
        coordinates = matrix_coordinates(self.dtsi)
        self.assertEqual(expected, coordinates)
        transform = block(self.dtsi, "size_s_transform:")
        self.assertRegex(transform, r"rows\s*=\s*<5>")
        self.assertRegex(transform, r"columns\s*=\s*<14>")
        self.assertEqual(44, len(set(coordinates)))
        self.assertTrue(all(0 <= row < 5 and 0 <= col < 14 for row, col in coordinates))

    def test_gpio_scanner_is_unchanged(self):
        scanner = block(self.dtsi, "kscan0:")
        self.assertEqual(EXPECTED_SCANNER_HASH, fingerprint(normalized(scanner)))
        cols = re.search(r"col-gpios\s*=([\s\S]*?);", scanner)[1]
        rows = re.search(r"row-gpios\s*=([\s\S]*?);", scanner)[1]
        self.assertEqual(7, cols.count("<&gpio"))
        self.assertEqual(5, rows.count("<&gpio"))

    def test_right_side_keeps_column_offset_seven(self):
        right = read(SHIELD / "torabo_tsuki_lp_right.overlay")
        self.assertRegex(block(right, "&size_s_transform"), r"col-offset\s*=\s*<7>")
        self.assertEqual(1, right.count("col-offset"))
        self.assertNotIn("col-offset", read(SHIELD / "torabo_tsuki_lp_left.overlay"))

    def test_physical_geometry_is_the_original_44_key_s_layout(self):
        geometry = block(self.layouts, "physical_layout_s:")
        self.assertEqual(44, geometry.count("&key_physical_attrs"))
        self.assertEqual(EXPECTED_GEOMETRY_HASH, fingerprint(normalized(geometry)))

    def test_every_layer_preserves_all_retained_bindings(self):
        layers = layer_bindings(self.keymap)
        self.assertEqual(set(EXPECTED_LAYER_HASHES), set(layers))
        self.assertEqual(12, len(layers))
        self.assertEqual(528, sum(map(len, layers.values())))
        for name, bindings in layers.items():
            with self.subTest(layer=name):
                self.assertEqual(44, len(bindings))
                self.assertEqual(EXPECTED_LAYER_HASHES[name], fingerprint(bindings))

    def test_every_position_reference_preserves_its_original_key(self):
        semantics = position_semantics(self.keymap)
        self.assertEqual(28, len(semantics["combos"]))
        self.assertEqual(5, len(semantics["holds"]))
        self.assertEqual(EXPECTED_POSITION_HASH, fingerprint(semantics))
        lists = list(semantics["macros"].values()) + list(semantics["combos"].values())
        lists += list(semantics["holds"].values()) + [semantics["excluded"]]
        self.assertTrue(all(0 <= value < 44 for values in lists for value in values))

    def test_editor_order_is_the_original_s_projection(self):
        # info.json row/col describe editor formatting, not electrical RC.
        info = json.loads(read("config/info.json"))
        keys = info["layouts"]["LAYOUT"]["layout"]
        self.assertEqual(44, len(keys))
        self.assertEqual(EXPECTED_EDITOR_HASH, fingerprint(keys))
        self.assertEqual([], info["sensors"])

    def test_unrelated_configuration_is_unchanged(self):
        for path, expected in EXPECTED_UNCHANGED_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, fingerprint(read(path).strip()))

    def test_layer_numbers_and_nonposition_behaviors_are_unchanged(self):
        self.assertEqual(EXPECTED_NONPOSITION_HASH, fingerprint(nonposition_keymap_source(self.keymap)))
        source = without_comments(self.keymap)
        names = ["DEFAULT", "ROMAJI", "ROMAJI_SHIFT", "BLANK", "LOWER", "RAISE", "EXTRA", "SYSTEM", "MOUSE", "SCROLL_X", "SCROLL_Y", "SCROLL_XY"]
        for number, name in enumerate(names):
            self.assertRegex(source, r"#define " + name + r" " + str(number) + r"\b")

    @unittest.skipUnless(BASELINE is not None, "Set S_LAYOUT_BASELINE for full direct baseline comparison")
    def test_direct_baseline_projection_of_every_binding_and_position(self):
        original = read("config/keymap.keymap", BASELINE)
        old_layers = layer_bindings(original)
        expected = {name: [bindings[i] for i in OLD_POSITIONS] for name, bindings in old_layers.items()}
        self.assertEqual(expected, layer_bindings(self.keymap))
        self.assertEqual(project_positions(position_semantics(original)), position_semantics(self.keymap))
        removed = set(range(66)) - set(OLD_POSITIONS)
        self.assertEqual(22, len(removed))
        for name, bindings in old_layers.items():
            self.assertEqual(66, len(bindings), name)
            self.assertTrue(all(bindings[i] in ("&none", "&trans") for i in removed), name)
        self.assertEqual(matrix_coordinates(read(SHIELD / "torabo_tsuki_lp.dtsi", BASELINE)), matrix_coordinates(self.dtsi))


if __name__ == "__main__":
    unittest.main()
