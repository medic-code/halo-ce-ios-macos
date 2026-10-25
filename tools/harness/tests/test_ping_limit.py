"""The host's ping limit (network.host_ping_limit, network_ping_limit.c): a machine's median round trip over the limit,
taken once a second from the internet play tunnel, in the lobby and in a machine's first seconds in a game, and never
for a machine the tunnel does not reach or one downloading the maps."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

SOURCE = "port/linux/game/network_ping_limit.c"
CASES = ["median", "spike", "threshold", "off", "kicked-at-third-second", "stays-under", "once-a-second",
         "byte-order", "lan-exempt", "not-timed", "joined-in-game", "late-in-game", "downloading", "joined-resets",
         "smoothed-start"]
FUNCTIONS = ("ping_limit_median", "ping_limit_over", "ping_limit_tunnel_ping", "network_ping_limit_machine_joined",
             "network_ping_limit_check")

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "upper-median": (("return sorted[(count - 1) / 2];", "return sorted[count / 2];"), "ping_limit_median", "median"),
    "two-samples-judged": (("count < PING_LIMIT_MINIMUM_SAMPLES ||", "count < 2 ||"), "ping_limit_median", "median"),
    "at-limit-kicks": (("median > limit", "median >= limit"), "ping_limit_over", "threshold"),
    "off-kicks": (("limit > 0 && ", ""), "ping_limit_over", "off"),
    "every-call-sampled": (("||\n\t\tnow - ping_limit_machines[machine_index].sampled_at < PING_LIMIT_SAMPLE_MILLISECONDS)",
                            ")"), "network_ping_limit_check", "once-a-second"),
    "host-byte-order": (("network = (address >> 24) | ((address >> 8) & 0xFF00) | ((address << 8) & 0xFF0000) | "
                         "(address << 24);", "network = address;"), "ping_limit_tunnel_ping", "byte-order"),
    "any-address": (("if ((address & PING_LIMIT_INTERNET_MASK) != PING_LIMIT_INTERNET_ADDRESS)\n\t\treturn NONE;\n", ""),
                    "ping_limit_tunnel_ping", "lan-exempt"),
    "judged-all-game": (("(!lobby && joined > PING_LIMIT_ADMISSION_MILLISECONDS) ||", ""),
                        "network_ping_limit_check", "late-in-game"),
    "judged-downloading": ((" ||\n\t\tmap_share_machine_downloading(machine_index))", ")"),
                           "network_ping_limit_check", "downloading"),
    "join-keeps-samples": (("ping_limit_machines[machine_index].count = 0;", ""),
                           "network_ping_limit_machine_joined", "joined-resets"),
}


def generated(fault=None, faulty_function=None):
    source = read(SOURCE)
    config = "#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 16\n"
    config += enum_with(source, "PING_LIMIT_SAMPLES =") + "\n"
    config += structure(source, "ping_limit_machine") + "\n"
    config += "static struct ping_limit_machine ping_limit_machines[MAXIMUM_PING_LIMIT_MACHINES];\n"
    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("ping_limit", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("ping_limit", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_off_by_default():
    # a departure from the stock game: no limit unless the host sets one (port_config.c)
    config = read("port/linux/src/port_config.c")
    assert '{ "network.host_ping_limit", _config_integer, "0", "HALO_NET_HOST_PING_LIMIT"' in config


def test_host_kicks_remote_machines_rejoinable():
    # the host's server judges only joined machines not its own, in the lobby until the start is sent, and kicks as
    # the host's kick does (the machine may join again); a new machine at a slot starts with no round trips
    server = read("source/networking/network_server_manager.c")
    loop = function(server, "network_game_server_handle_client_machines")
    guard = loop[:loop.index("network_ping_limit_check(")]
    guard = guard[guard.rindex("if (VALID_INDEX(client_machine->machine_index"):]
    assert "network_game_server_client_machine_is_joined_to_game(server, client_machine)" in guard
    assert "!network_game_server_client_machine_is_local(server, client_machine)" in guard
    assert "server->state == _network_game_server_state_pregame && !server->sent_start_game_message," in loop
    assert "network_ping_limit_kicked(names, ping);\n\t\t\t\t\tnetwork_game_server_kick_pending[machine_index] = " \
        "_kick_rejoinable;" in loop
    assert "network_ping_limit_machine_joined(machine_index);" in function(
        server, "network_game_server_accept_client_machine_into_game")
