"""The server browser's column titles that sort its games (menu_functions.c's
browser_sort_column, browser_sort_headers_show and browser_sort)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/menu_functions.c')
    headers = re.search(r"^static char const \*const browser_sort_headers\[\] =.*?^\};", source, re.M | re.S).group(0)
    code = '\n'.join([headers] + [function(source, name) for name in
                                  ('browser_sort_column', 'browser_sort_headers_show', 'browser_sort')])
    if fault:
        code = mutated(code, *fault)
    return (('orders.inc', enum_with(read('port/linux/src/p2p.h'), 'P2P_LOBBY_ORDER_PLAYERS')),
            ('under_test.inc', code))


@pytest.mark.parametrize('case', ['columns', 'arrows', 'focus', 'toggle', 'ready', 'lan'])
def test_case(case):
    status, output = run(build('browser_sort', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('(order == P2P_LOBBY_ORDER_PLAYERS) == reversed);', '!reversed);'), 'arrows'),
    (('header->disabled = browser_sort_column(header) == NONE;', 'header->disabled = FALSE;'), 'focus'),
    (('headers->focused_child = header;', ';'), 'focus'),
    (('p2p_lobby_order(&reversed) == column && !reversed', 'p2p_lobby_order(&reversed) >= 0 && !reversed'), 'toggle'),
    (('p2p_lobby_order(&reversed) == column && !reversed', '0'), 'toggle'),
    (('lobby_browser.first = 0;', ';'), 'toggle'),
    (('if (lobby_browser.ready)', 'if (0)'), 'ready'),
    (('multiplayer.mode != _multiplayer_mode_server_browser || ', ''), 'lan'),
])
def test_negative_control(fault, case):
    status, output = run(build('browser_sort', generated(fault)), case)
    assert status == CHECK_FAILED, output
