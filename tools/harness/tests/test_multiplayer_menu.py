"""The Multiplayer menu's rows and their focus bars."""
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import read

MT = "main_menu/multiplayer_type_select"
CURVE = 393
BAR = 32


def rows():
    menus = ElementTree.fromstring(read(f"port/assets/menus/ce/{MT.replace('/', '.')}.xml").encode("latin-1"))
    for widget in menus.iter("widget"):
        if widget.get("name") == f"{MT}/multiplayer_type_select_list":
            return [(child.get("widget"), int(child.get("y", 0))) for child in widget.iter("child")
                    if not child.get("widget").endswith("_button_bar")]
    raise LookupError("multiplayer_type_select_list")


def faults(listed):
    above = [y for _, y in listed[:-1]]
    gametypes = listed[-1]
    found = []
    if [name for name, _ in listed[:2]] != [f"{MT}/multiplayer_type_join_item", f"{MT}/multiplayer_type_matchmaking_item"]:
        found.append("MATCHMAKING is not the row under JOIN GAME")
    if any(b - a != 29 for a, b in zip(above, above[1:])):
        found.append("the rows above the divider are not 29 apart")
    if gametypes[0] != f"{MT}/multiplayer_type_gametypes_item" or gametypes[1] + BAR > CURVE:
        found.append("EDIT GAMETYPES's bar is on the curve")
    return found


def spaced(gap):
    names = ["join", "matchmaking", "join_internet", "join_lan", "join_direct", "create", "create_internet",
             "create_lan", "coop"]
    listed = [(f"{MT}/multiplayer_type_{name}_item", 78 + gap * index) for index, name in enumerate(names)]
    return listed + [(f"{MT}/multiplayer_type_gametypes_item", listed[-1][1] + 48)]


@pytest.mark.parametrize("listed, expected", [
    (spaced(29), []),
    (spaced(33), ["the rows above the divider are not 29 apart", "EDIT GAMETYPES's bar is on the curve"]),
])
def test_faults_finds_the_old_spacing(listed, expected):
    assert faults(listed) == expected


def test_the_menu_has_matchmaking_and_every_bar_inside_the_curve():
    assert faults(rows()) == []
