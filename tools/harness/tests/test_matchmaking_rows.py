"""The MATCHMAKING screen's EXTENDED SEARCH, MAX PING and REGION rows (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402
import port_settings  # noqa: E402

CASES = ["ping_snaps", "region_rows", "defaults", "member_rows", "locked", "help", "spinners"]

NEGATIVE_CONTROLS = {
    "ping-no-any": (("while (row < (short)MATCHMAKING_PING_ANY && ", "while (row < 3 && "), "ping_snaps"),
    "region-case-sensitive": (("csstrcasecmp(setting, matchmaking_region_settings[row])",
                               "strcmp(setting, matchmaking_region_settings[row])"), "region_rows"),
    "auto-shows-region": (('strings="AUTO|', 'strings="AUTO (EU)|'), "spinners"),
    "region-skips-any": (("short row = MATCHMAKING_REGION_ROWS - 1;", "short row = MATCHMAKING_REGION_ROWS - 2;"),
                         "region_rows"),
    "auto-not-flagged": (("choice->region_auto = !region;", "choice->region_auto = 0;"), "member_rows"),
    "leader-auto-shown-any": (("return choice->region_auto ? 0 : ", "return "), "member_rows"),
    "searching-unlocked": (("|| party->search_state != P2P_MM_OFF", ""), "locked"),
    "ping-default-any": (('"200", "HALO_NET_MATCHMAKING_MAX_PING"', '"0", "HALO_NET_MATCHMAKING_MAX_PING"'),
                         "defaults"),
    "big-help-says-widen": (('sizes too."/>\n\t\t<string text="Big games always stay their size.',
                             'sizes too."/>\n\t\t<string text="OFF: only BIG TEAM games.'), "help"),
    "help-off-by-one": (("_matchmaking_help_ping = _matchmaking_help_extended + NUMBER_OF_P2P_MM_SIZES,",
                         "_matchmaking_help_ping = _matchmaking_help_extended + NUMBER_OF_P2P_MM_SIZES - 1,"), "help"),
    "region-help-ignores-row": (("return region ? _matchmaking_help_region_set :", "return 0 ? _matchmaking_help_region_set :"),
                                "help"),
    "region-help-auto-only": (("<string text=\"Region steers big games.\"/>", "<string text=\"AUTO picks EU from your clock.\"/>"),
                              "help"),
}

MATCHMAKING_XML = "port/assets/menus/ce/main_menu.multiplayer_type_select.matchmaking.xml"
# small_ui's line pitch on the MATCHMAKING help, measured on the Mac at 20.3 px: three lines need 66
HELP_LINES_HEIGHT = 66
BAR_TOP = 414

# matchmaking_screen_update's writes, which a member never makes (E3)
MEMBER_WRITES = ["\tif (!member)\n\t\tmatchmaking_rows_save(extended, ping, region);",
                 "\tif (!member && (playlists != matchmaking_screen.playlists"]


def c_strings(name, texts):
    quoted = ", ".join('"' + text.replace("\\n", " ").replace("\n", "\\n").replace('"', '\\"') + '"' for text in texts)
    return f"static char const *const {name}[] = {{ {quoted} }};"


def config_default(source, name):
    return re.search(r'\{ "' + re.escape(name) + r'", _config_\w+, "((?:[^"\\]|\\.)*)"', source).group(1).replace('\\"', "")


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    search = read("port/linux/src/p2p_matchmaking.c")
    config = read("port/linux/src/port_config.c")
    xml = read(MATCHMAKING_XML)
    if fault and fault[0] in config:
        config, fault = mutated(config, *fault), None
    if fault and fault[0] in xml:
        xml, fault = mutated(xml, *fault), None
    helps = re.findall(r'<string text="([^"]*)"/>', xml[xml.index('matchmaking/help_strings">'):].split("</strings>")[0])
    regions = re.search(r'matchmaking/region_spinner"[^>]*strings="([^"]*)"', xml).group(1).split("|")
    defines = [re.search(r"^#define " + name + r" .*$", source, re.M).group(0)
               for name in ("MATCHMAKING_CLOCK_REGIONS", "MATCHMAKING_PING_ANY", "MATCHMAKING_REGION_ROWS")]
    tables = [re.search(r"^static \w+ const (?:\*const )?" + name + r"\[\].*$", source, re.M).group(0)
              for name in ("matchmaking_ping_limits", "matchmaking_ping_texts", "matchmaking_region_settings")]
    code = "\n".join(defines + [enum_with(source, "MATCHMAKING_ONE_PLAYER"), enum_with(source, "_matchmaking_help_size"),
                                enum_with(source, "_matchmaking_row_extended")] + tables +
                     [re.search(r"^const .*\b" + name + r"\[\].*$", search, re.M).group(0)
                      for name in ("p2p_mm_ping_limits", "p2p_mm_region_names")] +
                     [function(search, name) for name in ("p2p_mm_max_ping", "p2p_mm_region")] +
                     [function(source, name) for name in
                      ("matchmaking_ping_row", "matchmaking_region_row", "matchmaking_clock_region",
                       "matchmaking_region_help", "matchmaking_region_of_choice",
                       "matchmaking_choice_rows", "matchmaking_party_member", "matchmaking_rows_locked")])
    if fault:
        code = mutated(code, *fault)
    defaults = [f'#define DEFAULT_{key} "{config_default(config, "network.matchmaking_" + key.lower())}"'
                for key in ("EXTENDED", "MAX_PING", "REGION")]
    return (("under_test.inc", "\n".join([
        "#include <strings.h>", "#define csstrcasecmp strcasecmp", "#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))",
        enum_with(header, "P2P_LISTING_REGION_SIZE"), enum_with(header, "P2P_MM_ANY"), enum_with(header, "P2P_MM_SLAYER"),
        enum_with(header, "P2P_MM_1V1"), enum_with(header, "P2P_MM_OFF"), enum_with(header, "P2P_PARTY_MAXIMUM"),
        structure(header, "p2p_mm_choice"), structure(header, "p2p_party_member"),
        structure(header, "p2p_party_status"), *defaults,
        c_strings("help_texts", helps), c_strings("help_raw", [text.replace("\\n", "\n") for text in helps]),
        c_strings("ping_strings", port_settings.MATCHMAKING_PINGS),
        c_strings("region_strings", regions), code]) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matchmaking_rows", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("matchmaking_rows", generated(fault)), case)
    assert status == CHECK_FAILED, f"menu_functions.c with a fault ({control}) passed '{case}': the test cannot see it"


def member_writes_guarded(update):
    return all(guarded in update for guarded in MEMBER_WRITES) and update.count("config_write(") == 2 and \
        update.count("matchmaking_rows_save(") == 1


def test_member_never_writes_config():
    assert member_writes_guarded(function(read("port/linux/game/menu_functions.c"), "matchmaking_screen_update")), \
        "E3: matchmaking_screen_update can write a member's config"


@pytest.mark.parametrize("guarded", MEMBER_WRITES)
def test_member_writes_control(guarded):
    update = function(read("port/linux/game/menu_functions.c"), "matchmaking_screen_update")
    assert not member_writes_guarded(mutated(update, guarded, guarded.replace("!member", "1")))


def holds_three_lines(xml):
    top, height = map(int, re.search(r'matchmaking/help" type="text"[^>]* top="(\d+)"[^>]* height="(\d+)"',
                                     xml).groups())
    return height >= HELP_LINES_HEIGHT and top + height <= BAR_TOP


def test_help_holds_three_lines():
    assert holds_three_lines(read(MATCHMAKING_XML)), "E10: the help box drops a third line or covers the bar"


def test_help_box_control():
    assert not holds_three_lines(mutated(read(MATCHMAKING_XML), f'height="{HELP_LINES_HEIGHT}" string_list',
                                         'height="54" string_list'))


# matchmaking_screen_update at BIG TEAM and BIG FFA: EXTENDED SEARCH greyed, its stored value kept
BIG_EXTENDED = ["\tif (p2p_mm_size_big(size) && !member)\n\t\textended_spinner->parameters.list.selected_index = extended = "
                "matchmaking_screen.extended;",
                "\tif (p2p_mm_size_big(size))\n\t\textended_spinner->parent->alpha_modifier = TAB_UNFOCUSED_ALPHA;"]


def big_extended_greyed(update):
    return all(line in update for line in BIG_EXTENDED) and \
        update.index(BIG_EXTENDED[0]) < update.index("matchmaking_rows_save(")


def test_big_extended_greyed():
    assert big_extended_greyed(function(read("port/linux/game/menu_functions.c"), "matchmaking_screen_update")), \
        "EXTENDED SEARCH is live at a big size"


@pytest.mark.parametrize("line", BIG_EXTENDED)
def test_big_extended_control(line):
    update = function(read("port/linux/game/menu_functions.c"), "matchmaking_screen_update")
    assert not big_extended_greyed(mutated(update, line, line.replace("p2p_mm_size_big(size)", "0")))
