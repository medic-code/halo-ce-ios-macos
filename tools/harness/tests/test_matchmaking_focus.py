"""MATCHMAKING's screen gives its focus to the options, not a scroll arrow."""
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import read

BASE = "main_menu/multiplayer_type_select/matchmaking"


def first_focusable(text):
    """the engine focuses a screen's first child that handles an event"""
    menus = ElementTree.fromstring(text.encode("latin-1"))
    widgets = {widget.get("name"): widget for widget in menus.iter("widget")}
    for child in widgets[f"{BASE}/matchmaking_screen"].findall("child"):
        name = child.get("widget")
        if name in widgets and (widgets[name].find("on") is not None or widgets[name].find("child") is not None):
            return name
    return None


def screen():
    return read(f"port/assets/menus/ce/{BASE.replace('/', '.')}.xml")


def test_options_take_the_focus():
    assert first_focusable(screen()) == f"{BASE}/options_menu"


def test_arrows_first_take_the_focus():
    text = screen()
    options = f'<child widget="{BASE}/options_menu"/>'
    arrow = f'<child widget="{BASE}/party_scroll_up"/>'
    swapped = text.replace(options, "\0").replace(arrow, options).replace("\0", arrow)
    assert first_focusable(swapped) == f"{BASE}/party_scroll_up"
