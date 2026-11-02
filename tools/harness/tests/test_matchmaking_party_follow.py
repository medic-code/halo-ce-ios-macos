"""A member's screens follow the leader's search, from DIRECT LINK too, and not from a game (matchmaking.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402


def generated(fault=None):
    source = read("port/linux/game/matchmaking.c")
    header = read("port/linux/src/p2p.h")
    enums = "\n".join(["#define P2P_LISTING_REGION_SIZE 16", enum_with(header, "P2P_MM_SLAYER"),
                       enum_with(header, "P2P_MM_1V1"), enum_with(header, "P2P_MM_LOOKING"),
                       enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"), structure(header, "p2p_mm_choice"),
                       structure(header, "p2p_mm_status"), enum_with(header, "P2P_PARTY_MAXIMUM"),
                       enum_with(header, "P2P_PARTY_NOTICE_NONE"), structure(header, "p2p_party_member"),
                       structure(header, "p2p_party_status"), enum_with(source, "_matchmaking_joining"),
                       enum_with(source, "_client_state_searching")]) + "\n"
    code = "\n".join(function(source, name) for name in ("matchmaking_client_in_game", "matchmaking_party_update"))
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["main_menu", "direct_link", "in_a_game"])
def test_case(case):
    status, output = run(build("matchmaking_party_follow", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    (("network_game_client_get_state(client, &state_data) != _client_state_searching", "1"), "direct_link"),
    (("network_game_client_get_state(client, &state_data) != _client_state_searching", "0"), "in_a_game"),
])
def test_negative_control(fault, case):
    status, output = run(build("matchmaking_party_follow", generated(fault)), case)
    assert status == CHECK_FAILED, output
