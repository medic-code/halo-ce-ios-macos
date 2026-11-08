"""The friends list (p2p_friends.c): the tag and id of fixed keys against a Python reference (hashlib and
cryptography's Ed25519), friend.key made with mode 0600, read again, and left alone when it cannot be read; the records
(their seal, signature, age, names and replays), friends.txt, and requests sent and answered between players in one
process."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402
from harness.tests.test_master_protocol import crypto_includes  # noqa: E402

CASES = ["tag_vectors", "key_file", "record_round_trip", "bad_records", "once_from_four", "age", "list_file", "topics",
         "names", "replay", "small_order", "add", "junk", "drops_logged", "inbox_restart", "answers", "list_safety", "publish", "party_add",
         "presence_timer", "hello_schedule", "version", "two_copies", "here_recovery", "quit_byes", "drops_at_quit", "quit_order", "party_first",
         "hello_later", "broker_swap", "invite", "invite_strangers", "remove", "remove_twice", "online_rows", "by_id", "invite_searching",
         "request_restart", "card_publish", "lookup", "lookup_two",
         "answer_full", "answer_forms"]

NEGATIVE_CONTROLS = {
    "key-readable-by-all": (("(replace ? O_TRUNC : O_EXCL) | O_WRONLY, 0600)", "(replace ? O_TRUNC : O_EXCL) | O_WRONLY, 0644)"),
                            "key_file"),
    "list-readable-by-all": (("(replace ? O_TRUNC : O_EXCL) | O_WRONLY, 0600)", "(replace ? O_TRUNC : O_EXCL) | O_WRONLY, 0644)"),
                             "list_file"),
    "unreadable-key-overwritten": (("(replace ? O_TRUNC : O_EXCL)", "O_TRUNC"), "key_file"),
    "unreadable-key-replaced": (("friends.on = key_parse(text);", "friends.on = key_parse(text) || (remove(path) == 0 && key_make(path));"),
                                "key_file"),
    "id-with-the-tags-label": (("friend_hash(id_label, (int)sizeof(id_label) - 1, public_key, digest);",
                                "friend_hash(tag_label, (int)sizeof(tag_label) - 1, public_key, digest);"), "tag_vectors"),
    "tag-bits-shifted": (("(group >> (11 - bit % 8)) & 31", "(group >> (10 - bit % 8)) & 31"), "tag_vectors"),
    "short-inbox-read": (("found ? 2 * FRIEND_INBOX_BYTES : 2 * P2P_SEED_SIZE", "found ? 2 * FRIEND_INBOX_BYTES - 1 : 2 * P2P_SEED_SIZE"),
                         "key_file"),
    "new-key-not-logged": (('platform_log("friends: new key");', ""), "key_file"),
    "unmade-key-unshown": (("friends.key_unmade = 1;", "(void)0;"), "key_file"),
    "signature-not-checked": (("return FRIEND_DROP_SIGNATURE;", "(void)0;"), "bad_records"),
    "sequence-not-checked": (("if (friend && record.sequence <= friend->last_sequence)", "if (0)"), "replay"),
    "age-not-checked": (("return FRIEND_DROP_AGE;", "(void)0;"), "age"),
    "nonce-not-matched": (("!p2p_equal(friend->nonce, record.nonce, FRIEND_NONCE_SIZE)", "0"), "replay"),
    "recipient-not-signed": (("memcpy(message + size, recipient, P2P_KEY_SIZE);", "memset(message + size, 0, P2P_KEY_SIZE);"),
                             "bad_records"),
    "id-in-the-log": (('"friends: friend %d clock off by %lld s", friend->number,', '"friends: friend %s clock off by %lld s", friend->id,'),
                      "age"),
    "small-order-member-added": (("!p2p_ed25519_to_x25519(key, x25519_public)", "0"), "small_order"),
    "zero-shared-secret-kept": (("if (p2p_equal(shared, zeros, P2P_KEY_SIZE))\n\t\treturn 0;", ""), "small_order"),
    "own-record-kept": (("return FRIEND_DROP_SELF;", "(void)0;"), "bad_records"),
    "no-flood-limit": (("if (friends.opened == FRIEND_OPENED_MAXIMUM)", "if (0)"), "junk"),
    "refused-forgotten": (("return FRIEND_DROP_REFUSED;", "(void)0;"), "answers"),
    "no-clear-on-answer": (("request->key, topic))\n\t\tfriend_out(topic, NULL, 0, -1);", "request->key, topic))\n\t\t(void)0;"),
                           "answers"),
    "friend-request-unanswered": (("friend_accept(record.sender, record.nonce);", "(void)0;"), "answers"),
    "nameless-add": (("!p2p_signal_connected() || !own_name_ok()", "!p2p_signal_connected()"), "add"),
    "full-yes-clears": (("\tif (yes && !friend && friends.count == P2P_FRIENDS_MAXIMUM)\n\t\treturn P2P_FRIEND_ADD_FULL;\n"
                         "\tif (friend_pair_topic(friends.public_key, request->key, topic))\n\t\tfriend_out(topic, NULL, 0, -1);\n",
                         "\tif (friend_pair_topic(friends.public_key, request->key, topic))\n\t\tfriend_out(topic, NULL, 0, -1);\n"
                         "\tif (yes && !friend && friends.count == P2P_FRIENDS_MAXIMUM)\n\t\treturn P2P_FRIEND_ADD_FULL;\n"),
                        "answer_full"),
    "row-kept-after-accept": (("\t\tif (index < friends.request_count)\n\t\t\tfriend_request_drop(index);\n\t\tfriend_out(topic, NULL, 0, -1);",
                               "\t\tfriend_out(topic, NULL, 0, -1);"), "answer_forms"),
    "accept-record-kept": (("\t\t\tfriend_request_drop(index);\n\t\tfriend_out(topic, NULL, 0, -1);",
                            "\t\t\tfriend_request_drop(index);\n"), "answer_forms"),
    "add-ignores-request": (("index = friend_request_find(key);\n\tif (index < friends.request_count)\n\t{",
                             "index = friend_request_find(key);\n\tif (0)\n\t{"), "answer_forms"),
    "friend-add-says-sent": (("friend->sent ? P2P_FRIEND_ADD_SENT : P2P_FRIEND_ADD_FRIEND", "P2P_FRIEND_ADD_SENT"), "answer_forms"),
    "nameless-yes": (("if (yes && !own_name_ok())", "if (0)"), "answers"),
    "request-rebuilt-each-pass": (("\tif (!friend->request_size)\n\t\tfriend->request_size =", "\tfriend->request_size ="),
                                  "publish"),
    "unheard-kept": (("friend.sent ? FRIEND_REQUEST_DAYS : FRIEND_FORGOTTEN_DAYS", "friend.sent ? FRIEND_REQUEST_DAYS : 99999"),
                     "list_file"),
    "rename-unchecked": (("if (saved && !posix_server_rename(temporary, path))", "if (saved && !posix_server_rename(temporary, path) && 0)"),
                         "list_file"),
    "unsaved-unshown": (("friends.unsaved = !saved;", "(void)0;"), "list_file"),
    "no-merge": (("\tfriends_merge(path);\n", ""), "list_safety"),
    "stale-tmp-kept": (("\tsnprintf(other, sizeof(other), \"%s.tmp\", path);\n\tremove(other);\n", ""), "list_safety"),
    "drops-unlogged": (("platform_log(\"friends: %d record%s dropped: %s\", total, total == 1 ? \"\" : \"s\", text);", "(void)0;"), "drops_logged"),
    "drops-logged-each-update": (("(long)time(NULL) - friends.drops_logged >= FRIEND_DROPS_LOG_SECONDS", "1"), "drops_logged"),
    "drops-lost-at-quit": (("\tp2p_signal_flush();\n\tfriends_drops_log();\n", "\tp2p_signal_flush();\n"), "drops_at_quit"),
    "one-broker-enough": (("friend_request_publish(friend) >= connected", "friend_request_publish(friend) >= 1"), "publish"),
    "topic-not-checked": (("topic)))\n\t\treturn FRIEND_DROP_TOPIC;", "topic)))\n\t\t(void)0;"), "topics"),
    "own-tag-looked-up": (("else if (!strcmp(tag, friends.tag))", "else if (0)"), "lookup"),
    "card-tag-unchecked": (("return FRIEND_DROP_TAG;", "(void)0;"), "lookup"),
    "card-topic-unchecked": (("topic))\n\t\treturn FRIEND_DROP_TOPIC;", "topic))\n\t\t(void)0;"), "lookup"),
    "card-name-unchecked": (("(kind == FRIEND_CARD && !friend_name_ok(record->body, record->body_size))", "0"), "lookup"),
    "two-players-add-the-first": (("friends.lookup_result = friends.look_count == 2 ? P2P_FRIEND_ADD_TWO_PLAYERS : friends.look_count ?",
                                   "friends.lookup_result = friends.look_count ?"), "lookup_two"),
    "card-not-renewed": (("friends.card_size = 0;\n\t\tfriends.card_due = 1;", "friends.card_size = 0;"), "card_publish"),
    "card-kept-a-week": (("FRIEND_CARD_SECONDS,\n\t\t&friends.card_taken", "FRIEND_REQUEST_SECONDS,\n\t\t&friends.card_taken"),
                         "card_publish"),
    "nameless-card": (("if (!own_name_ok())\n\t\t\treturn 0;\n\t\tfriends.card_size", "if (0)\n\t\t\treturn 0;\n\t\tfriends.card_size"),
                      "card_publish"),
    "lookup-ends-at-once": (("(unsigned int)(p2p_now() - friends.look_time) >= FRIEND_LOOKUP_SECONDS * 1000u", "1"), "lookup"),
    "lookup-result-unset": (("friends.lookup_result = P2P_FRIEND_ADD_LOOKING;\n", ""), "lookup"),
    "lookup-ignores-wildcards": (("!p2p_signal_wildcards() || !own_name_ok()", "!p2p_signal_connected() || !own_name_ok()"), "lookup"),
    "lookup-nameless": (("!p2p_signal_wildcards() || !own_name_ok()", "!p2p_signal_wildcards()"), "lookup"),
    "lookup-left-subscribed": (("friends.look_topic[0] = 0;\n\tfriends.topics_set = 0;", "friends.topics_set = 0;"), "lookup"),
    "names-unchecked": (("return FRIEND_DROP_NAME;", "(void)0;"), "names"),
    "name-from-the-party-record": (("friend_name_tag(record.sender, record.body + FRIEND_INBOX_BYTES, record.body_size - FRIEND_INBOX_BYTES,\n\t\t\tfriend->name_tag);",
                                    "(void)0;"), "party_add"),
    "bye-ignored": (("if (record->kind == FRIEND_BYE)", "if (0)"), "presence_timer"),
    "silence-never-offline": (("FRIEND_OFFLINE_SECONDS * 1000u", "FRIEND_OFFLINE_SECONDS * 1000000u"), "presence_timer"),
    "here-to-the-unheard": (("friends.list[index].heard && friends.list[index].presence != FRIEND_HELLO",
                             "friends.list[index].presence != FRIEND_HELLO"), "presence_timer"),
    "no-here-on-change": (("friends.state_changed || ", ""), "presence_timer"),
    "version-ignored": (("(record->body[1] << 8 | record->body[2]) == HALO_PORT_NETWORK_VERSION", "1"), "version"),
    "body-unchecked": (("return FRIEND_DROP_BODY;", "(void)0;"), "version"),
    "unsent-hello-dropped": (("!friend_presence_publish(&friends.list[index]))", "!(friend_presence_publish(&friends.list[index]) || 1))"), "hello_schedule"),
    "older-here-wins": (("if (friend && record.sequence <= friend->last_sequence)", "if (friend && kind != FRIEND_HERE && record.sequence <= friend->last_sequence)"),
                        "two_copies"),
    "here-unanswered": (("if ((record->kind == FRIEND_HELLO || !friend->heard) && friend->presence != FRIEND_HELLO)",
                         "if (record->kind == FRIEND_HELLO && friend->presence != FRIEND_HELLO)"), "here_recovery"),
    "timer-here-keeps-the-taken-mask": (("friend_presence_due(&friends.list[index], FRIEND_HERE);",
                                         "friends.list[index].presence = FRIEND_HERE;"), "presence_timer"),
    "no-hello-on-later-brokers": (("fresh = up & ~friends.up;", "fresh = friends.up ? 0 : up;"), "hello_later"),
    "bye-throttled": (("broker_publish_friend(&signalling.brokers[index], topic, payload, size, 0);",
                       "if (broker_may_publish(&signalling.brokers[index], UNPROVEN_RESERVE))\n\t\t\tbroker_publish_friend("
                       "&signalling.brokers[index], topic, payload, size, 0);"), "quit_byes", "signal"),
    "hello-only-on-a-rise": (("fresh = up & ~friends.up;", "fresh = connected > friends.connected ? up : 0;"), "broker_swap"),
    "bye-ahead-of-the-tombstone": (("\tpthread_mutex_lock(&p2p_lock);\n\tp2p_lobby_quit();",
                                    "\tp2p_friends_quit();\n\tpthread_mutex_lock(&p2p_lock);\n\tp2p_lobby_quit();"),
                                   "quit_order", "quit"),
    "bye-after-the-disconnect": (("p2p_friends_quit();", "(void)0;"), "quit_order", "quit"),
    "republish-to-taken": (("if (*took & 1u << index)", "if (0)"), "publish", "signal"),
    "hellos-take-the-party-tokens": (("broker_may_publish(broker, UNPROVEN_RESERVE)", "broker_may_publish(broker, 0)"),
                                     "party_first", "signal"),
    "bucket-never-spent": (("broker->publish_tokens--;", "(void)0;"), "hello_schedule", "signal"),
    "bucket-never-refills": (("int gained = (int)((now - broker->publish_time) / PUBLISH_INTERVAL);", "int gained = 0;"),
                             "hello_schedule", "signal"),
    "sent-not-marked": (("friend ? (friend->sent ? 2 : 1) : 0", "friend ? 1 : 0"), "party_add"),
    "invite-kept-off-matchmaking": (("MATCHMAKING)\n\t\t\t\treturn FRIEND_DROP_STATE;", "MATCHMAKING)\n\t\t\t\t(void)0;"), "invite"),
    "second-invite-replaces": (("return FRIEND_DROP_BUSY;", "(void)0;"), "invite"),
    "link-unchecked": (("(kind == FRIEND_INVITE && !friend_link_ok(record.body, record.body_size))", "0"), "invite_strangers"),
    "invite-to-any-state": (("friends.list[index].state == P2P_FRIEND_MATCHMAKING && friends.out_count", "friends.out_count"),
                            "invite"),
    "yes-ignores-the-party": (("can_join = pending && yes && p2p_party_can_join(link);", "can_join = pending && yes;"), "invite"),
    "no-joins": (("if (!pending || !yes)\n\t\treturn P2P_FRIEND_ANSWER_DONE;", "if (!pending)\n\t\treturn P2P_FRIEND_ANSWER_DONE;"),
                 "invite"),
    "answer-without-invite-refused": (("if (!pending || !yes)\n\t\treturn P2P_FRIEND_ANSWER_DONE;",
                                       "if (!pending)\n\t\treturn P2P_FRIEND_ANSWER_LEAVE_FIRST;\n\tif (!yes)\n\t\treturn P2P_FRIEND_ANSWER_DONE;"),
                                      "invite"),
    "failed-join-says-leave-first": (("? P2P_FRIEND_ANSWER_DONE : P2P_FRIEND_ANSWER_FAILED", "? P2P_FRIEND_ANSWER_DONE : P2P_FRIEND_ANSWER_LEAVE_FIRST"),
                                     "invite"),
    "can-join-read-unlocked": (("can_join = pending && yes && p2p_party_can_join(link);\n\tpthread_mutex_unlock(&p2p_lock);",
                                "pthread_mutex_unlock(&p2p_lock);\n\tcan_join = pending && yes && p2p_party_can_join(link);"), "invite"),
    "invite-on-the-clipboard": (("!p2p_party_invite_quiet(link, sizeof(link))", "!p2p_party_invite(link, sizeof(link))"), "invite"),
    "search-kept-for-yes": (("if (party.search_state != P2P_MM_OFF)\n\t\tp2p_mm_cancel();", "(void)0;"), "invite_searching"),
    "search-ended-for-no": (("if (!pending || !yes)\n\t\treturn P2P_FRIEND_ANSWER_DONE;",
                             "if (!pending)\n\t\treturn P2P_FRIEND_ANSWER_DONE;\n\tif (!yes)\n\t{\n\t\tp2p_mm_cancel();\n\t\treturn P2P_FRIEND_ANSWER_DONE;\n\t}"),
                            "invite_searching"),
    "search-ended-before-the-check": (("if (!can_join)\n\t\treturn P2P_FRIEND_ANSWER_LEAVE_FIRST;\n\tp2p_party_get_status(&party);",
                                       "p2p_mm_cancel();\n\tif (!can_join)\n\t\treturn P2P_FRIEND_ANSWER_LEAVE_FIRST;\n\tp2p_party_get_status(&party);"),
                                      "invite_searching"),
    "nameless-request-after-a-restart": ((" || (!friend->request_size && !own_name_ok()))", ")"), "request_restart"),
    "removal-forgotten-after-a-failed-save": (("if (friends_save(friends.list_path))\n\t\t\tfriends.removed_count = 0;",
                                               "friends_save(friends.list_path);\n\t\tfriends.removed_count = 0;"), "remove"),
    "invite-kept-after-leaving": (("		if (state != P2P_FRIEND_MATCHMAKING)\n			friends.invite_pending = 0;\n", ""), "invite"),
    "invite-kept-after-bye": (("\tfriend->state = P2P_FRIEND_OFFLINE;\n\tfriend_invite_drop(friend->key);\n}", "\tfriend->state = P2P_FRIEND_OFFLINE;\n}"), "invite"),
    "removed-friend-saved-back": (("if (friend_removed(saved[index].key))\n\t\t\tcontinue;", ""),
                                  "remove"),
    "only-the-last-removal-held": (("memcpy(friends.removed[friends.removed_count - 1]", "memcpy(friends.removed[0]"), "remove_twice"),
    "party-members-listed-twice": (("!friend->heard != !heard || member < party.count)", "!friend->heard != !heard)"), "online_rows"),
    "rows-unsorted": (("place > first && friend_name_before(", "place > first && 0 && friend_name_before("), "online_rows"),
    "action-by-position": (("if (!strcmp(friends.list[index].id, id))\n\t\t\treturn index;", "if (index == 1)\n\t\t\treturn index;"), "by_id"),
}


def generated(fault=None, target="friends"):
    signal = read("port/linux/src/p2p_signal.c")
    source = read("port/linux/src/p2p_friends.c")
    header = read("port/linux/src/p2p.h")
    internal = read("port/linux/src/p2p_internal.h")
    version = re.search(r"^#define HALO_PORT_NETWORK_VERSION .*$", read("port/linux/include/halo_port_limits.h"), re.M).group(0)
    prefix = re.search(r"^#define P2P_FRIEND_TOPIC_PREFIX .*$", internal, re.M).group(0)
    config = "\n".join([enum_with(header, "P2P_PARTY_NAME_SIZE"), enum_with(header, "P2P_FRIEND_TAG_SIZE"),
                        enum_with(header, "P2P_FRIEND_ADD_SENT"), enum_with(header, "P2P_FRIEND_OFFLINE"), enum_with(header, "P2P_MM_OFF"), enum_with(header, "P2P_FRIEND_ANSWER_DONE"), structure(header, "p2p_mm_choice"),
                        structure(header, "p2p_party_member"), structure(header, "p2p_party_status"),
                        structure(header, "p2p_friend_row"), structure(header, "p2p_friends_status"), prefix,
                        "#include <fcntl.h>", "#include <pthread.h>", "const char *p2p_party_name(void);",
                        "#define HALO_SERVER_BUILD 1", version, "unsigned long p2p_now(void);", "void p2p_signal_flush(void);", "unsigned p2p_signal_up(void);", "void p2p_lobby_quit(void);", "void p2p_signal_disconnect(void);", "int p2p_party_can_join(const char *text);", "int p2p_join_invite(const char *text);", "int p2p_party_invite_quiet(char *link, int size);", "void p2p_mm_cancel(void);", "int posix_server_rename(const char *from, const char *to);"])
    source = mutated(source, "static void friend_tag(const unsigned char *public_key, char *tag)\n{",
                     "static void friend_tag_real(const unsigned char *public_key, char *tag);\n"
                     "static void friend_tag(const unsigned char *public_key, char *tag)\n"
                     "{\n\tif (tag_forced)\n\t\tstrcpy(tag, tag_forced);\n\telse\n\t\tfriend_tag_real(public_key, tag);\n}\n\n"
                     "static void friend_tag_real(const unsigned char *public_key, char *tag)\n{")
    code = "\n".join([function(read("port/linux/src/p2p.c"), name) for name in ("p2p_hex", "hex_value", "p2p_unhex")] + [
                      re.sub(r"^#include .*$", "", source, flags=re.M)])
    throttle = "\n".join([f"#define {name} {constant(signal, name)}" for name in
                          ("PUBLISH_BURST", "PUBLISH_INTERVAL", "UNPROVEN_RESERVE", "TOPIC_SIZE", "MAXIMUM_LISTING_SIZE")] +
                         [enum_with(signal, "_broker_ready")] +
                         [function(signal, name) for name in ("broker_may_publish", "publish_everywhere",
                                                              "p2p_signal_friend_publish", "p2p_signal_friend_goodbye")])
    quit_code = function(read("port/linux/src/p2p.c"), "lobby_quit")
    if fault:
        if target == "signal":
            throttle = mutated(throttle, *fault)
        elif target == "quit":
            quit_code = mutated(quit_code, *fault)
        else:
            code = mutated(code, *fault)
    return crypto_includes() + (("config.inc", config), ("throttle.inc", throttle), ("quit.inc", quit_code),
                                ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("friends", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case, *target = NEGATIVE_CONTROLS[control]
    status, output = run(build("friends", generated(fault, *target)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def update_order(text):
    """The pass calls the matchmaking and party updates before the friends' (the HELLOs wait for what is left)."""
    return text.index("p2p_mm_update(") < text.index("p2p_party_update();") < text.index("p2p_friends_update();")


def test_party_and_search_records_go_before_hellos():
    assert update_order(read("port/linux/src/p2p.c"))


def test_negative_control_hello_ahead_of_the_party_record():
    text = mutated(read("port/linux/src/p2p.c"), "p2p_party_update();\n\t\tif (p2p_friends_wanted())\n\t\t\tp2p_signal_start();\n\t\tp2p_friends_update();",
                   "p2p_friends_update();\n\t\tp2p_party_update();")
    assert not update_order(text), "the test cannot see the HELLOs sent ahead of the party record"


def card_zero(text):
    return "zeros[P2P_KEY_SIZE]" in text


def test_card_is_signed_over_a_whole_zero_key():
    assert card_zero(read("port/linux/src/p2p_friends.c")), "friend_record_write reads 32 bytes of the zero it is given"


def test_negative_control_card_zero_too_short():
    text = mutated(read("port/linux/src/p2p_friends.c"), "static const unsigned char zeros[P2P_KEY_SIZE];", "static const unsigned char zeros[FRIEND_NONCE_SIZE];")
    assert not card_zero(text), "the test cannot see a zero shorter than the key"
