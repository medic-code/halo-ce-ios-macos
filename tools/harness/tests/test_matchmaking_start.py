"""The countdown of a full small matchmade game and START NOW (matchmaking.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["full_starts", "leave_stops", "start_now_holds", "not_ready", "big_never_counts_down",
         "start_now_while_part_full", "start_now_needs_the_server", "big_full_keeps_start_now", "lobby_locks",
         "browser_joiner_locks", "big_teams_stay_open"]

NEGATIVE_CONTROLS = {
    "short-lobby-counts": (("players < most_players", "players < most_players - 1"), "leave_stops"),
    "five-seconds-shortened": (("4999", "499"), "full_starts"),
    "start-now-stopped": (("if (start_now)", "if (0)"), "start_now_holds"),
    "pause-ignored": ((" && !network_game_server_port_countdown_paused(server)", ""), "not_ready"),
    "big-counts-down": ((" || p2p_mm_size_big(matchmaking.size))\n", ")\n"), "big_never_counts_down"),
    "start-now-ignores-server": ((" && network_game_server_port_ok_to_countdown(server) &&\n\t\t(", " &&\n\t\t("),
                                 "start_now_needs_the_server"),
    "small-full-start-now": (("p2p_mm_size_big(matchmaking.size) || ", "1 || "), "start_now_while_part_full"),
    "big-full-no-start-now": (("p2p_mm_size_big(matchmaking.size) || ", ""), "big_full_keeps_start_now"),
    "big-teams-locked": ((" && !p2p_mm_size_big(matchmaking.size)", ""), "lobby_locks"),
    "browser-joiner-unlocked": ((" && p2p_joined_listed_big_game()", " && 0"), "browser_joiner_locks"),
    "browser-joiner-public-locked": ((" && p2p_joined_listed_big_game()", ""), "browser_joiner_locks"),
    "big-host-locked-by-listing": (("(!global_network_game_server_get() && ", "("), "browser_joiner_locks"),
    "stale-size-locks-teams": (("matchmaking.role != _matchmaking_idle && !matchmaking.public_game && !p2p_mm_size_big(matchmaking.size)",
                                "matchmaking_game_locked() && !p2p_mm_size_big(matchmaking.size)"), "browser_joiner_locks"),
    "switch-team-hidden-when-locked": (("!matchmaking_teams_locked()", "!matchmaking_game_locked()"),
                                       "big_teams_stay_open"),
    "host-keeps-team-when-locked": (("|| matchmaking_teams_locked()", "|| matchmaking_game_locked()"),
                                    "big_teams_stay_open"),
}


def team_rules():
    shown = re.search(r'visible_set\(named\(list, "lobby_button_team", 0\), (.*)\);',
                      read("port/linux/game/menu_functions.c")).group(1)
    kept = re.search(r"\((!game->variant\.universal_variant\.teams \|\|.*?matchmaking_\w+_locked\(\))\)\)\s*\{\s*"
                     r"player\.team_index = game->players",
                     read("source/networking/network_server_message_handler.c"), re.S).group(1)
    return (f"static boolean team_button_shown(struct network_game *game) {{ return {shown}; }}\n"
            f"static boolean host_keeps_team(struct network_game *game, struct player_update player)\n"
            f"{{ return {kept}; }}\n")


def generated(fault=None):
    source = read("port/linux/game/matchmaking.c")
    seconds = re.search(r"^#define MATCHMAKING_START_MILLISECONDS .*$", source, re.M).group(0)
    code = "\n".join(function(source, name) for name in ("matchmaking_start_countdown", "matchmaking_can_start_now",
                                                         "matchmaking_game_locked", "matchmaking_teams_locked"))
    code += "\n" + team_rules()
    if fault and fault[0] == "4999":
        seconds = mutated(seconds, *fault)
    elif fault:
        code = mutated(code, *fault)
    return (("under_test.inc", seconds + "\n" + code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matchmaking_start", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("matchmaking_start", generated(fault)), case)
    assert status == CHECK_FAILED, f"matchmaking.c with a fault ({control}) passed '{case}': the test cannot see it"
