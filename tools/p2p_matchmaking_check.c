/* P2P_MATCHMAKING_CHECK.C: matchmaking's records, and machines searching through a stand-in broker */

#include "../port/linux/src/p2p_matchmaking.h"
#include "halo_port_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEXT_OF_(x) #x
#define TEXT_OF(x) TEXT_OF_(x)
#define HALO_PORT_NETWORK_VERSION_TEXT TEXT_OF(HALO_PORT_NETWORK_VERSION)

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long clock_now = 1800000000UL;

void posix_random_bytes(void *buffer, unsigned long size)
{
	static unsigned char next = 0x5a;

	memset(buffer, next++, size);
}
unsigned long p2p_now(void) { return clock_now; }
int config_boolean(const char *name) { (void)name; return 1; }
void platform_log(const char *format, ...) { (void)format; }

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + index * 2, "%02x", bytes[index]);
}

static int lost_in_verify;

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
	/* mm_record_read's last call: p2p_mm_verify_pass holds no lock here, as when the game's thread runs */
	if (lost_in_verify)
	{
		lost_in_verify = 0;
		clock_now += 100;
		p2p_mm_game_lost();
	}
}

static unsigned char run_seed[P2P_SEED_SIZE] = { 0x77, 0x13 }, run_key[P2P_KEY_SIZE];

const unsigned char *p2p_signing_seed(void) { return run_seed; }
const unsigned char *p2p_signing_key(void)
{
	p2p_ed25519_public(run_seed, run_key, NULL);
	return run_key;
}
void p2p_signal_mm_topics(const char topics[][P2P_MM_TOPIC_SIZE], int count) { (void)topics; (void)count; }

static char signal_topic[P2P_PARTY_TOPIC_SIZE], publish_topic[P2P_PARTY_TOPIC_SIZE];
static char clipboard[P2P_PARTY_LINK_SIZE], offered_link[P2P_PARTY_LINK_SIZE];
static unsigned char party_published[MM_MAXIMUM_RECORD_SIZE];
static int party_published_size, offered_count;
static char resent_topic[P2P_PARTY_TOPIC_SIZE];
static unsigned char resent[MM_MAXIMUM_RECORD_SIZE];
static int resent_size;

void p2p_signal_party_topic(const char *topic) { strcpy(signal_topic, topic); }
void p2p_signal_party_publish(const unsigned char *payload, int size)
{
	strcpy(publish_topic, signal_topic);
	memcpy(party_published, payload, (size_t)size);
	party_published_size = size;
}
void p2p_signal_party_publish_on(const char *topic, const unsigned char *payload, int size)
{
	strcpy(resent_topic, topic);
	memcpy(resent, payload, (size_t)size);
	resent_size = size;
}
void p2p_clipboard_offer(const char *text) { strcpy(clipboard, text); }
void p2p_discord_set_party(const char *link, int count, int maximum)
{
	(void)maximum;
	strcpy(offered_link, link);
	offered_count = count;
}

static struct p2p_listing listings[8];
static int listing_count, browsing, browse_calls;

int lobby_games_unlocked(struct p2p_listing *games, int maximum_count)
{
	int count = listing_count < maximum_count ? listing_count : maximum_count;

	memcpy(games, listings, (size_t)count * sizeof(*games));
	return count;
}
static int masters_heard = 1;
int p2p_lobby_masters_were_heard(void) { return masters_heard; }
static int masters_forgotten, masters_refreshed;
void p2p_lobby_masters_forget(void) { masters_forgotten++; }
void master_list_refresh(void) { masters_refreshed++; }
static unsigned char marked[P2P_IDENTIFIER_SIZE];
static int marked_count;

void lobby_mark_failed_unlocked(const unsigned char *identifier)
{
	int index;

	memcpy(marked, identifier, sizeof(marked));
	marked_count++;
	for (index = 0; index < listing_count; index++)
		if (!memcmp(listings[index].identifier, identifier, P2P_IDENTIFIER_SIZE))
			listings[index].failed = 1;
}
/* lobby_pick.inc: written by tools/test_linux_port.py from p2p_lobby.c */
#include "lobby_pick.inc"

static int verifies;
static void (*verify_window)(void);

int __real_p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature);

int __wrap_p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature)
{
	verifies++;
	if (verify_window)
		verify_window();
	return __real_p2p_ed25519_verify(public_key, message, size, signature);
}
int p2p_lobby_browsing(void) { return browsing; }
void p2p_lobby_browse(int on)
{
	browsing = on;
	browse_calls++;
}

static unsigned char published[MM_MAXIMUM_RECORD_SIZE];
static int published_size;

void p2p_signal_mm_publish(const char *topic, const unsigned char *payload, int size)
{
	(void)topic;
	memcpy(published, payload, (size_t)size);
	published_size = size;
}

static int failures;

#define CHECK(condition, ...) do { if (!(condition)) { failures++; printf("FAIL %s: ", case_name); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *case_name;

static void seed_of(int machine, unsigned char *seed)
{
	memset(seed, 0, P2P_SEED_SIZE);
	seed[0] = (unsigned char)(machine + 1);
	seed[1] = (unsigned char)((machine + 1) >> 8);
}

static struct mm_record searching(int machine, unsigned long sequence)
{
	struct mm_record record;
	unsigned char seed[P2P_SEED_SIZE];

	memset(&record, 0, sizeof(record));
	seed_of(machine, seed);
	p2p_ed25519_public(seed, record.key, NULL);
	record.kind = MM_KIND_SEARCHING;
	record.network_version = HALO_PORT_NETWORK_VERSION;
	record.sequence = sequence;
	record.interval = MM_INTERVAL;
	record.can_host = 1;
	record.playlist = P2P_MM_SLAYER;
	record.size = P2P_MM_2V2;
	return record;
}

static void check_records(void)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE], message[MM_MAXIMUM_RECORD_SIZE + 9];
	struct mm_record record = searching(0, 7), read_back;
	int size;

	case_name = "records";
	seed_of(0, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	CHECK(size == MM_HEADER_SIZE + P2P_SIGNATURE_SIZE, "a searching record is %d bytes", size);
	CHECK(mm_record_read(bytes, size, &read_back), "read back");
	CHECK(read_back.sequence == 7 && read_back.can_host &&
		read_back.playlist == P2P_MM_SLAYER && read_back.size == P2P_MM_2V2 &&
		read_back.network_version == HALO_PORT_NETWORK_VERSION, "fields kept");
	bytes[30] ^= 1;
	CHECK(!mm_record_read(bytes, size, &read_back), "E14: a tampered record is refused");
	bytes[30] ^= 1;
	CHECK(!mm_record_read(bytes, size - 1, &read_back), "cut short");
	/* (signed again, so that only the format is wrong) */
	bytes[2] = MM_FORMAT_PARTY + 1;
	memcpy(message, "hceu-mm-1", 9);
	memcpy(message + 9, bytes, (size_t)(size - P2P_SIGNATURE_SIZE));
	p2p_ed25519_sign(seed, record.key, message, 9 + size - P2P_SIGNATURE_SIZE, bytes + size - P2P_SIGNATURE_SIZE);
	CHECK(!mm_record_read(bytes, size, &read_back), "another format is refused");
	record = searching(1, 7);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	CHECK(!mm_record_read(bytes, size, &read_back), "signed by a key not its own");
}

static void check_playlists(void)
{
	case_name = "playlists";
	CHECK(!mm_playlist_has_size(1u << P2P_MM_CTF, P2P_MM_FFA_6), "CTF has no free-for-all");
	CHECK(mm_playlist_has_size(1u << P2P_MM_CTF, P2P_MM_4V4) && mm_playlist_has_size(1u << P2P_MM_SLAYER, P2P_MM_FFA_6),
		"every other pair is a queue");
}

static void check_topics(void)
{
	char topic[P2P_MM_TOPIC_SIZE];

	case_name = "topics";
	mm_topic(P2P_MM_SLAYER, P2P_MM_2V2, topic, sizeof(topic));
	CHECK(!strcmp(topic, "hceu/3/mm/" HALO_PORT_NETWORK_VERSION_TEXT "/slayer/2v2"), "E16: %s", topic);
	mm_topic(P2P_MM_RACE, P2P_MM_FFA_6, topic, sizeof(topic));
	CHECK(!strcmp(topic, "hceu/3/mm/" HALO_PORT_NETWORK_VERSION_TEXT "/race/ffa6"), "%s", topic);
	CHECK(mm_interval(1) == 30 && mm_interval(12) == 30, "up to 12 queues: 3 s");
	CHECK(mm_interval(25) >= 62 && mm_interval(25) <= 63, "E44: 25 queues: about 6.3 s: %d", mm_interval(25));
	CHECK(mm_interval(25) * MM_RECORDS_PER_SECOND >= 25 * 10, "E44: at most 4 a second");
}

static void check_party_topic(void)
{
	unsigned char secret[P2P_PARTY_SECRET_SIZE];
	char topic[P2P_PARTY_TOPIC_SIZE];
	int index;

	case_name = "party topic";
	for (index = 0; index < P2P_PARTY_SECRET_SIZE; index++)
		secret[index] = (unsigned char)index;
	mm_party_topic(secret, topic, sizeof(topic));
	CHECK(!strcmp(topic, "hceu/3/party/" HALO_PORT_NETWORK_VERSION_TEXT "/be45cb2605bf36bebde684841a28f0fd"), "%s", topic);
}

static void check_topic_moves(void)
{
	static const char *const had[3] = { "A", "B", "C" };
	static const char *const wanted[3] = { "B", "C", "" };
	int index, unsubscribed_wanted = 0, unsubscribed = 0;

	case_name = "topic moves";
	for (index = 0; index < 3; index++)
	{
		if (had[index][0] && !mm_topic_listed(wanted, 3, had[index]))
		{
			unsubscribed++;
			unsubscribed_wanted += !strcmp(had[index], "B") || !strcmp(had[index], "C");
		}
	}
	CHECK(unsubscribed == 1 && !unsubscribed_wanted, "[A,B,C] to [B,C]: only A is unsubscribed: %d", unsubscribed);
}

static struct mm_record party_of(int leader, int members, unsigned long sequence);

static void verify_budget_of(int members, int queue_size, double limit_ms)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, queue_size };
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	struct p2p_mm_status status;
	struct timespec before, after;
	int index, size, passes, left;
	double first_pass_ms;

	p2p_mm_search(&choice);
	/* MAXIMUM_QUEUED (64) are kept: 21 keys of three records, and one more */
	for (index = 0; index < 300; index++)
	{
		struct mm_record record = party_of(index / 3 + 10, members, (unsigned long)(index % 3 + 1));

		record.size = queue_size;
		seed_of(index / 3 + 10, seed);
		size = mm_record_write(&record, seed, bytes, sizeof(bytes));
		pthread_mutex_lock(&p2p_lock);
		p2p_mm_heard(bytes, size);
		pthread_mutex_unlock(&p2p_lock);
	}
	pthread_mutex_lock(&p2p_lock);
	clock_gettime(CLOCK_MONOTONIC, &before);
	left = p2p_mm_verify_pass(clock_now);
	clock_gettime(CLOCK_MONOTONIC, &after);
	first_pass_ms = (double)(after.tv_sec - before.tv_sec) * 1000.0 + (double)(after.tv_nsec - before.tv_nsec) / 1e6;
	for (passes = 1; left && passes < 100; passes++)
		left = p2p_mm_verify_pass(clock_now);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(first_pass_ms < limit_ms, "one pass took %.1f ms", first_pass_ms);
	CHECK(passes > 1 || first_pass_ms < 2.5, "a pass of %.1f ms past the budget stopped early: %d passes", first_pass_ms, passes);
	CHECK(!left, "the queue drains over later passes: %d left", left);
	p2p_mm_get_status(&status);
	CHECK(status.count == 1 + 21 * (members + 1), "64 kept, all verified into the search: %d searchers counted, 21 and this machine",
		status.count);
	p2p_mm_cancel();
}

static void check_verify_budget(void)
{
	case_name = "verify_budget";
	verify_budget_of(0, P2P_MM_2V2, 4.0);
	case_name = "verify_budget_party";
	verify_budget_of(3, P2P_MM_4V4, 8.0);
}

static void check_run_key(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_record record;

	case_name = "run_key";
	published_size = 0;
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(published_size && mm_record_read(published, published_size, &record), "a searching record is published");
	CHECK(published_size && !memcmp(record.key, p2p_signing_key(), P2P_KEY_SIZE),
		"the search is the run's identity: its key is the signing key");
	p2p_mm_cancel();
}

static void add_listing(const char *name, int engine, int players, int most, int ping);

static void check_big_listing(void)
{
	struct p2p_mm_choice big = { 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM }, small = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct p2p_mm_decision decision;
	struct p2p_mm_status status;
	unsigned char token[P2P_TOKEN_SIZE] = { 1 };

	case_name = "big_listing";
	listing_count = 0;
	masters_heard = 1;
	p2p_mm_search(&big);
	CHECK(!p2p_mm_hosts_big_game(), "a search hosts nothing yet");
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_HOST && p2p_mm_hosts_big_game(),
		"a big host says so from its first listing");
	p2p_mm_host_gave_up();
	add_listing("other", 2, 2, 16, 70);
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN && !p2p_mm_hosts_big_game(),
		"a big host gone back to the queue that joins a game hosts none");
	listing_count = 0;
	p2p_mm_cancel();
	p2p_mm_search(&big);
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_HOST && p2p_mm_hosts_big_game(),
		"a big host that searches again hosts again");
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_update(clock_now, 1, token);
	pthread_mutex_unlock(&p2p_lock);
	p2p_mm_game(1, 128, 0);
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_HOSTING && p2p_mm_hosts_big_game(), "a big host still says so once it hosts");
	CHECK(browsing, "a lone big host keeps the browser open for the pings it merges by");
	p2p_mm_game(2, 128, 0);
	CHECK(!browsing, "a big host with a player leaves the browser as it was");
	p2p_mm_cancel();
	CHECK(!p2p_mm_hosts_big_game(), "a search cancelled hosts no big game");
	p2p_mm_search(&small);
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	p2p_mm_host_now();
	pthread_mutex_lock(&p2p_lock);
	clock_now += 100;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_HOST && !p2p_mm_hosts_big_game(),
		"a small host's game is no big game");
	p2p_mm_cancel();
}

static void check_joined_then_lost(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_record record = searching(20, 1);
	struct p2p_mm_decision decision;
	struct p2p_mm_status status;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	case_name = "joined_then_lost";
	record.kind = MM_KIND_HOSTING;
	record.players = 1;
	record.most_players = 4;
	seed_of(20, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_heard(bytes, size);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN, "a hosted game is joined");
	p2p_mm_joined(0);
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_QUEUED && status.notice == P2P_MM_NOTICE_HOST_UNREACHED, "a failed join requeues: %d", status.state);
	p2p_mm_game_lost();
	p2p_mm_cancel();
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	record.sequence = 2;
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	p2p_mm_heard(bytes, size);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN, "joined again");
	p2p_mm_joined(1);
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_OFF, "joined: the search is over");
	published_size = 0;
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(published_size && mm_record_read(published, published_size, &record) && record.kind == MM_KIND_CANCEL &&
		record.playlist == P2P_MM_SLAYER && record.size == P2P_MM_2V2, "joined: the queue it joined in is cancelled");
	CHECK(!browsing, "joined: the browser is left as it was");
	p2p_mm_game_lost();
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_LOOKING && status.notice == P2P_MM_NOTICE_HOST_LEFT,
		"E11: the host left a joined game: searching again, told: %d", status.state);
	CHECK(browsing, "the restarted search holds the server browser open");
	p2p_mm_cancel();
	CHECK(!browsing, "the end of the search leaves the browser as it was");
}

static void check_lost_in_verify(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_record record = searching(20, 1);
	struct p2p_mm_decision decision;
	struct p2p_mm_status status;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	case_name = "lost_in_verify";
	seed_of(20, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_heard(bytes, size);
	lost_in_verify = 1;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(!lost_in_verify, "the game is lost during the verify pass");
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_LOOKING, "the restarted search looks for its full time: %d", status.state);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_NONE, "the restarted search decides nothing yet");
	p2p_mm_cancel();
}

static void add_listing(const char *name, int engine, int players, int most, int ping)
{
	struct p2p_listing *game = &listings[listing_count++];

	memset(game, 0, sizeof(*game));
	snprintf(game->name, sizeof(game->name), "%s", name);
	snprintf(game->invite, sizeof(game->invite), "halo://join/%s", name);
	game->identifier[0] = (unsigned char)listing_count;
	game->engine_type = (unsigned char)engine;
	game->player_count = (unsigned char)players;
	game->maximum_player_count = (unsigned char)most;
	game->has_teams = 1;
	game->open = 1;
	game->ping = (short)ping;
	game->network_version = HALO_PORT_NETWORK_VERSION;
}

static void look_start(struct mm_search *search, unsigned playlists, int size, unsigned long now)
{
	struct p2p_mm_choice choice = { playlists, size };
	unsigned char seed[P2P_SEED_SIZE];

	listing_count = 0;
	seed_of(30, seed);
	mm_start(search, seed, &choice, now, 1800000000UL);
}

static void check_look(void)
{
	static struct mm_search search;
	struct mm_out out[MM_MAXIMUM_OUT];
	struct mm_record record;
	int count, queues;

	case_name = "look";
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1000);
	CHECK(search.state == P2P_MM_LOOKING, "a search starts by looking");
	add_listing("game", 2, 2, 4, -1);
	count = mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.state == P2P_MM_LOOKING && !count && search.decision.kind == P2P_MM_DECIDE_NONE,
		"a fitting game with no ping holds the look: %d", search.state);
	CHECK(search.games_checked == 1, "the games examined are counted: %d", search.games_checked);
	listings[0].ping = 40;
	mm_step(&search, 3100, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && !strcmp(search.decision.invite, "halo://join/game") &&
		search.state == P2P_MM_FOUND, "a timed fitting game is joined: %d", search.decision.kind);
	CHECK(search.decision.playlist == P2P_MM_SLAYER && search.decision.size == P2P_MM_2V2, "its playlist and size");
	CHECK(search.decision.public_game, "a public game's join says so");

	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1000);
	add_listing("ctf", 1, 2, 4, 40);
	count = mm_step(&search, 6900, out, MM_MAXIMUM_OUT);
	CHECK(search.state == P2P_MM_LOOKING && !count, "before 6 s nothing fitting keeps the look");
	count = mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(search.state == P2P_MM_QUEUED && count == 1 && mm_record_read(out[0].bytes, out[0].size, &record) &&
		record.kind == MM_KIND_SEARCHING, "at 6 s the search queues and its record goes out at once: %d", count);

	look_start(&search, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_2V2, 1000);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	queues = search.queue_count;
	add_listing("late", 1, 3, 4, 50);
	count = mm_step(&search, 8000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && search.decision.playlist == P2P_MM_CTF &&
		!strcmp(search.decision.invite, "halo://join/late"), "E20: a game that appears while queued is joined");
	CHECK(queues == 2 && count == queues, "E20: a cancel for each queue: %d of %d", count, queues);
	for (queues = 0; queues < count; queues++)
		CHECK(mm_record_read(out[queues].bytes, out[queues].size, &record) && record.kind == MM_KIND_CANCEL,
			"E20: output %d is a cancel", queues);
	listing_count = 0;

	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1000);
	add_listing("quick", 2, 2, 4, 40);
	add_listing("unpinged", 2, 3, 4, -1);
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.state == P2P_MM_LOOKING && search.decision.kind == P2P_MM_DECIDE_NONE,
		"a pinged fit waits while another fit is unpinged: %d", search.state);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && !strcmp(search.decision.invite, "halo://join/quick"),
		"at 6 s the timed fit is taken");
	listing_count = 0;
}

static void check_big_look(void)
{
	static struct mm_search search;
	struct mm_out out[MM_MAXIMUM_OUT];

	case_name = "big_look";
	masters_heard = 0;
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("players", 2, 10, 16, 70);
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.state == P2P_MM_LOOKING && search.decision.kind == P2P_MM_DECIDE_NONE,
		"a big search waits for the master servers: %d", search.state);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && !strcmp(search.decision.invite, "halo://join/players") &&
		search.decision.size == P2P_MM_BIG_TEAM, "E54: at 6 s the player-hosted game is taken without them");
	CHECK(!search.joining_region[0], "a player's game has no region: %s", search.joining_region);
	masters_heard = 1;
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("players", 2, 10, 16, 70);
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && search.decision.public_game,
		"the masters having answered, a big search picks at once, and an unflagged game is public");
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("server", 2, 9, 16, -1);
	listings[0].dedicated = listings[0].master = 1;
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_NONE, "E53: with no region a dedicated server waits out the look");
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN, "E53: at 6 s the dedicated server is taken");
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("big", 2, 2, 16, 70);
	listings[0].big_game = 1;
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && !search.decision.public_game,
		"a flagged listing is a big game");
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("server", 2, 8, 16, -1);
	listings[0].dedicated = listings[0].master = 1;
	strcpy(listings[0].region, "EU");
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(!strcmp(search.joining_region, "EU"), "a dedicated server's region reaches the screens: %s",
		search.joining_region);
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("swat", 2, 12, 16, -1);
	add_listing("plain", 2, 8, 16, -1);
	listings[0].dedicated = listings[0].master = listings[1].dedicated = listings[1].master = 1;
	listings[0].kind = 1;
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_JOIN && !strcmp(search.decision.invite, "halo://join/plain"),
		"a big search passes over a dedicated server of another game kind: %s", search.decision.invite);
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1000);
	add_listing("listed player", 2, 8, 16, 70);
	listings[0].master = 1;
	strcpy(listings[0].region, "EU");
	mm_step(&search, 3000, out, MM_MAXIMUM_OUT);
	CHECK(!search.joining_region[0], "a player's game on a master server has no region: %s", search.joining_region);
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1000);
	mm_widen_now(&search);
	add_listing("large", 2, 46, 128, 40);
	mm_step(&search, 7100, out, MM_MAXIMUM_OUT);
	CHECK(search.decision.kind == P2P_MM_DECIDE_NONE, "E34: a widened search never takes a big game");
	listing_count = 0;
}

static void check_widened_pick(void)
{
	static struct mm_search search;
	struct mm_out out[MM_MAXIMUM_OUT];

	case_name = "widened_pick";
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1000);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	mm_widen_now(&search);
	add_listing("one", 2, 1, 2, 40);
	add_listing("four", 2, 7, 8, 40);
	mm_step(&search, 7100, out, MM_MAXIMUM_OUT);
	CHECK(!strcmp(search.decision.invite, "halo://join/four"), "a widened search picks the chosen size first: %s",
		search.decision.invite);
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1000);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	mm_widen_now(&search);
	add_listing("three", 2, 5, 6, 40);
	add_listing("four", 2, 1, 8, 90);
	mm_step(&search, 7100, out, MM_MAXIMUM_OUT);
	CHECK(!strcmp(search.decision.invite, "halo://join/four"), "even with fewer players: %s", search.decision.invite);
	look_start(&search, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1000);
	mm_step(&search, 7000, out, MM_MAXIMUM_OUT);
	mm_widen_now(&search);
	add_listing("one", 2, 1, 2, 40);
	add_listing("three", 2, 5, 6, 90);
	mm_step(&search, 7100, out, MM_MAXIMUM_OUT);
	CHECK(!strcmp(search.decision.invite, "halo://join/three"), "no fit at the chosen size: the most players: %s",
		search.decision.invite);
	listing_count = 0;
}

static void check_failed_pick(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct p2p_mm_decision decision;

	case_name = "step1_failed";
	listing_count = marked_count = 0;
	add_listing("host", 2, 2, 4, 40);
	p2p_mm_search(&choice);
	CHECK(browsing, "a search holds the server browser open");
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN && !strcmp(decision.invite, "halo://join/host"),
		"the game is joined");
	p2p_mm_joined(0);
	CHECK(marked_count == 1 && !memcmp(marked, listings[0].identifier, P2P_IDENTIFIER_SIZE),
		"a join that fails marks the listing failed: %d", marked_count);
	pthread_mutex_lock(&p2p_lock);
	clock_now += 100;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_NONE, "the failed game is passed over");
	p2p_mm_cancel();
	CHECK(!browsing, "the end of the search leaves the browser as it was");

	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_NONE, "the failed game stays passed over");
	p2p_mm_host_now();
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(browsing, "the browser stays open until the decision is taken");
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_HOST, "HOST NOW opens a game");
	CHECK(!browsing, "a HOST decision leaves the browser as it was");
	p2p_mm_cancel();
	listing_count = 0;
}

static void check_ffa_wanted(void)
{
	static struct mm_search search;
	struct p2p_lobby_wanted wanted;

	case_name = "e63_ffa_wanted";
	look_start(&search, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_FFA_6, 1000);
	mm_wanted(&search, 0, &wanted);
	CHECK(wanted.engines == 1u << 2 && wanted.players == 6 && !wanted.teams,
		"E63: an FFA 6 search wants Slayer alone, 6 players, no teams: %u %d %d", wanted.engines, wanted.players,
		wanted.teams);
	look_start(&search, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_2V2, 1000);
	mm_wanted(&search, 0, &wanted);
	CHECK(wanted.engines == (1u << 2 | 1u << 1) && wanted.players == 4 && wanted.teams,
		"a 2V2 search wants both: %u %d %d", wanted.engines, wanted.players, wanted.teams);
}

static int contains(const unsigned char *bytes, int size, const unsigned char *part, int part_size)
{
	int at;

	for (at = 0; at + part_size <= size; at++)
		if (!memcmp(bytes + at, part, (size_t)part_size))
			return 1;
	return 0;
}

static void check_hosting(void)
{
	unsigned char host_seed[P2P_SEED_SIZE], member_seed[P2P_SEED_SIZE], other_seed[P2P_SEED_SIZE];
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE], token[P2P_TOKEN_SIZE], opened[P2P_TOKEN_SIZE];
	struct mm_record record = searching(0, 1), member = searching(1, 1), read_back;
	int size;

	case_name = "hosting";
	seed_of(0, host_seed);
	seed_of(1, member_seed);
	seed_of(2, other_seed);
	memset(token, 0xa5, sizeof(token));
	size = mm_record_write(&member, member_seed, bytes, sizeof(bytes));
	CHECK(mm_record_read(bytes, size, &member), "the member's own record");
	record.kind = MM_KIND_HOSTING;
	record.players = 1;
	record.most_players = 4;
	record.held = 1;
	record.pool_count = 1;
	memcpy(record.pool[0], member.key_hash, P2P_KEY_HASH_SIZE);
	memcpy(record.token, token, P2P_TOKEN_SIZE);
	CHECK(mm_seal_token(host_seed, member.key, token, record.sealed[0]), "sealed");
	size = mm_record_write(&record, host_seed, bytes, sizeof(bytes));
	CHECK(mm_record_read(bytes, size, &read_back), "a held hosting record read back");
	CHECK(read_back.held && read_back.pool_count == 1 && read_back.most_players == 4, "held, one member, 4 most");
	CHECK(mm_open_token(member_seed, read_back.key, read_back.sealed[0], opened) &&
		!memcmp(opened, token, P2P_TOKEN_SIZE), "the member opens the token");
	CHECK(!mm_open_token(other_seed, read_back.key, read_back.sealed[0], opened), "no one else opens it");
	CHECK(!contains(bytes, size, token, P2P_TOKEN_SIZE), "a held game's token is nowhere in clear");
}

static struct mm_unit unit(int machine, int can_host)
{
	struct mm_unit result;
	unsigned char seed[P2P_SEED_SIZE], x25519[P2P_KEY_SIZE];

	memset(&result, 0, sizeof(result));
	seed_of(machine, seed);
	p2p_ed25519_public(seed, result.key, NULL);
	p2p_ed25519_to_x25519(result.key, x25519);
	p2p_key_hash(x25519, result.key_hash);
	result.can_host = can_host;
	result.players = 1;
	return result;
}

static int hash_rank(const struct mm_unit *units, int count, int index)
{
	int other, rank = 0;

	for (other = 0; other < count; other++)
		rank += memcmp(units[other].key_hash, units[index].key_hash, P2P_KEY_HASH_SIZE) < 0;
	return rank;
}

static void check_pool(void)
{
	static struct mm_unit every[MM_MAXIMUM_UNITS + 1];
	struct mm_unit units[8], shuffled[8];
	int pool[8], again[8], count, index, host, host_again, last = 0;

	case_name = "pool";
	for (index = 0; index < 6; index++)
		units[index] = unit(index, 1);
	count = mm_pool(units, 6, 4, 1, pool);
	CHECK(count == 4, "E4: a pool of 4 from 6: %d", count);
	for (index = 0; index < count; index++)
		CHECK(hash_rank(units, 6, pool[index]) == index, "E4: the 4 lowest key hashes, in order: %d at %d",
			pool[index], index);
	for (index = 0; index < 6; index++)
		shuffled[index] = units[5 - index];
	CHECK(mm_pool(shuffled, 6, 4, 1, again) == 4, "shuffled: a pool");
	for (index = 0; index < 4; index++)
		CHECK(!memcmp(units[pool[index]].key, shuffled[again[index]].key, P2P_KEY_SIZE), "shuffled: the same pool at %d", index);
	host = mm_elect(units, pool, count);
	host_again = mm_elect(shuffled, again, 4);
	CHECK(host >= 0 && host_again >= 0 && !memcmp(units[host].key, shuffled[host_again].key, P2P_KEY_SIZE),
		"E4: the same host on every machine");
	CHECK(mm_pool(units, 3, 4, 1, pool) == 0, "3 of 4: no pool");
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
		every[index].failed = every[index].players = 1;
	every[MM_MAXIMUM_UNITS].can_host = every[MM_MAXIMUM_UNITS].players = 1;
	CHECK(mm_pool(every, MM_MAXIMUM_UNITS + 1, 1, 0, pool) == 1 && pool[0] == MM_MAXIMUM_UNITS,
		"this machine and a full peers table: every unit is walked");
	/* four strict, then one that can host with the highest key hash: it can take only the last place */
	for (index = 0; index < 5; index++)
		units[index] = unit(index, 0);
	for (index = 0; index < 5; index++)
		if (hash_rank(units, 5, index) == 4)
			last = index;
	units[last].can_host = 1;
	CHECK(mm_pool(units, 5, 4, 1, pool) == 4 && pool[3] == last && mm_elect(units, pool, 4) == last,
		"E38: the host takes the last place and hosts");
	units[last].can_host = 0;
	CHECK(mm_pool(units, 5, 4, 1, pool) == 0, "E38: five strict routers form no pool");
	units[last].can_host = 1;
	units[last].failed = 1;
	CHECK(mm_pool(units, 5, 4, 1, pool) == 0, "E12: the only host failed: no pool until another arrives");
}

static void check_election(void)
{
	struct mm_unit units[4];
	int pool[4] = { 0, 1, 2, 3 }, index, host, winners = 0;

	case_name = "election";
	for (index = 0; index < 3; index++)
		units[index] = unit(index, 1);
	for (index = 0; index < 40; index++)
	{
		units[3] = unit(100 + index, 1);
		host = mm_elect(units, pool, 4);
		if (host >= 0 && host < 3)
			winners |= 1 << host;
	}
	CHECK((winners & (winners - 1)) != 0, "E14: one of the three always wins when it can: %d", winners);
}

struct machine
{
	struct mm_search search;
	int in, hosting, players, most, quit, ping;
};

static struct machine machines[8];
static int machine_count;

int lobby_ping_unlocked(const unsigned char *key_hash)
{
	int index;

	for (index = 0; index < machine_count; index++)
		if (!memcmp(machines[index].search.key_hash, key_hash, P2P_KEY_HASH_SIZE))
			return machines[index].ping;
	return 0;
}

/* the first of machines that are in the party of parties[] */
static int party_machines;
static void party_machine_step(int index);
static unsigned long now_ms, now_unix;
static int drop_percent;
static int sweep_seed = 1, sweep_drop;
static int cancel_on_join, cancel_sent, never_joins = -1;
static int fail_join = -1, hosted_while_joining, joins_taken;
static int ignore_first_host, ignore_only = -1, ignored = -1, fail_first_host, failed_host = -1;
static int capture_from = -1, captured_count;
static struct mm_out captured[256], quit_out[MM_MAXIMUM_OUT];
static int published_kinds[MM_KIND_CANCEL + 1];

struct in_flight
{
	int to;
	unsigned long at;
	struct mm_out out;
};
static struct in_flight flight[1024];
static int flight_count;

static int subscribed(const struct mm_search *search, const char *topic)
{
	char own[P2P_MM_TOPIC_SIZE];
	int index;

	for (index = 0; search->state != P2P_MM_OFF && index < search->queue_count; index++)
	{
		mm_topic(search->queues[index][0], search->queues[index][1], own, sizeof(own));
		if (!strcmp(own, topic))
			return 1;
	}
	return 0;
}

static void publish(int from, const struct mm_out *out, int count)
{
	int index, to;

	for (index = 0; index < count; index++)
	{
		if (from == capture_from && captured_count < 256)
			captured[captured_count++] = out[index];
		published_kinds[out[index].bytes[5]]++;
		for (to = 0; to < machine_count; to++)
		{
			if (to == from || !subscribed(&machines[to].search, out[index].topic) || rand() % 100 < drop_percent ||
				flight_count == 1024)
			{
				continue;
			}
			flight[flight_count].to = to;
			flight[flight_count].at = now_ms + 100;
			flight[flight_count++].out = out[index];
		}
	}
}

static void deliver(void)
{
	struct mm_record record;
	int index = 0;

	while (index < flight_count)
	{
		if (flight[index].at > now_ms)
		{
			index++;
			continue;
		}
		if (!machines[flight[index].to].quit &&
			mm_record_read(flight[index].out.bytes, flight[index].out.size, &record))
		{
			mm_heard(&machines[flight[index].to].search, &record, now_ms);
		}
		flight[index] = flight[--flight_count];
	}
}

static void replay_captured(int to)
{
	int index;

	for (index = 0; index < captured_count && flight_count < 1024; index++)
	{
		flight[flight_count].to = to;
		flight[flight_count].at = now_ms + 100;
		flight[flight_count++].out = captured[index];
	}
}

static int machine_of_invite(const char *invite)
{
	char text[2 * P2P_KEY_HASH_SIZE + 1];
	int index;

	for (index = 0; index < machine_count; index++)
	{
		p2p_hex(machines[index].search.key_hash, P2P_KEY_HASH_SIZE, text);
		if (!strncmp(invite + strlen("halo://join/"), text, strlen(text)))
			return index;
	}
	return -1;
}

static void game_side(int index)
{
	struct machine *machine = &machines[index];
	struct p2p_mm_decision decision = machine->search.decision;
	unsigned char token[P2P_TOKEN_SIZE];
	int host;

	memset(token, index + 1, sizeof(token));
	machine->search.decision.kind = P2P_MM_DECIDE_NONE;
	if (machine->search.left_game)
	{
		machine->search.left_game = 0;
		if (machine->in >= 0)
			machines[machine->in].players--;
		machine->in = -1;
		machine->hosting = 0;
	}
	if (machine->in >= 0 && (machines[machine->in].quit || !machines[machine->in].hosting))
	{
		machine->in = -1;
		mm_game_lost(&machine->search, now_ms);
	}
	if (decision.kind == P2P_MM_DECIDE_HOST && ignore_first_host && ignored < 0 &&
		(ignore_only < 0 || ignore_only == index))
	{
		ignored = index;
		return;
	}
	if (decision.kind == P2P_MM_DECIDE_HOST && fail_first_host && failed_host < 0)
	{
		failed_host = index;
		publish(index, quit_out, mm_cancel(&machine->search, quit_out, MM_MAXIMUM_OUT));
		return;
	}
	if (decision.kind == P2P_MM_DECIDE_HOST)
	{
		machine->hosting = 1;
		machine->players = 1;
		machine->most = p2p_mm_size_players(decision.size);
	}
	else if (decision.kind == P2P_MM_DECIDE_JOIN)
	{
		joins_taken++;
		machine->hosting = 0;
		if (fail_join == index)
		{
			mm_hosted(&machine->search, token, 1, 4, 0, now_ms);
			hosted_while_joining = machine->search.state == P2P_MM_HOSTING;
			mm_joined(&machine->search, 0, now_ms);
			return;
		}
		if (cancel_on_join && index == 1)
		{
			publish(index, quit_out, mm_cancel(&machine->search, quit_out, MM_MAXIMUM_OUT));
			cancel_sent = 1;
			return;
		}
		if (never_joins == index)
		{
			machine->quit = 1;
			return;
		}
		host = machine_of_invite(decision.invite);
		if (host < 0 || machines[host].quit || !machines[host].hosting ||
			machines[host].players >= machines[host].most)
		{
			mm_joined(&machine->search, 0, now_ms);
			return;
		}
		machine->in = host;
		machines[host].players++;
		mm_joined(&machine->search, 1, now_ms);
	}
	if (machine->hosting)
	{
		mm_hosted(&machine->search, token, machine->players, machine->most, machine->players == machine->most, now_ms);
	}
}

static void run(double seconds)
{
	unsigned long until = now_ms + (unsigned long)(seconds * 1000);
	struct mm_out out[MM_MAXIMUM_OUT];
	int index;

	for (; now_ms < until; now_ms += 100)
	{
		if (now_ms % 1000 == 0)
			now_unix++;
		deliver();
		for (index = 0; index < machine_count; index++)
		{
			if (machines[index].quit)
				continue;
			publish(index, out,
				mm_step(&machines[index].search, now_ms, out, MM_MAXIMUM_OUT));
			game_side(index);
			if (index < party_machines)
				party_machine_step(index);
		}
	}
}

static void world(int count)
{
	memset(machines, 0, sizeof(machines));
	machine_count = count;
	party_machines = 0;
	for (count = 0; count < 8; count++)
		machines[count].in = -1;
	ignore_first_host = fail_first_host = 0;
	ignored = failed_host = ignore_only = -1;
	now_ms = 1000;
	now_unix = 1800000000UL;
	drop_percent = flight_count = captured_count = cancel_on_join = cancel_sent = 0;
	never_joins = capture_from = fail_join = -1;
	hosted_while_joining = joins_taken = 0;
	memset(published_kinds, 0, sizeof(published_kinds));
	drop_percent = sweep_drop;
	srand(sweep_seed);
}

static void start_searching(int index, unsigned playlists, int size, int can_host)
{
	struct p2p_mm_choice choice;
	unsigned char seed[P2P_SEED_SIZE];

	memset(&choice, 0, sizeof(choice));
	choice.playlists = playlists;
	choice.size = size;
	seed_of(100 + index, seed);
	mm_start(&machines[index].search, seed, &choice, now_ms, now_unix);
	machines[index].search.state = P2P_MM_QUEUED;
	machines[index].search.can_host = can_host;
}

static void start_big(int index, unsigned playlists, int size, int can_host)
{
	start_searching(index, playlists, size, can_host);
	machines[index].search.state = P2P_MM_LOOKING;
}

static int find_host(void)
{
	int index;

	for (index = 0; index < machine_count; index++)
		if (machines[index].hosting && !machines[index].quit)
			return index;
	return -1;
}

static int host_count(void)
{
	int index, count = 0;

	for (index = 0; index < machine_count; index++)
		count += machines[index].hosting && !machines[index].quit;
	return count;
}

static int peer_count(const struct mm_search *search)
{
	int index, count = 0;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
		count += search->peers[index].used && mm_counted(&search->peers[index]);
	return count;
}

static int host_record_open(const struct mm_search *search)
{
	return search->state == P2P_MM_HOSTING && search->held_until && (long)(now_ms - search->held_until) >= 0;
}

static int same_pool(const struct mm_search *a, const struct mm_search *b)
{
	return a->pool_count > 0 && a->pool_count == b->pool_count &&
		!memcmp(a->pool, b->pool, (size_t)a->pool_count * P2P_KEY_HASH_SIZE);
}

static void check_four(void)
{
	int index, host, in_host = 0;

	case_name = "e4_four";
	world(4);
	for (index = 0; index < 4; index++)
	{
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
		run(1.0);
	}
	run(30.0);
	host = find_host();
	CHECK(host >= 0 && host_count() == 1, "E4: one machine hosts: %d", host_count());
	for (index = 0; host >= 0 && index < 4; index++)
		in_host += machines[index].in == host;
	CHECK(host >= 0 && in_host == 3 && machines[host].players == 4, "E4: three join by invite: %d", in_host);
	for (index = 0; host >= 0 && index < 4; index++)
		CHECK(index == host || (machines[index].search.joining_players == 4 &&
			machines[index].search.joining_most_players == 4), "E4: MATCH FOUND counts the pool, the host included: %d of %d",
			machines[index].search.joining_players, machines[index].search.joining_most_players);
	if (host >= 0)
	{
		struct mm_out out[MM_MAXIMUM_OUT];
		struct mm_record record;
		int count = mm_step(&machines[host].search, now_ms + 60000, out, MM_MAXIMUM_OUT);

		CHECK(count == 1 && mm_record_read(out[0].bytes, out[0].size, &record) && record.under_way &&
			record.interval == 250 && machines[host].search.next_hosting_record == now_ms + 85000,
			"E4: a game under way states 25 s and sends every 25 s");
	}
}

static void check_two_playlists(void)
{
	char ctf[P2P_MM_TOPIC_SIZE];

	case_name = "e17_two_playlists";
	world(5);
	mm_topic(P2P_MM_CTF, P2P_MM_2V2, ctf, sizeof(ctf));
	start_searching(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(2, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(3, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(4, 1u << P2P_MM_CTF, P2P_MM_2V2, 1);
	run(30.0);
	CHECK(machines[0].in >= 0 || machines[0].hosting, "E17: in the Slayer match");
	CHECK(!subscribed(&machines[0].search, ctf), "E17: the CTF queue cancelled");
	CHECK(peer_count(&machines[4].search) == 0, "E17: the CTF searcher no longer counts it");
}

static void check_ffa_leaves_ctf(void)
{
	char ctf[P2P_MM_TOPIC_SIZE], slayer[P2P_MM_TOPIC_SIZE];

	case_name = "e63_ffa_leaves_ctf";
	world(2);
	mm_topic(P2P_MM_CTF, P2P_MM_FFA_6, ctf, sizeof(ctf));
	mm_topic(P2P_MM_SLAYER, P2P_MM_FFA_6, slayer, sizeof(slayer));
	start_searching(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_FFA_6, 1);
	start_searching(1, 1u << P2P_MM_CTF, P2P_MM_FFA_6, 1);
	run(8.0);
	CHECK(subscribed(&machines[0].search, slayer), "E63: the Slayer FFA 6 queue");
	CHECK(!subscribed(&machines[0].search, ctf), "E63: no CTF FFA 6 queue");
	CHECK(peer_count(&machines[0].search) == 0, "E63: a CTF-only FFA 6 search is not counted by it");
}

static void check_lost_cancel(void)
{
	case_name = "e19_lost_cancel";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(8.0);
	CHECK(peer_count(&machines[1].search) == 1, "heard");
	machines[0].quit = 1;
	run(6.5);
	CHECK(peer_count(&machines[1].search) == 1, "E19: kept within 3 intervals of its last record");
	run(4.0);
	CHECK(peer_count(&machines[1].search) == 0, "E19: dropped 10 s after its last record");
}

static void check_cancel_mid_join(void)
{
	int index, host;

	case_name = "e15_cancel_mid_join";
	world(4);
	cancel_on_join = 1;
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, index != 1);
	run(20.0);
	host = find_host();
	CHECK(cancel_sent && machines[1].in < 0 && machines[1].search.state == P2P_MM_OFF,
		"E15: the join stops and a cancel is sent");
	run(21.0);
	CHECK(host >= 0 && host_record_open(&machines[host].search), "E15: the host's held slot opens after 20 s");
}

static void check_never_joins(void)
{
	int index, host;

	case_name = "e41_never_joins";
	world(5);
	never_joins = 3;
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, index != 3);
	run(35.0);
	host = find_host();
	CHECK(host >= 0 && machines[host].players == 3, "E41: the lobby shows 3 of 4");
	CHECK(host >= 0 && host_record_open(&machines[host].search), "E41: the held slot opens after 20 s");
	start_searching(4, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(10.0);
	CHECK(host >= 0 && machines[4].in == host && machines[host].players == 4, "E41: the next searcher fills it");
	CHECK(machines[4].search.joining_players == 4, "E41: its MATCH FOUND counts the lobby and itself: %d",
		machines[4].search.joining_players);
}

static void check_stun_unknown(void)
{
	int index;

	case_name = "e48_stun_unknown";
	world(4);
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, index == 3 ? -1 : 0);
	run(20.0);
	CHECK(host_count() == 0, "E48: unknown cannot host");
	CHECK(!machines[0].search.pool_count && !machines[1].search.pool_count && !machines[2].search.pool_count,
		"E48: no one forms a pool around it");
	machines[3].search.can_host = 1;
	run(20.0);
	CHECK(machines[3].hosting, "E48: once STUN answers, it can host");
}

static void check_quit(void)
{
	case_name = "quit";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(8.0);
	publish(0, quit_out, mm_cancel(&machines[0].search, quit_out, MM_MAXIMUM_OUT));
	machines[0].quit = 1;
	run(0.5);
	CHECK(peer_count(&machines[1].search) == 0, "quit: dropped at once by its cancel");
}

static void check_reconnect(void)
{
	unsigned long started;

	case_name = "reconnect";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(8.0);
	started = machines[0].search.started;
	drop_percent = 100;
	run(15.0);
	CHECK(peer_count(&machines[1].search) == 0, "expired while unheard");
	drop_percent = 0;
	run(9.5);
	CHECK(machines[0].search.started == started, "reconnect: the same start");
	CHECK(peer_count(&machines[1].search) == 1, "reconnect: counted again after 2 refreshes");
}

static void check_replay(void)
{
	case_name = "e43_replay";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	capture_from = 0;
	run(8.0);
	capture_from = -1;
	publish(0, quit_out, mm_cancel(&machines[0].search, quit_out, MM_MAXIMUM_OUT));
	machines[0].quit = 1;
	run(1.0);
	replay_captured(1);
	run(7.0);
	CHECK(peer_count(&machines[1].search) == 0, "E43: a replayed searcher is never counted again");
}

static void check_two_refreshes(void)
{
	case_name = "e14_two_refreshes";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(1.0);
	CHECK(peer_count(&machines[1].search) == 0, "E14: not counted on its first record");
	run(4.0);
	CHECK(peer_count(&machines[1].search) == 0, "E14: not counted on its first refresh");
	run(2.5);
	CHECK(peer_count(&machines[1].search) == 1, "E14: counted on its second refresh");
}

static void check_late_listener(void)
{
	int index;

	case_name = "e37_late_listener";
	world(6);
	for (index = 0; index < 5; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 1);
	run(25.0);
	CHECK(host_count() == 0 && machines[0].search.pool_count == 0, "E37: five searchers are no pool of six");
	start_searching(5, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 1);
	run(15.0);
	CHECK(same_pool(&machines[0].search, &machines[5].search), "E37: the same pool, in the same order");
	CHECK(host_count() == 1, "E37: one host: %d", host_count());
}

static void heard_record(struct mm_search *search, int machine, const struct mm_record *record)
{
	struct mm_record read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	seed_of(machine, seed);
	size = mm_record_write(record, seed, bytes, sizeof(bytes));
	if (mm_record_read(bytes, size, &read_back))
		mm_heard(search, &read_back, now_ms);
}

static void heard_kind(struct mm_search *search, int machine, unsigned long sequence, int kind)
{
	struct mm_record record = searching(machine, sequence);

	record.kind = kind;
	heard_record(search, machine, &record);
}

static void heard_big(struct mm_search *search, int machine, int kind, int most_players)
{
	struct mm_record record = searching(machine, 1);

	record.kind = kind;
	record.size = P2P_MM_BIG_TEAM;
	record.players = 10;
	record.most_players = most_players;
	heard_record(search, machine, &record);
}

static void heard_from(struct mm_search *search, int machine, unsigned long sequence)
{
	heard_kind(search, machine, sequence, MM_KIND_SEARCHING);
}

static int has_peer(const struct mm_search *search, int machine)
{
	struct mm_record record = searching(machine, 1);
	int index;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
		if (search->peers[index].used && !memcmp(search->peers[index].record.key, record.key, P2P_KEY_SIZE))
			return 1;
	return 0;
}

static void check_full_table(void)
{
	int machine;

	case_name = "full_table";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	for (machine = 300; machine < 555; machine++, now_ms++)
	{
		heard_from(&machines[0].search, machine, 1);
		heard_from(&machines[0].search, machine, 2);
		heard_from(&machines[0].search, machine, 3);
	}
	heard_from(&machines[0].search, 555, 1);
	CHECK(has_peer(&machines[0].search, 555), "256 searchers fill the table");
	heard_from(&machines[0].search, 556, 1);
	CHECK(has_peer(&machines[0].search, 556) && !has_peer(&machines[0].search, 555),
		"a new searcher replaces the uncounted one");
	heard_from(&machines[0].search, 556, 2);
	heard_from(&machines[0].search, 556, 3);
	heard_from(&machines[0].search, 557, 1);
	heard_from(&machines[0].search, 557, 2);
	heard_from(&machines[0].search, 557, 3);
	CHECK(!has_peer(&machines[0].search, 557) && has_peer(&machines[0].search, 300),
		"counted searchers are never evicted for a new one");
}

static void check_search_after_cancel(void)
{
	struct mm_search *search = &machines[0].search;

	case_name = "e14_after_cancel";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	heard_from(search, 300, 1);
	heard_from(search, 300, 2);
	heard_from(search, 300, 3);
	heard_kind(search, 300, 4, MM_KIND_CANCEL);
	heard_from(search, 300, 5);
	heard_from(search, 300, 6);
	CHECK(peer_count(search) == 0, "E14: a search after a cancel is not counted on its first refresh");
	heard_from(search, 300, 7);
	CHECK(peer_count(search) == 1, "E14: a search after a cancel is counted on its second refresh");
}

static void check_restart(void)
{
	case_name = "restart";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(8.0);
	publish(0, quit_out, mm_cancel(&machines[0].search, quit_out, MM_MAXIMUM_OUT));
	run(1.0);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(6.5);
	CHECK(peer_count(&machines[1].search) == 1, "a search begun again at once is counted within 2 intervals");
}

static void check_left_queue(void)
{
	struct mm_record record = searching(300, 1), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];

	case_name = "left_queue";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_2V2, 1);
	seed_of(300, seed);
	record.kind = MM_KIND_HOSTING;
	record.playlist = P2P_MM_CTF;
	record.players = 1;
	record.most_players = 4;
	if (mm_record_read(bytes, mm_record_write(&record, seed, bytes, sizeof(bytes)), &read_back))
		mm_heard(&machines[0].search, &read_back, now_ms);
	machines[0].search.queue_count = 1;
	run(1.0);
	CHECK(joins_taken == 0, "a game in a queue the search has left is not joined");
}

static void check_failed_join(void)
{
	char ctf[P2P_MM_TOPIC_SIZE], slayer[P2P_MM_TOPIC_SIZE];
	int index;

	case_name = "failed_join";
	world(4);
	fail_join = 0;
	mm_topic(P2P_MM_CTF, P2P_MM_2V2, ctf, sizeof(ctf));
	mm_topic(P2P_MM_SLAYER, P2P_MM_2V2, slayer, sizeof(slayer));
	start_searching(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_2V2, 0);
	for (index = 1; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(25.0);
	CHECK(joins_taken > 0, "the failing machine was sent to a game");
	CHECK(!hosted_while_joining, "a JOIN is never answered with a hosted game");
	CHECK(machines[0].search.state == P2P_MM_QUEUED && subscribed(&machines[0].search, slayer) &&
		subscribed(&machines[0].search, ctf), "after a failed join both queues are searched again");
}

static int host_record_held(const struct mm_search *search)
{
	return search->state == P2P_MM_HOSTING && search->held_until && (long)(now_ms - search->held_until) < 0;
}

static int no_big_queue(const struct mm_search *search)
{
	int index;

	for (index = 0; index < search->queue_count; index++)
		if (search->queues[index][1] >= NUMBER_OF_P2P_MM_SMALL_SIZES)
			return 0;
	return 1;
}

static void check_alone(void)
{
	case_name = "e5_alone";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(44.5);
	CHECK(!machines[0].search.widened, "not widened before 0:45");
	run(1.0);
	CHECK(machines[0].search.widened && machines[0].search.queue_count == NUMBER_OF_P2P_MM_SMALL_SIZES,
		"E5: widened at 0:45 to every small size: %d queues", machines[0].search.queue_count);
	CHECK(no_big_queue(&machines[0].search), "E34: widening takes no big size");
	run(73.0);
	CHECK(!machines[0].hosting, "not hosting before 2:00");
	run(2.0);
	CHECK(machines[0].hosting && machines[0].search.hosting_size == P2P_MM_2V2,
		"E5: hosts an open SLAYER 2V2 at 2:00");
	CHECK(!host_record_held(&machines[0].search), "E5: open, not held");
}

static void check_widen_now(void)
{
	int index;

	case_name = "e6_widen_now";
	world(8);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	for (index = 1; index < 8; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1);
	run(10.0);
	CHECK(!machines[0].search.widened, "E6: still narrow at 10 s");
	mm_widen_now(&machines[0].search);
	run(25.0);
	CHECK(machines[0].in >= 0 || machines[0].hosting, "E6: joins the 4v4 pool");
}

static void check_host_now(void)
{
	case_name = "e7_host_now";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(20.0);
	mm_host_now(&machines[0].search);
	run(10.0);
	CHECK(machines[0].hosting && machines[1].in == 0, "E7: hosts at once; the other joins");
}

static void check_strict(void)
{
	case_name = "e13_strict";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	mm_host_now(&machines[0].search);
	run(125.0);
	CHECK(!machines[0].hosting && machines[0].search.state == P2P_MM_QUEUED,
		"E13: HOST NOW and 2:00 do nothing for a strict router; it keeps searching");
}

static void check_full_match(void)
{
	int index, joins;

	case_name = "e8_full";
	world(5);
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(30.0);
	CHECK(find_host() >= 0 && machines[find_host()].players == 4, "a full match");
	joins = joins_taken;
	start_searching(4, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(46.0);
	CHECK(machines[4].in < 0 && joins_taken == joins, "E8: does not join the 4/4 game");
	CHECK(machines[4].search.widened, "E8: widened at its 0:45");
	run(75.0);
	CHECK(machines[4].hosting, "E8: hosts its own at 2:00");
}

static void check_widened_retry(void)
{
	int index;

	case_name = "widened_retry";
	world(4);
	fail_join = 0;
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	mm_widen_now(&machines[0].search);
	for (index = 1; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(25.0);
	CHECK(joins_taken > 0, "the failing machine was sent to a game");
	CHECK(machines[0].search.state == P2P_MM_QUEUED && machines[0].search.widened &&
		machines[0].search.queue_count == NUMBER_OF_P2P_MM_SMALL_SIZES,
		"after a failed join a widened search stands in every small size: %d", machines[0].search.queue_count);
}

static int lowest_key_hash(int count)
{
	int index, low = 0;

	world(count);
	for (index = 0; index < count; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	for (index = 1; index < count; index++)
		if (memcmp(machines[index].search.key_hash, machines[low].search.key_hash, P2P_KEY_HASH_SIZE) < 0)
			low = index;
	return low;
}

static struct mm_record heard_as(struct mm_search *search, int machine, unsigned long sequence, int can_host)
{
	struct mm_record record = searching(machine, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];

	seed_of(machine, seed);
	record.can_host = can_host;
	mm_record_read(bytes, mm_record_write(&record, seed, bytes, sizeof(bytes)), &read_back);
	if (search)
		mm_heard(search, &read_back, now_ms);
	return read_back;
}

static struct mm_record party_of(int leader, int members, unsigned long sequence)
{
	struct mm_record record = searching(leader, sequence);
	unsigned char seed[P2P_SEED_SIZE];
	int index;

	record.party = members + 1;
	record.size = P2P_MM_4V4;
	for (index = 0; index < members; index++)
	{
		seed_of(leader + 1 + index, seed);
		p2p_ed25519_public(seed, record.member_keys[index], NULL);
		mm_party_sign(seed, record.key, record.member_signatures[index]);
	}
	return record;
}

static void heard_party(struct mm_search *search, int leader, int members, unsigned long sequence, int playlist, int size)
{
	struct mm_record record = party_of(leader, members, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];

	record.playlist = playlist;
	record.size = size;
	seed_of(leader, seed);
	if (mm_record_read(bytes, mm_record_write(&record, seed, bytes, sizeof(bytes)), &read_back))
		mm_heard(search, &read_back, now_ms);
}

static void check_strict_lowest(void)
{
	int strict = 300, sequence;

	case_name = "strict_lowest";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	while (strict < 400 &&
		memcmp(heard_as(NULL, strict, 1, 0).key_hash, machines[0].search.key_hash, P2P_KEY_HASH_SIZE) >= 0)
		strict++;
	for (sequence = 1; sequence < 45; sequence++)
	{
		heard_as(&machines[0].search, strict, sequence, 0);
		run(3.0);
	}
	CHECK(machines[0].hosting, "a strict router with the lowest key hash does not stop a capable searcher hosting at 2:00");
}

static void check_pool_forming(void)
{
	int low = lowest_key_hash(4), index;

	case_name = "pool_forming";
	world(4);
	start_searching(low, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(111.0);
	for (index = 0; index < 4; index++)
		if (index != low)
			start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(9.5);
	CHECK(host_count() == 0 && machines[low].search.pool_count > 0, "a pool settling at 2:00 opens no game");
	run(10.0);
	CHECK(host_count() == 1, "the pool's host hosts one game: %d", host_count());
}

static void check_host_now_before_join(void)
{
	case_name = "host_now_first";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, -1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	mm_host_now(&machines[0].search);
	run(5.0);
	CHECK(!machines[0].hosting, "HOST NOW waits while STUN is unanswered");
	machines[0].search.can_host = 1;
	run(1.0);
	CHECK(machines[0].hosting, "HOST NOW pressed before STUN answered is served once it does");
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(10.0);
	mm_host_now(&machines[1].search);
	run(4.0);
	CHECK(machines[1].hosting && machines[0].in == 1, "the other searcher joins the game opened");
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(10.0);
	mm_host_now(&machines[1].search);
	run(0.2);
	mm_host_now(&machines[0].search);
	run(0.1);
	CHECK(machines[0].hosting && machines[0].in < 0, "HOST NOW is served before a game is joined");
}

static int game_most_players = 4;

static void hear_game(int host, int players, int held, unsigned long sequence, unsigned char *key_hash)
{
	struct mm_record record = searching(host, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE], token[P2P_TOKEN_SIZE];

	seed_of(host, seed);
	memset(token, host, sizeof(token));
	record.kind = MM_KIND_HOSTING;
	record.players = players;
	record.most_players = game_most_players;
	memcpy(record.token, token, P2P_TOKEN_SIZE);
	if (held)
	{
		record.held = record.pool_count = 1;
		memcpy(record.pool[0], machines[0].search.key_hash, P2P_KEY_HASH_SIZE);
		mm_seal_token(seed, machines[0].search.key, token, record.sealed[0]);
	}
	if (mm_record_read(bytes, mm_record_write(&record, seed, bytes, sizeof(bytes)), &read_back))
	{
		mm_heard(&machines[0].search, &read_back, now_ms);
		memcpy(key_hash, read_back.key_hash, P2P_KEY_HASH_SIZE);
	}
}

static void check_invited_twice(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	unsigned char thin[P2P_KEY_HASH_SIZE], fuller[P2P_KEY_HASH_SIZE];
	char text[2 * P2P_KEY_HASH_SIZE + 1];

	case_name = "e10_invited_twice";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	hear_game(300, 1, 1, 1, thin);
	hear_game(301, 3, 1, 1, fuller);
	mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
	p2p_hex(fuller, P2P_KEY_HASH_SIZE, text);
	CHECK(!machines[0].search.decision.public_game, "a pool's join is not a public game's");
	CHECK(machines[0].search.decision.kind == P2P_MM_DECIDE_JOIN &&
		!strncmp(machines[0].search.decision.invite + strlen("halo://join/"), text, strlen(text)),
		"E10: invited by two hosts, it joins the one with more players");
}

static void check_passed_over_hosts(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	unsigned char host[P2P_KEY_HASH_SIZE];
	char text[2 * P2P_KEY_HASH_SIZE + 1];
	int index;

	case_name = "passed_over_hosts";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	hear_game(300, 1, 0, 1, host);
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
		machines[0].search.peers[index].failed = machines[0].search.peers[index].used ? MM_FAILED_PASSED_OVER : MM_FAILED_NONE;
	hear_game(300, 1, 0, 2, host);
	mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
	p2p_hex(host, P2P_KEY_HASH_SIZE, text);
	CHECK(machines[0].search.decision.kind == P2P_MM_DECIDE_JOIN &&
		!strncmp(machines[0].search.decision.invite + strlen("halo://join/"), text, strlen(text)),
		"a host passed over, then heard hosting, is joined");
}

static void check_host_quits_lobby(void)
{
	unsigned long starts[8];
	int index, host, kept = 1;

	case_name = "e11_host_quits";
	world(5);
	for (index = 0; index < 4; index++)
	{
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
		starts[index] = machines[index].search.started;
	}
	run(17.0);
	host = find_host();
	CHECK(host >= 0, "a host");
	if (host < 0)
		return;
	machines[host].quit = 1;
	run(1.0);
	for (index = 0; index < 4; index++)
		if (index != host)
			kept &= machines[index].search.state == P2P_MM_LOOKING && machines[index].search.started == starts[index];
	CHECK(kept, "E11: back in the search with their original starts");
	start_searching(4, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(30.0);
	CHECK(host_count() == 1 && machines[find_host()].players == 4, "E11: they elect again with the next searcher");
}

static void check_never_hosts(void)
{
	int index;

	case_name = "e12_never_hosts";
	world(5);
	ignore_first_host = 1;
	for (index = 0; index < 5; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(20.0);
	if (!sweep_drop)
		CHECK(ignored >= 0 && host_count() == 0, "the elected host never hosted");
	run(sweep_drop ? 55.0 : 25.0);
	CHECK(ignored >= 0 && !machines[ignored].hosting, "E12: the elected host never hosts");
	if (!sweep_drop)
		CHECK(host_count() == 1, "E12: passed over after 20 s; another hosts");
	for (index = 0; index < 5; index++)
		CHECK(!machines[index].hosting || machines[index].players == machines[index].most,
			"E12: machine %d's game is full: one game per pool", index);
	for (index = 0; index < 5; index++)
		CHECK(index == ignored || machines[index].in >= 0 || machines[index].hosting,
			"E12: machine %d has a game once the host is passed over: no seat held for it", index);
}

static void check_return(void)
{
	unsigned long returned;
	int index, host, restarted = 1;

	case_name = "e39_return";
	world(4);
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(100.0);
	host = find_host();
	CHECK(host >= 0 && machines[host].players == 4, "a full match");
	if (host < 0)
		return;
	machines[host].quit = 1;
	run(0.2);
	returned = now_ms;
	for (index = 0; index < 4; index++)
		if (index != host)
			restarted &= machines[index].search.marks_from >= returned - 200;
	CHECK(restarted, "E39: marks restart from the return");
	run(10.0);
	CHECK(host_count() == 0, "E39: no lone host at once: %d", host_count());
	run(115.0);
	CHECK(host_count() >= 1 && host_count() <= 2, "E39: past the return's 2:00, at most two hosts: %d", host_count());
	for (index = 0; index < 4; index++)
		CHECK(index == host || machines[index].in >= 0 || machines[index].hosting,
			"E39: machine %d is in a game or hosting", index);
}

static void check_two_lone_hosts(void)
{
	case_name = "e40_two_lone_hosts";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	mm_host_now(&machines[0].search);
	mm_host_now(&machines[1].search);
	run(15.0);
	CHECK(host_count() == 1 && (machines[0].in >= 0 || machines[1].in >= 0),
		"E40: the higher key hash hands over and joins the other's game");
}

static void check_big_hosts(void)
{
	char topic[P2P_MM_TOPIC_SIZE];
	int index, on_topic = 0;

	case_name = "e32_big_hosts";
	world(2);
	capture_from = 0;
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1);
	run(5.5);
	CHECK(!machines[0].hosting, "step 1 comes first");
	run(1.0);
	CHECK(machines[0].hosting && machines[0].search.hosting_size == P2P_MM_BIG_TEAM,
		"E32: hosts within 6.5 s when nothing fits");
	CHECK(host_record_open(&machines[0].search) && machines[0].search.most_players == 16, "E32: open, at MAX PLAYERS");
	mm_topic(P2P_MM_SLAYER, P2P_MM_BIG_TEAM, topic, sizeof(topic));
	for (index = 0; index < captured_count; index++)
		on_topic += captured[index].bytes[5] == MM_KIND_HOSTING && !strcmp(captured[index].topic, topic);
	CHECK(on_topic, "E32: its hosting record is on %s", topic);
	start_big(1, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1);
	run(7.0);
	CHECK(machines[1].in == 0 && host_count() == 1, "E32: the next searcher joins it");
	world(2);
	machines[0].ping = 200;
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1);
	run(6.5);
	start_big(1, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 1);
	run(7.0);
	CHECK(machines[1].in < 0 && host_count() == 2, "E58: a searcher hosts rather than join a host at 200 ms");
	CHECK(!published_kinds[MM_KIND_SEARCHING], "E32: no searching record: %d", published_kinds[MM_KIND_SEARCHING]);
	world(1);
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, 0);
	run(10.0);
	CHECK(!machines[0].hosting && machines[0].search.state == P2P_MM_QUEUED, "a strict router never hosts");
	world(1);
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM, -1);
	run(10.0);
	CHECK(!machines[0].hosting, "no host until STUN has answered");
	machines[0].search.can_host = 1;
	run(1.0);
	CHECK(machines[0].hosting, "STUN answered: hosts");
}

static void check_big_lost(void)
{
	struct p2p_mm_choice choice;

	case_name = "big_lost";
	memset(&choice, 0, sizeof(choice));
	choice.playlists = 1u << P2P_MM_SLAYER;
	choice.size = P2P_MM_BIG_TEAM;
	p2p_mm_search(&choice);
	masters_forgotten = masters_refreshed = 0;
	p2p_mm_game_lost();
	CHECK(masters_forgotten == 1 && masters_refreshed == 1, "a restarted big search asks the masters again: %d %d",
		masters_forgotten, masters_refreshed);
	p2p_mm_cancel();
}

static void check_big_records(void)
{
	static struct mm_search search;
	unsigned char seed[P2P_SEED_SIZE];
	struct p2p_mm_choice choice;

	case_name = "big_records";
	memset(&choice, 0, sizeof(choice));
	choice.playlists = 1u << P2P_MM_SLAYER;
	choice.size = P2P_MM_BIG_TEAM;
	seed_of(30, seed);
	mm_start(&search, seed, &choice, 1000, 1800000000UL);
	heard_big(&search, 31, MM_KIND_HOSTING, HALO_PORT_MAXIMUM_NETWORK_PLAYERS);
	CHECK(has_peer(&search, 31), "a big host's MAX PLAYERS may reach the port's most");
	heard_big(&search, 32, MM_KIND_HOSTING, HALO_PORT_MAXIMUM_NETWORK_PLAYERS + 1);
	CHECK(!has_peer(&search, 32), "a big game past the port's most is a fake");
	heard_big(&search, 33, MM_KIND_SEARCHING, 0);
	CHECK(!has_peer(&search, 33), "big sizes never pool: a searching record is not heard");
}

static void check_lone_merge(void)
{
	int higher, lower;

	case_name = "e33_lone_merge";
	world(2);
	drop_percent = 100;
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	run(2.0);
	start_big(1, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	higher = memcmp(machines[0].search.key_hash, machines[1].search.key_hash, P2P_KEY_HASH_SIZE) > 0 ? 0 : 1;
	lower = !higher;
	run(6.5);
	CHECK(host_count() == 2, "both host, 2 s apart: %d", host_count());
	drop_percent = sweep_drop;
	run(15.0);
	CHECK(machines[higher].in == lower && machines[lower].hosting && host_count() == 1,
		"E33: the higher key hash, still alone, joins the other's game");
	world(2);
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	start_big(1, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	run(6.5);
	machines[0].players = machines[1].players = 2;
	run(15.0);
	CHECK(host_count() == 2, "E33: a host no longer alone stays: %d hosts", host_count());
	world(2);
	start_big(0, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	start_big(1, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
	higher = memcmp(machines[0].search.key_hash, machines[1].search.key_hash, P2P_KEY_HASH_SIZE) > 0 ? 0 : 1;
	lower = !higher;
	machines[lower].ping = 250;
	run(21.5);
	CHECK(host_count() == 2, "E58: a host at 250 ms is not merged with: %d hosts", host_count());
	machines[lower].ping = -1;
	run(10.0);
	CHECK(host_count() == 2, "E58: a host not yet pinged is not merged with: %d hosts", host_count());
	machines[lower].ping = 199;
	run(15.0);
	CHECK(machines[higher].in == lower && host_count() == 1, "E58: at 199 ms the merge goes ahead");
}

static void check_big_hold(void)
{
	int order[3] = {0, 1, 2};
	int best, middle, last, round, i, j;

	for (round = 0; round < 2; round++)
	{
		case_name = "big_hold";
		world(3);
		for (i = 0; i < 3; i++)
			start_big(i, 1u << P2P_MM_SLAYER, P2P_MM_BIG_FFA, 1);
		for (i = 0; i < 3; i++)
		{
			for (j = i + 1; j < 3; j++)
			{
				if (memcmp(machines[order[i]].search.key_hash, machines[order[j]].search.key_hash,
					P2P_KEY_HASH_SIZE) > 0)
				{
					int swap = order[i];

					order[i] = order[j];
					order[j] = swap;
				}
			}
		}
		best = order[0];
		middle = order[1];
		last = order[2];
		machines[best].ping = -1;
		run(20.0);
		CHECK(host_count() == 3, "the last host waits for a better host still unpinged: %d hosts", host_count());
		if (round)
		{
			machines[best].ping = 0;
			run(15.0);
			CHECK(machines[last].in == best && machines[middle].in == best && host_count() == 1,
				"hosts who started together all reach the best host");
		}
		else
		{
			run(30.0);
			CHECK(machines[last].in == middle, "a host that never answers a ping holds no one for long");
		}
	}
}

static void check_small_hands_over(void)
{
	case_name = "small_hands_over";
	world(2);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	mm_host_now(&machines[0].search);
	mm_host_now(&machines[1].search);
	run(0.5);
	machines[0].players = machines[1].players = 2;
	run(15.0);
	CHECK(host_count() == 1, "E10: a small host with players still hands over: %d hosts", host_count());
}

static void check_widen_no_big(void)
{
	case_name = "e34_widen_no_big";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	add_listing("large", 2, 46, 128, 40);
	run(46.0);
	CHECK(machines[0].search.widened && no_big_queue(&machines[0].search), "E34: widened to the small sizes alone");
	heard_big(&machines[0].search, 31, MM_KIND_HOSTING, 16);
	run(5.0);
	CHECK(!has_peer(&machines[0].search, 31), "E34: a big hosting record is not heard");
	CHECK(machines[0].in < 0 && !joins_taken, "E34: never joins a big game");
	listing_count = 0;
}

static void check_big_ffa_no_ctf(void)
{
	char slayer[P2P_MM_TOPIC_SIZE], ctf[P2P_MM_TOPIC_SIZE];

	case_name = "e64_big_ffa_no_ctf";
	CHECK(!mm_playlist_has_size(1u << P2P_MM_CTF, P2P_MM_BIG_FFA) &&
		mm_playlist_has_size(1u << P2P_MM_CTF, P2P_MM_BIG_TEAM), "only CTF: BIG FFA greyed, BIG TEAM offered");
	mm_topic(P2P_MM_SLAYER, P2P_MM_BIG_FFA, slayer, sizeof(slayer));
	mm_topic(P2P_MM_CTF, P2P_MM_BIG_FFA, ctf, sizeof(ctf));
	world(1);
	start_big(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_BIG_FFA, 1);
	CHECK(subscribed(&machines[0].search, slayer) && !subscribed(&machines[0].search, ctf),
		"E64: SLAYER and CTF: only Slayer's topic");
	start_big(0, 1u << P2P_MM_CTF, P2P_MM_BIG_FFA, 1);
	CHECK(!machines[0].search.queue_count, "E64: only CTF: no topic");
}

static void check_host_failed(void)
{
	int index;

	case_name = "host_failed";
	world(5);
	fail_first_host = 1;
	for (index = 0; index < 5; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	while (failed_host < 0 && now_ms < 30000)
		run(0.1);
	CHECK(failed_host >= 0 && (sweep_drop || host_count() == 0), "the elected host failed");
	run(sweep_drop ? 40.0 : 8.0);
	CHECK(host_count() == 1 && failed_host >= 0 && !machines[failed_host].hosting, "host_failed: elected again within 8 s, not 20");
	for (index = 0; index < 5; index++)
		CHECK(index == failed_host || machines[index].in >= 0 || machines[index].hosting,
			"host_failed: machine %d is in the one game", index);
}

static void check_fake_fuller_game(void)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	int sequence;

	case_name = "fake_fuller_game";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	machines[0].search.state = P2P_MM_HOSTING;
	machines[0].search.players = 1;
	machines[0].search.most_players = 4;
	game_most_players = 16;
	for (sequence = 1; sequence <= 3; sequence++)
		hear_game(20, 3, 0, sequence, key_hash);
	game_most_players = 4;
	run(1.0);
	CHECK(machines[0].search.state == P2P_MM_HOSTING, "a game claiming more players than its size is ignored");
	hear_game(21, 3, 0, 1, key_hash);
	run(1.0);
	CHECK(machines[0].search.state == P2P_MM_HOSTING, "one record does not end a lobby");
	hear_game(21, 3, 0, 2, key_hash);
	hear_game(21, 3, 0, 3, key_hash);
	run(1.0);
	CHECK(machines[0].search.state != P2P_MM_HOSTING, "a counted fuller game takes the lobby");
}

static void check_failed_join_waits(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_record record = searching(20, 1);
	struct p2p_mm_decision decision;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	case_name = "failed_join_waits";
	record.kind = MM_KIND_HOSTING;
	record.players = 1;
	record.most_players = 4;
	seed_of(20, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_heard(bytes, size);
	clock_now += MM_LOOK_TIME;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN, "the first fake is joined");
	p2p_mm_joined(0);
	record = searching(21, 1);
	record.kind = MM_KIND_HOSTING;
	record.players = 1;
	record.most_players = 4;
	seed_of(21, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_heard(bytes, size);
	clock_now += MM_JOIN_BACKOFF - 1000;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_NONE, "a second open game waits out the back-off");
	record.sequence = 2;
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_heard(bytes, size);
	clock_now += 1000;
	p2p_mm_update(clock_now, 1, NULL);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(p2p_mm_take_decision(&decision) == P2P_MM_DECIDE_JOIN, "the open game is joined after the back-off");
	p2p_mm_cancel();
}

static int elects_second(int first, int second)
{
	struct mm_unit units[2];
	int pool[2] = { 0, 1 };

	units[0] = unit(100 + first, 1);
	units[1] = unit(100 + second, 1);
	return mm_elect(units, pool, 2) == 1;
}

static void check_two_queues(void)
{
	int slayer, ctf;

	case_name = "two_queues";
	world(8);
	for (slayer = 1; slayer < 8 && !elects_second(0, slayer); slayer++)
		;
	for (ctf = 1; ctf < 8 && (ctf == slayer || elects_second(0, ctf)); ctf++)
		;
	CHECK(slayer < 8 && ctf < 8, "machines for the two pools");
	if (slayer == 8 || ctf == 8)
		return;
	ignore_first_host = 1;
	ignore_only = slayer;
	start_searching(0, 1u << P2P_MM_SLAYER | 1u << P2P_MM_CTF, P2P_MM_1V1, 1);
	start_searching(slayer, 1u << P2P_MM_SLAYER, P2P_MM_1V1, 1);
	start_searching(ctf, 1u << P2P_MM_CTF, P2P_MM_1V1, 1);
	run(25.0);
	if (!sweep_drop)
		CHECK(ignored == slayer && host_count() == 0, "the Slayer host never hosts; the CTF pool waits for machine 0");
	run(sweep_drop ? 60.0 : 30.0);
	CHECK(host_count() >= 1 && ignored >= 0 && !machines[ignored].hosting, "the CTF pool passes machine 0 over after 20 s");
	CHECK(machines[0].hosting && machines[ctf].in == 0,
		"machine 0 and the CTF machine end in one game: the CTF pool re-elects machine 0");
}

static void check_party_pool(void)
{
	static const int sizes[5] = { 2, 2, 2, 1, 1 };
	struct mm_unit units[8];
	int pool[8], index, count;

	case_name = "e21_party_pool";
	for (index = 0; index < 3; index++)
		units[index] = unit(index, 1);
	units[0].players = 2;
	CHECK(mm_pool(units, 3, 4, 1, pool) == 3, "E21: a party of 2 and two solos are one pool of three units");
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	for (index = 1; index <= 3; index++)
	{
		heard_party(&machines[0].search, 300, 1, index, P2P_MM_SLAYER, P2P_MM_2V2);
		heard_party(&machines[0].search, 302, 0, index, P2P_MM_SLAYER, P2P_MM_2V2);
	}
	run(1.0);
	CHECK(machines[0].search.pool_count == 3, "E21: a searcher pools a heard party as one unit of 2: %d",
		machines[0].search.pool_count);

	case_name = "e26_parties_3v3";
	for (index = 0; index < 5; index++)
		units[index] = unit(index, 1);
	for (index = 0; index < 5; index++)
		units[index].players = sizes[hash_rank(units, 5, index)];
	count = mm_pool(units, 5, 6, 1, pool);
	CHECK(count == 4, "E26: four units in the pool: %d", count);
	if (count == 4)
	{
		CHECK(hash_rank(units, 5, pool[0]) == 0 && hash_rank(units, 5, pool[1]) == 1 &&
			hash_rank(units, 5, pool[2]) == 3 && hash_rank(units, 5, pool[3]) == 4, "E26: A, B, D and E; C waits");
	}
	count = mm_pool(units, 5, 6, 0, pool);
	CHECK(count == 3, "a free-for-all takes three parties of 2: %d", count);
	for (index = 0; index < 3; index++)
		units[index].players = 3 - hash_rank(units, 3, index);
	count = mm_pool(units, 3, 4, 0, pool);
	CHECK(count == 2 && units[pool[0]].players == 3 && units[pool[1]].players == 1,
		"a party that does not fit is passed over: %d", count);
	units[0].players = units[1].players = 2;
	units[0].can_host = units[1].can_host = 0;
	CHECK(mm_pool(units, 2, 4, 1, pool) == 0, "two strict parties form no pool");
}

static void check_party_sizes(void)
{
	static const int refused[] = { P2P_MM_1V1, P2P_MM_2V2 };
	static const int allowed[] = { P2P_MM_3V3, P2P_MM_4V4, P2P_MM_FFA_6, P2P_MM_BIG_TEAM, P2P_MM_BIG_FFA };
	unsigned index;

	case_name = "e22_sizes";
	for (index = 0; index < sizeof(refused) / sizeof(*refused); index++)
		CHECK(!mm_party_size_allowed(3, refused[index]), "E22: a party of 3 is refused on %s",
			p2p_mm_size_names[refused[index]]);
	for (index = 0; index < sizeof(allowed) / sizeof(*allowed); index++)
		CHECK(mm_party_size_allowed(3, allowed[index]), "E22: a party of 3 is allowed on %s",
			p2p_mm_size_names[allowed[index]]);
	CHECK(mm_party_size_allowed(1, P2P_MM_1V1), "a solo stands on every size");
	CHECK(!mm_party_size_allowed(4, P2P_MM_FFA_6), "FFA 6 takes a party of 3 at most");
	CHECK(mm_party_size_allowed(MM_MAXIMUM_PARTY, P2P_MM_BIG_TEAM) && mm_party_size_allowed(MM_MAXIMUM_PARTY, P2P_MM_BIG_FFA),
		"a full party has BIG TEAM and BIG FFA");
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	heard_party(&machines[0].search, 300, 2, 1, P2P_MM_SLAYER, P2P_MM_2V2);
	CHECK(!has_peer(&machines[0].search, 300), "a party too large for its queue is not heard");
}

static void party_key(int machine, unsigned char *key)
{
	unsigned char seed[P2P_SEED_SIZE];

	seed_of(machine, seed);
	p2p_ed25519_public(seed, key, NULL);
}

static void resign(int machine, unsigned char *bytes, int size)
{
	unsigned char seed[P2P_SEED_SIZE], key[P2P_KEY_SIZE], message[MM_MAXIMUM_RECORD_SIZE + 9];

	seed_of(machine, seed);
	party_key(machine, key);
	memcpy(message, "hceu-mm-1", 9);
	memcpy(message + 9, bytes, (size_t)(size - P2P_SIGNATURE_SIZE));
	p2p_ed25519_sign(seed, key, message, 9 + size - P2P_SIGNATURE_SIZE, bytes + size - P2P_SIGNATURE_SIZE);
}

static int party_refused(const struct mm_record *record)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	struct mm_record read_back;

	int size;

	seed_of(300, seed);
	size = mm_record_write(record, seed, bytes, sizeof(bytes));
	verifies = 0;
	return !mm_record_read(bytes, size, &read_back);
}

static void heard_strict(struct mm_search *search, int machine, unsigned long sequence)
{
	struct mm_record record = searching(machine, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];

	record.playlist = P2P_MM_SLAYER;
	record.size = P2P_MM_2V2;
	record.can_host = 0;
	seed_of(machine, seed);
	if (mm_record_read(bytes, mm_record_write(&record, seed, bytes, sizeof(bytes)), &read_back))
		mm_heard(search, &read_back, now_ms);
}

static void set_party(struct mm_search *search, const struct mm_record *record, int member_hosts)
{
	struct mm_search_party party;

	memset(&party, 0, sizeof(party));
	party.count = record->party;
	party.member_hosts = member_hosts;
	memcpy(party.keys, record->member_keys, sizeof(party.keys));
	memcpy(party.signatures, record->member_signatures, sizeof(party.signatures));
	mm_set_party(search, &party, now_ms);
}

static void check_party_search(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	struct mm_record read_back, keys, flags = { 0 };
	int count, index, hosts = 0;

	case_name = "party_search";
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1);
	keys = party_of(100, 3, 1);
	set_party(&machines[0].search, &keys, 0);
	count = mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
	CHECK(count == 1 && mm_record_read(out[0].bytes, out[0].size, &read_back) && read_back.party == 4 &&
		!memcmp(read_back.member_keys, keys.member_keys, 3 * P2P_KEY_SIZE),
		"the leader's search is a format 2 record of its 4 players");
	keys = party_of(100, 1, 1);
	set_party(&machines[0].search, &keys, 0);
	CHECK(machines[0].search.next_record == now_ms, "a smaller party is told at once, start kept");
	mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
	CHECK(machines[0].search.started == now_unix, "the start kept");
	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1);
	keys = party_of(100, 3, 1);
	set_party(&machines[0].search, &keys, 0);
	mm_widen_now(&machines[0].search);
	CHECK(machines[0].search.queue_count == 1 && machines[0].search.queues[0][1] == P2P_MM_4V4,
		"a party of 4 widens to no size it cannot fit: %d queues", machines[0].search.queue_count);

	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	keys = party_of(100, 1, 1);
	set_party(&machines[0].search, &keys, 1);
	mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
	for (index = 0; index < 16; index++)
	{
		now_ms += 500;
		heard_strict(&machines[0].search, 301, 300 + (unsigned long)index);
		heard_strict(&machines[0].search, 302, 300 + (unsigned long)index);
		count = mm_step(&machines[0].search, now_ms, out, MM_MAXIMUM_OUT);
		hosts += machines[0].search.decided == P2P_MM_DECIDE_HOST;
		if (count && mm_record_read(out[0].bytes, out[0].size, &read_back) && read_back.kind == MM_KIND_SEARCHING)
			flags = read_back;
	}
	CHECK(flags.can_host, "a strict leader's party can host through its member");
	CHECK(machines[0].search.pool_count == 3, "the party of 2 and two others fill 2V2: pool of %d",
		machines[0].search.pool_count);
	CHECK(!hosts, "a strict leader never takes the party's host");
}

static int pool_names(const struct mm_record *record, const unsigned char *key)
{
	unsigned char x25519[P2P_KEY_SIZE], hash[P2P_KEY_HASH_SIZE];
	int index;

	p2p_ed25519_to_x25519(key, x25519);
	p2p_key_hash(x25519, hash);
	for (index = 0; index < record->pool_count; index++)
		if (!memcmp(record->pool[index], hash, P2P_KEY_HASH_SIZE))
			return index + 1;
	return 0;
}

static void check_party_pool_invited(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	struct mm_record read_back, heard, own, other = party_of(200, 1, 300);
	struct mm_search *search;
	unsigned char token[P2P_TOKEN_SIZE], seed[P2P_SEED_SIZE], opened[P2P_TOKEN_SIZE];
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int count, index, found = 0;

	case_name = "party_pool_invited";
	world(1);
	search = &machines[0].search;
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 1);
	own = party_of(100, 1, 1);
	set_party(search, &own, 0);
	memset(token, 7, sizeof(token));
	for (index = 0; index < 3; index++)
		heard_party(search, 200, 1, 300 + (unsigned long)index, P2P_MM_SLAYER, P2P_MM_3V3);
	memcpy(search->pool[0], search->key_hash, P2P_KEY_HASH_SIZE);
	other.playlist = P2P_MM_SLAYER;
	other.size = P2P_MM_3V3;
	seed_of(200, seed);
	CHECK(mm_record_read(bytes, mm_record_write(&other, seed, bytes, sizeof(bytes)), &heard), "read");
	memcpy(search->pool[1], heard.key_hash, P2P_KEY_HASH_SIZE);
	search->pool_count = 2;
	search->hosting_playlist = P2P_MM_SLAYER;
	search->hosting_size = P2P_MM_3V3;
	search->state = P2P_MM_FOUND;
	search->decided = P2P_MM_DECIDE_HOST;
	mm_hosted(search, token, 1, 6, 0, now_ms);
	count = mm_step(search, now_ms, out, MM_MAXIMUM_OUT);
	for (index = 0; index < count; index++)
		found |= mm_record_read(out[index].bytes, out[index].size, &read_back) && read_back.kind == MM_KIND_HOSTING;
	CHECK(found && read_back.pool_count == 3, "the host's own member and the other party's two are invited: %d",
		read_back.pool_count);
	CHECK(pool_names(&read_back, own.member_keys[0]) && pool_names(&read_back, other.key) &&
		pool_names(&read_back, other.member_keys[0]), "each by its own key");
	seed_of(201, seed);
	index = pool_names(&read_back, other.member_keys[0]) - 1;
	CHECK(index >= 0 && mm_open_token(seed, read_back.key, read_back.sealed[index], opened) &&
		!memcmp(opened, token, P2P_TOKEN_SIZE), "the other party's member opens its own token");
}

static void check_party_records(void)
{
	enum { HEADER = MM_HEADER_SIZE + 1 };
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE + 1];
	struct mm_record record = party_of(300, 2, 7), full = party_of(300, 3, 7), big = party_of(300, MM_MAXIMUM_PARTY - 1, 7), bad, read_back;
	unsigned char x25519[P2P_KEY_SIZE];
	int size, index;

	case_name = "e42_member_signatures";
	seed_of(300, seed);
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	CHECK(size == HEADER + 2 * MM_MEMBER_SIZE + P2P_SIGNATURE_SIZE && bytes[2] == MM_FORMAT_PARTY && bytes[MM_HEADER_SIZE] == 3,
		"format 2, N and two members: %d bytes", size);
	CHECK(mm_record_read(bytes, size, &read_back) && read_back.party == 3 &&
		!memcmp(read_back.member_keys, record.member_keys, 2 * P2P_KEY_SIZE), "a party of 3 read back");

	bad = record;
	seed_of(303, seed);
	mm_party_sign(seed, record.key, bad.member_signatures[1]);
	CHECK(party_refused(&bad), "E42: a member's signature that is another key's is refused");
	bad = record;
	memset(bad.member_signatures[0], 0, P2P_SIGNATURE_SIZE);
	CHECK(party_refused(&bad), "an unsigned member is refused");
	bad = record;
	party_key(301, bad.member_keys[1]);
	memcpy(bad.member_signatures[1], bad.member_signatures[0], P2P_SIGNATURE_SIZE);
	CHECK(party_refused(&bad), "a member named twice is refused");
	bad = record;
	memcpy(bad.member_keys[0], bad.key, P2P_KEY_SIZE);
	seed_of(300, seed);
	mm_party_sign(seed, bad.key, bad.member_signatures[0]);
	CHECK(party_refused(&bad), "the leader named as a member is refused");
	bad = party_of(304, 2, 7);
	memcpy(bad.key, record.key, P2P_KEY_SIZE);
	CHECK(party_refused(&bad), "signatures made for another leader are refused");
	bad = record;
	memcpy(bad.member_keys[1], bad.member_keys[0], P2P_KEY_SIZE);
	bad.member_keys[1][P2P_KEY_SIZE - 1] ^= 0x80;
	CHECK(party_refused(&bad) && verifies == 2, "a member's negative is the same machine, unchecked: %d signatures", verifies);
	bad = record;
	memset(bad.member_keys[1], 0, P2P_KEY_SIZE);
	CHECK(!p2p_ed25519_to_x25519(bad.member_keys[1], x25519), "the zero key has no X25519 form");
	CHECK(party_refused(&bad) && verifies == 2, "a member with no X25519 form goes unchecked: %d signatures", verifies);

	seed_of(300, seed);
	size = mm_record_write(&full, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	CHECK(mm_record_read(bytes, size, &read_back) && read_back.party == 4, "the largest party a queue takes read back");
	size = mm_record_write(&big, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	CHECK(size == MM_MAXIMUM_PARTY_SIZE && !mm_record_read(bytes, size, &read_back),
		"the largest party the format holds is written and 4v4 refuses it: %d bytes", size);
	bad = big;
	bad.party = MM_MAXIMUM_PARTY + 1;
	CHECK(!mm_record_write(&bad, seed, bytes, MM_MAXIMUM_RECORD_SIZE), "a party too large is not written");
	bad = record;
	bad.kind = MM_KIND_HOSTING;
	CHECK(!mm_record_write(&bad, seed, bytes, MM_MAXIMUM_RECORD_SIZE), "only a searching record names a party");

	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	CHECK(!mm_record_read(bytes, size - 1, &read_back), "cut short");
	bytes[size] = 0;
	resign(300, bytes, size + 1);
	CHECK(!mm_record_read(bytes, size + 1, &read_back), "a byte too long");
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	for (index = 0; index < 2; index++)
	{
		bytes[MM_HEADER_SIZE] = (unsigned char)index;
		resign(300, bytes, HEADER + P2P_SIGNATURE_SIZE);
		CHECK(!mm_record_read(bytes, HEADER + P2P_SIGNATURE_SIZE, &read_back), "a count of %d is refused", index);
	}
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	bytes[MM_HEADER_SIZE] = 4;
	resign(300, bytes, size);
	CHECK(!mm_record_read(bytes, size, &read_back), "a count past the bytes is refused");
	bytes[MM_HEADER_SIZE] = 3;
	bytes[5] = MM_KIND_CANCEL;
	resign(300, bytes, size);
	CHECK(!mm_record_read(bytes, size, &read_back), "a cancel in format 2 is refused");
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	bytes[size - 1] ^= 1;
	CHECK(!mm_record_read(bytes, size, &read_back), "unsigned by the leader");
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	bytes[2] = MM_FORMAT;
	resign(300, bytes, size);
	CHECK(!mm_record_read(bytes, size, &read_back), "format 1 with a party's bytes is refused");
	record = searching(300, 7);
	size = mm_record_write(&record, seed, bytes, MM_MAXIMUM_RECORD_SIZE);
	CHECK(size == MM_HEADER_SIZE + P2P_SIGNATURE_SIZE && bytes[2] == MM_FORMAT && mm_record_read(bytes, size, &read_back) &&
		read_back.party == 1, "a solo's record stays format 1");

	case_name = "party_no_signature";
	bad = party_of(300, 2, 7);
	bad.size = P2P_MM_2V2;
	CHECK(party_refused(&bad) && !verifies, "a party too large for its queue spends no signature: %d", verifies);
	CHECK(party_refused(&big) && !verifies, "a party of 8 on 4v4 spends no signature: %d", verifies);
}

static void check_pool_never_hosts(void)
{
	int low = lowest_key_hash(4), index;

	case_name = "pool_never_hosts";
	world(4);
	ignore_first_host = 1;
	start_searching(low, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(111.0);
	for (index = 0; index < 4; index++)
		if (index != low)
			start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(39.0);
	CHECK(ignored >= 0, "a host was elected");
	for (index = 0; index < 4; index++)
		CHECK(index == ignored || machines[index].in >= 0 || machines[index].hosting,
			"machine %d hosts or joins within the host wait past 2:00", index);
}

static void check_worlds(void)
{
	check_four();
	check_two_playlists();
	check_ffa_leaves_ctf();
	check_lost_cancel();
	check_cancel_mid_join();
	check_never_joins();
	check_stun_unknown();
	check_quit();
	check_reconnect();
	check_replay();
	check_two_refreshes();
	check_late_listener();
	check_full_table();
	check_search_after_cancel();
	check_restart();
	check_left_queue();
	check_failed_join();
	check_alone();
	check_widen_now();
	check_host_now();
	check_strict();
	check_full_match();
	check_widened_retry();
	check_strict_lowest();
	check_pool_forming();
	check_host_now_before_join();
	check_big_hosts();
	check_big_lost();
	check_big_records();
	check_small_hands_over();
	check_widen_no_big();
	check_big_ffa_no_ctf();
}

static void check_region(void)
{
	static const struct { const char *zone, *region; } offsets[] = {
		{ "UTC+12", "" }, { "UTC+11", "" }, { "UTC+10", "na-west" }, { "UTC+7", "na-west" }, { "UTC+6", "na-east" },
		{ "UTC+4", "na-east" }, { "UTC+3:30", "" }, { "UTC+2", "" }, { "UTC+1", "eu" }, { "UTC0", "eu" },
		{ "UTC-4", "eu" }, { "UTC-4:30", "" }, { "UTC-5:30", "" }, { "UTC-7", "" }, { "UTC-8", "oce" }, { "UTC-14", "oce" },
		/* standard +3, daylight saving +5, all year */
		{ "AAA-3BBB-5,J1/0,J365/24", "eu" },
	};
	size_t index;

	for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++)
	{
		setenv("TZ", offsets[index].zone, 1);
		tzset();
		CHECK(!strcmp(p2p_mm_region(""), offsets[index].region), "the region of %s: \"%s\"", offsets[index].zone, p2p_mm_region(""));
	}
	CHECK(!strcmp(p2p_mm_region("any"), "") && !strcmp(p2p_mm_region("ANY"), ""), "any is the worldwide path");
	CHECK(!strcmp(p2p_mm_region("na-west"), "na-west"), "a set region stays");
	unsetenv("TZ");
	tzset();
}

static void check_faults(void)
{
	check_invited_twice();
	check_passed_over_hosts();
	check_host_quits_lobby();
	check_never_hosts();
	check_return();
	check_two_lone_hosts();
	check_lone_merge();
	check_big_hold();
	check_host_failed();
	check_two_queues();
	check_pool_never_hosts();
}

static struct mm_party parties[P2P_PARTY_MAXIMUM + 2];
static unsigned long party_now;
static int party_silent;

static void party_seed(int machine, unsigned char *seed)
{
	seed_of(machine + 400, seed);
}

static int party_quit(int index)
{
	return machines[index].quit;
}

static void party_send(int machines, const char *topic, const unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < machines; index++)
	{
		struct mm_party_record record;

		if (index < party_machines && party_quit(index))
			continue;
		if (!strcmp(parties[index].topic, topic) && mm_party_read(&parties[index], bytes, size, &record) &&
			mm_party_verify(&record))
		{
			mm_party_heard(&parties[index], &record, party_now);
		}
	}
}

static void party_run(int machines, int half_seconds)
{
	while (half_seconds--)
	{
		int index;

		party_now += 500;
		for (index = 0; index < machines; index++)
		{
			unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
			char topic[P2P_PARTY_TOPIC_SIZE];
			int size;

			if (index == party_silent)
				continue;
			strcpy(topic, parties[index].topic);
			size = mm_party_step(&parties[index], party_now, bytes);
			if (size)
				party_send(machines, topic, bytes, size);
		}
	}
}

static void party_form(int machines)
{
	static const char *const names[] = { "CHIEF", "ROOK", "KAT", "JUN", "EMILE", "JORGE", "CARTER", "NOBLE", "SIX" };
	unsigned char seed[P2P_SEED_SIZE], secret[P2P_PARTY_SECRET_SIZE];
	int index;

	memset(parties, 0, sizeof(parties));
	memset(secret, 0x31, sizeof(secret));
	party_now = 1000000;
	party_silent = -1;
	for (index = 0; index < machines; index++)
		strcpy(parties[index].name, names[index]);
	party_seed(0, seed);
	mm_party_make(&parties[0], seed, secret, party_now);
	for (index = 1; index < machines; index++)
	{
		party_seed(index, seed);
		mm_party_join(&parties[index], seed, parties[0].link, party_now);
	}
	party_run(machines, 12);
}

static void party_machine_step(int index)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	char topic[P2P_PARTY_TOPIC_SIZE];
	int size;

	party_now = now_ms;
	mm_party_search(&parties[index], &machines[index].search, now_ms, now_unix);
	strcpy(topic, parties[index].topic);
	size = mm_party_step(&parties[index], now_ms, bytes);
	if (size)
		party_send(party_machines, topic, bytes, size);
}

static void party_world(int members, int total)
{
	world(total);
	party_form(members);
	now_ms = party_now;
	party_machines = members;
}

/* can_host: a bit for each machine */
static void party_start(int members, int size, unsigned can_host)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, size };
	struct mm_search *search = &machines[0].search;
	unsigned char seed[P2P_SEED_SIZE];
	int index;

	for (index = 1; index < members; index++)
		machines[index].search.can_host = can_host >> index & 1;
	party_seed(0, seed);
	mm_start(search, seed, &choice, now_ms, now_unix);
	search->state = P2P_MM_QUEUED;
	search->can_host = can_host & 1;
	mm_party_search_begin(&parties[0], search, now_ms);
}

static int party_size(void)
{
	return machines[0].search.party.count;
}

static void check_e24_leader_cancels(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	struct p2p_party_status status;

	case_name = "e24_leader_cancels";
	party_world(3, 3);
	party_start(3, P2P_MM_4V4, 7);
	run(8);
	CHECK(party_size() == 3, "the leader's records carry 3: %d", party_size());
	CHECK(machines[1].search.state == P2P_MM_QUEUED && machines[2].search.state == P2P_MM_QUEUED, "the members follow");
	CHECK(!machines[1].search.next_queue && machines[1].search.party.following, "and publish no search of their own");
	mm_party_status(&parties[1], &status);
	CHECK(status.choice.size == P2P_MM_4V4 && status.choice.playlists == 1u << P2P_MM_SLAYER, "a member reads the choice");
	publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
	run(4);
	CHECK(machines[1].search.state == P2P_MM_OFF && machines[2].search.state == P2P_MM_OFF, "every member's state OFF");
	CHECK(parties[0].count == 3 && parties[1].role == MM_PARTY_MEMBER && parties[2].role == MM_PARTY_MEMBER,
		"the party intact");
}

static void check_e23_member_quits(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	char topic[P2P_PARTY_TOPIC_SIZE];
	unsigned long started;
	int index, size;

	case_name = "e23_member_quits";
	party_world(3, 7);
	party_start(3, P2P_MM_3V3, 1);
	for (index = 3; index < 5; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 0);
	run(8);
	CHECK(party_size() == 3, "3 at first: %d", party_size());
	started = machines[0].search.started;
	machines[1].quit = 1;
	run(10);
	CHECK(party_size() == 2, "N-1 within 10 s: %d", party_size());
	CHECK(machines[0].search.started == started, "the start kept");
	for (index = 5; index < 7; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 0);
	run(30);
	CHECK(machines[0].hosting && machines[0].players == 6, "a pool not yet settled forms again: %d in the game",
		machines[0].players);

	party_world(3, 3);
	party_start(3, P2P_MM_4V4, 7);
	run(8);
	strcpy(topic, parties[2].topic);
	size = mm_party_leave(&parties[2], bytes);
	party_send(3, topic, bytes, size);
	run(3);
	CHECK(party_size() == 2, "LEAVE PARTY: within 3 s: %d", party_size());
	CHECK(machines[2].search.state == P2P_MM_OFF, "who leaves stops searching");
	CHECK(machines[1].search.state == P2P_MM_QUEUED, "the other member searches on");
}

static void check_e60_member_b(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	unsigned long started;

	case_name = "e60_member_b";
	party_world(3, 3);
	party_start(3, P2P_MM_4V4, 7);
	run(8);
	started = machines[0].search.started;
	mm_party_search_left(&parties[1], now_ms);
	mm_cancel(&machines[1].search, out, MM_MAXIMUM_OUT);
	run(3);
	CHECK(party_size() == 2, "N-1 within 3 s: %d", party_size());
	CHECK(machines[0].search.started == started, "the start kept");
	CHECK(parties[0].count == 3 && parties[1].role == MM_PARTY_MEMBER, "still in the party");
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_LEFT_SEARCH, "the leader told: %d", parties[0].notice);
	run(6);
	CHECK(machines[1].search.state == P2P_MM_OFF, "the member stays out of this search");
	publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
	run(4);
	party_start(3, P2P_MM_4V4, 7);
	run(8);
	CHECK(party_size() == 3 && machines[1].search.party.following, "the leader's next SEARCH takes the member again");
}

static void check_e30_leader_quits(void)
{
	case_name = "e30_leader_quits";
	party_world(3, 3);
	party_start(3, P2P_MM_4V4, 7);
	run(8);
	machines[0].quit = 1;
	run(14);
	CHECK(parties[1].notice == P2P_PARTY_NOTICE_ENDED, "ENDED: %d", parties[1].notice);
	CHECK(parties[1].role == MM_PARTY_NONE && parties[2].role == MM_PARTY_NONE, "solo");
	CHECK(machines[1].search.state == P2P_MM_OFF && machines[2].search.state == P2P_MM_OFF, "not searching");
}

static void check_e47_strict_leader(void)
{
	case_name = "e47_strict_leader";
	party_world(2, 2);
	party_start(2, P2P_MM_2V2, 2);
	run(100);
	CHECK(!machines[1].hosting, "no host before 2:00");
	run(40);
	CHECK(machines[1].hosting, "the member hosts");
	CHECK(machines[0].in == 1, "the leader joins");
	CHECK(machines[0].search.in_game && !parties[0].notice, "and the party stays together: notice %d", parties[0].notice);
}

static void check_e47_big_strict_leader(void)
{
	static const int sizes[] = { P2P_MM_BIG_TEAM, P2P_MM_BIG_FFA };
	int index;

	case_name = "e47_big_strict_leader";
	for (index = 0; index < 2; index++)
	{
		party_world(3, 3);
		party_start(3, sizes[index], 6);
		run(20);
		CHECK(machines[1].hosting && !machines[2].hosting, "size %d: the first member who can hosts: %d and %d",
			sizes[index], machines[1].hosting, machines[2].hosting);
		run(20);
		CHECK(machines[0].in == 1 && machines[2].in == 1, "size %d: the leader and the other member join it: in %d and %d",
			sizes[index], machines[0].in, machines[2].in);
	}
}

static void check_e25_big_follow_count(void)
{
	int step;

	case_name = "e25_big_follow_count";
	party_world(2, 2);
	party_start(2, P2P_MM_BIG_TEAM, 1);
	for (step = 0; step < 400 && machines[1].search.state != P2P_MM_FOUND; step++)
		run(0.1);
	CHECK(machines[1].search.state == P2P_MM_FOUND, "the member follows the leader into its game");
	CHECK(!machines[1].search.joining_most_players, "a big game's most players is not known to the member: %d",
		machines[1].search.joining_most_players);
}

static void check_small_follow_count(void)
{
	int step;

	case_name = "small_follow_count";
	party_world(2, 4);
	party_start(2, P2P_MM_2V2, 1);
	start_searching(2, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	start_searching(3, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	for (step = 0; step < 400 && machines[1].search.state != P2P_MM_FOUND; step++)
		run(0.1);
	CHECK(machines[1].search.state == P2P_MM_FOUND, "the member follows the leader into its game");
	CHECK(!machines[1].search.joining_most_players, "the leader's pool is not known to the member: %d",
		machines[1].search.joining_most_players);
}

static void check_big_member_size(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_BIG_TEAM };
	struct p2p_mm_status status;
	unsigned char seed[P2P_SEED_SIZE], leader[P2P_KEY_SIZE];

	case_name = "big_member_size";
	seed_of(30, seed);
	p2p_ed25519_public(seed, leader, NULL);
	seed_of(31, seed);
	pthread_mutex_lock(&p2p_lock);
	mm_follow(p2p_mm_local(), seed, &choice, leader, 2, clock_now, 1800000000UL);
	pthread_mutex_unlock(&p2p_lock);
	p2p_mm_get_status(&status);
	CHECK(status.state == P2P_MM_QUEUED && status.size == P2P_MM_BIG_TEAM, "a big search's member has its size: %d",
		status.size);
	p2p_mm_cancel();
}

/* a game on the list, with its host running here so a join can reach it */
static void list_game(int host, int players, int most)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	char hash[2 * P2P_KEY_HASH_SIZE + 1];

	start_searching(host, 1u << P2P_MM_SLAYER, P2P_MM_4V4, 1);
	mm_cancel(&machines[host].search, out, MM_MAXIMUM_OUT);
	machines[host].hosting = 1;
	machines[host].players = players;
	machines[host].most = most;
	listing_count = 0;
	add_listing("listed", 2, players, most, 30);
	p2p_hex(machines[host].search.key_hash, P2P_KEY_HASH_SIZE, hash);
	snprintf(listings[0].invite, sizeof(listings[0].invite), "halo://join/%s", hash);
}

static void check_e25_follow_big(void)
{
	int index;

	case_name = "e25_follow_big";
	party_world(4, 5);
	list_game(4, 2, 8);
	party_start(4, P2P_MM_4V4, 15);
	for (index = 0; index < 10 && machines[0].in < 0; index++)
		run(1);
	CHECK(machines[0].in == 4, "the leader joins the listed game");
	run(20);
	for (index = 1; index < 4; index++)
		CHECK(machines[index].in == 4, "member %d followed within 20 s: in %d", index, machines[index].in);
	CHECK(machines[4].players == 6, "the party's 4 in the game: %d", machines[4].players);
	CHECK(!parties[0].notice, "no notice: %d", parties[0].notice);
	listing_count = 0;
}

static void check_e29_cannot_follow(void)
{
	int joined;

	case_name = "e29_cannot_follow";
	party_world(3, 4);
	list_game(3, 2, 8);
	listings[0].identifier[0] = 9;
	marked_count = 0;
	party_start(3, P2P_MM_4V4, 7);
	fail_join = 2;
	for (joined = 0; joined < 10 && machines[0].in < 0; joined++)
		run(1);
	CHECK(machines[0].in == 3, "the leader joins the listed game");
	run(4);
	CHECK(machines[1].in == 3, "one member follows");
	joined = machines[3].players;
	run(14);
	CHECK(machines[0].in == 3 && machines[3].players == joined, "the party waits 20 s");
	run(3);
	CHECK(machines[0].in < 0 && machines[1].in < 0 && machines[3].players == 2, "the party leaves it: %d in", machines[3].players);
	CHECK(marked_count == 1 && listings[0].failed, "marks it FAILED: %d", marked_count);
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_COULD_NOT_JOIN && !strcmp(parties[0].notice_name, "KAT"),
		"names who could not: %d %s", parties[0].notice, parties[0].notice_name);
	CHECK(machines[0].search.state != P2P_MM_OFF && machines[1].search.state == P2P_MM_QUEUED, "searches again");
	listing_count = 0;
}

/* way: 0 the leader cancels, 1 quits, 2 removes the member */
static void e75_leader_goes(int way)
{
	static const unsigned char secret[P2P_PARTY_SECRET_SIZE] = { 6 };
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	char topic[P2P_PARTY_TOPIC_SIZE];
	struct mm_out out[MM_MAXIMUM_OUT];
	struct mm_search *host = &machines[1].search;

	party_world(2, 3);
	party_start(2, P2P_MM_2V2, 2);
	run(140);
	CHECK(machines[1].hosting && machines[0].in == 1, "way %d: the member hosts with the leader in", way);
	if (way == 0)
	{
		publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
		machines[0].search.in_game = 0;
	}
	else if (way == 1)
	{
		machines[0].quit = 1;
		machines[1].players--;
	}
	else
	{
		strcpy(topic, parties[0].topic);
		party_send(2, topic, bytes, mm_party_remove(&parties[0], 1, secret, now_ms, bytes));
	}
	run(14);
	CHECK(host->state == P2P_MM_HOSTING && !host->party.following, "way %d: the member keeps hosting: state %d", way,
		host->state);
	start_searching(2, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	run(10);
	CHECK(machines[2].in == 1, "way %d: its game refills: in %d", way, machines[2].in);
	CHECK(host->state == P2P_MM_HOSTING, "way %d: and it hosts on: state %d", way, host->state);
}

static void check_e75_member_keeps_hosting(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	struct mm_search *member = &machines[1].search;
	int way;

	case_name = "e75_member_keeps_hosting";
	for (way = 0; way < 3; way++)
		e75_leader_goes(way);

	party_world(2, 3);
	party_start(2, P2P_MM_2V2, 2);
	run(140);
	publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
	machines[0].search.in_game = 0;
	run(4);
	parties[0].notice = P2P_PARTY_NOTICE_NONE;
	party_start(2, P2P_MM_2V2, 1);
	run(4);
	CHECK(party_size() == 1 && parties[0].notice == P2P_PARTY_NOTICE_LEFT_SEARCH,
		"the host is not counted in the leader's next search: %d, notice %d", party_size(), parties[0].notice);
	CHECK(member->state == P2P_MM_HOSTING, "and hosts on: state %d", member->state);

	party_world(2, 2);
	party_start(2, P2P_MM_2V2, 2);
	run(8);
	member->state = P2P_MM_FOUND;
	member->decided = P2P_MM_DECIDE_HOST;
	publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
	run(1);
	CHECK(member->state == P2P_MM_FOUND && !member->party.following, "a member starting its game keeps it: state %d",
		member->state);
}

static void check_party_flags(void)
{
	static const unsigned char secret[P2P_PARTY_SECRET_SIZE] = { 8 };
	static struct mm_search search;
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	case_name = "party flags";
	memset(parties, 0, sizeof(parties));
	memset(&search, 0, sizeof(search));
	party_machines = 0;
	party_now = 2000000;
	strcpy(parties[0].name, "CHIEF");
	strcpy(parties[1].name, "KAT");
	party_seed(0, seed);
	mm_party_make(&parties[0], seed, secret, party_now);
	mm_start(&search, seed, &choice, party_now, 1);
	party_seed(1, seed);
	mm_party_join(&parties[1], seed, parties[0].link, party_now);
	parties[1].sent = party_now - 3000;
	size = mm_party_step(&parties[1], party_now, bytes);
	party_send(1, parties[0].topic, bytes, size);
	CHECK(parties[0].count == 2, "the leader lists KAT");
	mm_party_search_begin(&parties[0], &search, party_now);
	CHECK(parties[0].members[1].searching, "the search counts KAT");
	parties[0].sent = party_now - 3000;
	size = mm_party_step(&parties[0], party_now, bytes);
	party_send(2, parties[1].topic, bytes, size);
	party_now += 3000;
	size = mm_party_step(&parties[1], party_now, bytes);
	party_send(1, parties[0].topic, bytes, size);
	CHECK(!parties[0].notice && parties[0].members[1].searching, "KAT's first record heard was the search: notice %d",
		parties[0].notice);

	mm_party_search_begin(&parties[0], &search, party_now);
	parties[0].sent = party_now - 3000;
	size = mm_party_step(&parties[0], party_now, bytes);
	party_send(2, parties[1].topic, bytes, size);
	mm_party_search_left(&parties[1], party_now);
	size = mm_party_step(&parties[1], party_now, bytes);
	party_send(1, parties[0].topic, bytes, size);
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_LEFT_SEARCH, "KAT leaving a search it had is told: %d",
		parties[0].notice);

	memset(&search, 0, sizeof(search));
	parties[1].sent = party_now;
	search.in_game = 1;
	mm_party_search(&parties[1], &search, party_now, 1);
	CHECK(mm_party_step(&parties[1], party_now, bytes) > 0, "a member's game goes out at once");

	memset(&search, 0, sizeof(search));
	search.state = P2P_MM_QUEUED;
	search.choice.size = search.decision.size = P2P_MM_2V2;
	parties[0].members[1].searching = 1;
	parties[0].members[1].in_game = 1;
	search.in_game = 1;
	mm_party_search(&parties[0], &search, party_now, 1);
	mm_party_search(&parties[0], &search, party_now, 1);
	search.in_game = 0;
	mm_party_search(&parties[0], &search, party_now, 1);
	parties[0].members[1].in_game = 1;
	search.in_game = 1;
	mm_party_search(&parties[0], &search, party_now, 1);
	parties[0].notice = 0;
	party_now += 21000;
	mm_party_search(&parties[0], &search, party_now, 1);
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_COULD_NOT_JOIN, "a second game asks again of the last one's players: %d",
		parties[0].notice);
}

static void check_e76_party_stays_together(void)
{
	int waited;

	case_name = "e76_party_stays_together";
	party_world(2, 2);
	party_start(2, P2P_MM_2V2, 2);
	fail_join = 0;
	run(125);
	CHECK(machines[1].hosting && machines[0].search.state == P2P_MM_QUEUED, "the leader cannot reach the member's game");
	run(14);
	CHECK(machines[1].hosting, "the member waits 20 s");
	run(10);
	CHECK(!machines[1].hosting, "then stops hosting");
	CHECK(machines[1].search.state == P2P_MM_QUEUED && machines[1].search.party.following, "and searches with the party");
	CHECK(machines[0].search.state == P2P_MM_QUEUED && party_size() == 2, "the party searches together: %d", party_size());
	CHECK(!parties[0].notice && parties[1].role == MM_PARTY_MEMBER, "intact: notice %d", parties[0].notice);
	fail_join = -1;
	run(130);
	CHECK(machines[1].hosting && machines[0].in == 1, "the game the member hosted is not failed: the leader joins its next");

	party_world(2, 3);
	party_start(2, P2P_MM_2V2, 2);
	fail_join = 0;
	run(125);
	CHECK(machines[1].hosting, "the member hosts");
	start_searching(2, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 0);
	for (waited = 0; waited < 20 && machines[2].in != 1; waited++)
		run(1);
	CHECK(machines[2].in == 1, "another player joins: in %d", machines[2].in);
	run(40);
	CHECK(machines[1].hosting && machines[1].players == 2 && machines[1].search.state == P2P_MM_HOSTING,
		"the member hosts them on: state %d", machines[1].search.state);
	machines[2].in = -1;
	machines[2].quit = 1;
	machines[1].players--;
	run(2);
	CHECK(!machines[1].hosting, "and stops once they have gone");

	party_world(3, 3);
	party_start(3, P2P_MM_4V4, 2);
	fail_join = 2;
	run(125);
	CHECK(machines[1].hosting && machines[0].in == 1, "the leader joins its member's game");
	run(30);
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_COULD_NOT_JOIN && !machines[1].hosting, "a member cannot: the party leaves");
	fail_join = -1;
	run(140);
	CHECK(machines[1].hosting && machines[0].in == 1 && machines[2].in == 1,
		"the member hosts again and the party joins: in %d %d", machines[0].in, machines[2].in);
}

static void friend_takes_link(int index, const char *name)
{
	unsigned char seed[P2P_SEED_SIZE];

	strcpy(parties[index].name, name);
	party_seed(index, seed);
	mm_party_join(&parties[index], seed, parties[0].link, now_ms);
	party_machines = index + 1;
}

static void check_e77_friend_joins_search(void)
{
	struct mm_search *const friend = &machines[1].search;
	int index, joined;

	case_name = "e77_friend_joins_search";
	party_world(1, 4);
	party_start(1, P2P_MM_2V2, 1);
	start_searching(2, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(3, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(19);
	CHECK(machines[2].search.state == P2P_MM_QUEUED, "three of four wait: state %d", machines[2].search.state);
	friend_takes_link(1, "KAT");
	run(3);
	CHECK(party_size() == 2 && friend->party.following, "KAT is in the search within 3 s: %d", party_size());
	run(30);
	for (index = joined = 0; index < 4; index++)
		joined += machines[index].hosting ? machines[index].players == 4 : 0;
	CHECK(joined == 1 && machines[1].in >= 0 && machines[1].in == machines[0].in, "the pool forms again with all four");
	CHECK(!parties[0].notice, "no notice: %d", parties[0].notice);

	party_world(1, 3);
	list_game(2, 2, 8);
	party_start(1, P2P_MM_4V4, 1);
	for (index = 0; index < 10 && machines[0].in < 0; index++)
		run(1);
	CHECK(machines[0].in == 2, "the leader is in a game");
	run(25);
	fail_join = 1;
	friend_takes_link(1, "KAT");
	run(25);
	CHECK(machines[0].in == 2 && !parties[0].notice, "a friend who cannot follow leaves the leader's game alone: %d",
		parties[0].notice);
	listing_count = 0;

	party_world(1, 3);
	list_game(2, 2, 8);
	party_start(1, P2P_MM_4V4, 1);
	for (index = 0; index < 10 && machines[0].in < 0; index++)
		run(1);
	run(5);
	friend_takes_link(1, "KAT");
	run(6);
	CHECK(machines[1].in == 2, "the friend follows the leader into its game: in %d", machines[1].in);
	CHECK(machines[0].in == 2 && !parties[0].notice, "and the leader stays: %d", parties[0].notice);
	listing_count = 0;

	party_world(1, 2);
	party_start(1, P2P_MM_2V2, 1);
	friend->in_game = 1;
	friend_takes_link(1, "KAT");
	run(6);
	CHECK(party_size() == 1 && !friend->party.following, "a friend in a game stays out: %d", party_size());
	CHECK(!parties[0].notice, "and it never had the search: %d", parties[0].notice);
}

static void check_e77_party_outgrows(void)
{
	struct mm_record record;
	int cancels = 0, index;

	case_name = "e77_party_outgrows";
	party_world(2, 3);
	party_start(2, P2P_MM_2V2, 3);
	run(8);
	CHECK(machines[1].search.party.following, "the member follows");
	capture_from = 0;
	captured_count = 0;
	friend_takes_link(2, "JUN");
	run(5);
	capture_from = -1;
	for (index = 0; index < captured_count; index++)
		cancels += mm_record_read(captured[index].bytes, captured[index].size, &record) &&
			record.kind == MM_KIND_CANCEL;
	CHECK(cancels > 0, "the queue's cancel goes out: %d", cancels);
	for (index = 0; index < 3; index++)
	{
		CHECK(machines[index].search.state == P2P_MM_OFF, "machine %d stops: state %d", index, machines[index].search.state);
		CHECK(parties[index].notice == P2P_PARTY_NOTICE_TOO_BIG, "machine %d is told: %d", index, parties[index].notice);
	}
	CHECK(parties[0].count == 3 && parties[2].role == MM_PARTY_MEMBER, "the friend stays in the party: %d", parties[0].count);
	run(150);
	CHECK(!machines[0].hosting && !machines[1].hosting && !machines[2].hosting, "and nobody hosts it");

	party_start(3, P2P_MM_4V4, 7);
	run(5);
	CHECK(party_size() == 3 && !parties[0].notice && !parties[1].notice && !parties[2].notice,
		"a bigger game takes the three, the notice gone: %d %d", party_size(), parties[1].notice);

	party_world(1, 2);
	party_start(1, P2P_MM_1V1, 1);
	run(130);
	CHECK(machines[0].hosting, "the leader hosts its 1V1");
	friend_takes_link(1, "KAT");
	run(5);
	CHECK(!machines[0].hosting && machines[0].search.state == P2P_MM_OFF && machines[1].in < 0, "a lobby stops too");
	CHECK(parties[1].notice == P2P_PARTY_NOTICE_TOO_BIG, "and the friend is told: %d", parties[1].notice);

	party_world(2, 4);
	list_game(3, 1, 4);
	party_start(2, P2P_MM_2V2, 3);
	for (index = 0; index < 10 && machines[0].in < 0; index++)
		run(1);
	CHECK(machines[0].in == 3, "the leader is in a 2V2: in %d", machines[0].in);
	run(10);
	friend_takes_link(2, "JUN");
	run(10);
	CHECK(machines[0].in == 3 && machines[2].in < 0 && !machines[2].search.party.following,
		"a game goes on without the friend: in %d %d", machines[0].in, machines[2].in);
	CHECK(parties[2].notice == P2P_PARTY_NOTICE_TOO_BIG, "who is told: %d", parties[2].notice);
	listing_count = 0;

	world(1);
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_1V1, 1);
	machines[0].search.party.count = 2;
	run(130);
	CHECK(!machines[0].hosting, "host_open keeps to sizes the party fits");
}

static void check_member_stays_in_game(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	int index;

	case_name = "member_stays_in_game";
	party_world(2, 3);
	list_game(2, 2, 8);
	party_start(2, P2P_MM_4V4, 3);
	for (index = 0; index < 20 && machines[1].in < 0; index++)
		run(1);
	CHECK(machines[0].in == 2 && machines[1].in == 2, "both are in the game");
	publish(0, out, mm_cancel(&machines[0].search, out, MM_MAXIMUM_OUT));
	machines[0].in = -1;
	machines[2].players--;
	run(4);
	CHECK(machines[1].search.in_game && machines[1].in == 2, "the member stays in the game");
	parties[0].notice = P2P_PARTY_NOTICE_NONE;
	party_start(2, P2P_MM_4V4, 3);
	run(4);
	CHECK(party_size() == 1 && parties[0].notice == P2P_PARTY_NOTICE_LEFT_SEARCH,
		"and is not counted in the leader's next search: %d, notice %d", party_size(), parties[0].notice);
	CHECK(machines[1].search.state == P2P_MM_OFF, "nor follows it: state %d", machines[1].search.state);
	run(25);
	CHECK(parties[0].notice != P2P_PARTY_NOTICE_COULD_NOT_JOIN, "the leader does not wait for it: %d", parties[0].notice);
	listing_count = 0;
}

static void check_party_journeys(void)
{
	check_e24_leader_cancels();
	check_e23_member_quits();
	check_e60_member_b();
	check_e30_leader_quits();
	check_e47_strict_leader();
	check_e47_big_strict_leader();
	check_e25_big_follow_count();
	check_small_follow_count();
	check_big_member_size();
	check_e75_member_keeps_hosting();
	check_e76_party_stays_together();
	check_e77_friend_joins_search();
	check_e77_party_outgrows();
	check_member_stays_in_game();
	check_e25_follow_big();
	check_e29_cannot_follow();
}

static void check_party_record_search(void)
{
	struct mm_party_search *said = &parties[0].search;
	int *const fields[] = { &said->game_playlist, &said->game_size, &said->size, &said->playlists };
	const int bad[] = { NUMBER_OF_P2P_MM_PLAYLISTS, NUMBER_OF_P2P_MM_SIZES, NUMBER_OF_P2P_MM_SIZES,
		1 << NUMBER_OF_P2P_MM_PLAYLISTS };
	int index;

	case_name = "party_record_search";
	party_form(3);
	party_run(3, 7);
	CHECK(parties[0].members[1].signature_heard && parties[0].members[2].signature_heard,
		"each member's record carries its signature for the leader's search");
	CHECK(mm_party_signed(parties[0].members[1].key, parties[0].key, parties[0].members[1].signature),
		"signed over the leader's key");
	parties[1].can_host = 1;
	parties[0].search.number = 7;
	parties[0].search.searching = 1;
	parties[0].search.playlists = 1 << P2P_MM_CTF;
	parties[0].search.size = P2P_MM_4V4;
	parties[0].search.has_game = 1;
	parties[0].search.game = 3;
	parties[0].search.game_playlist = P2P_MM_CTF;
	parties[0].search.game_size = P2P_MM_4V4;
	strcpy(parties[0].search.invite, "halo://join/0123");
	parties[2].search_heard = 0;
	party_run(3, 7);
	CHECK(parties[0].members[1].can_host && !parties[0].members[2].can_host, "the member's can-host bit");
	CHECK(parties[1].search.searching && parties[1].search.number == 7 && parties[1].search.size == P2P_MM_4V4,
		"the member reads the search");
	CHECK(parties[2].search.has_game && !strcmp(parties[2].search.invite, "halo://join/0123"),
		"each member opens the invite sealed to it: %s", parties[2].search.invite);
	CHECK(!parties[2].left_number && parties[2].followed_game == 3, "the first record heard leaves that search open");
	for (index = 0; index < 4; index++)
	{
		int *const field = fields[index], good = *field;

		*field = bad[index];
		parties[0].search.number++;
		party_run(3, 7);
		CHECK(parties[1].search.number != parties[0].search.number, "out of range field %d is dropped", index);
		*field = good;
		party_run(3, 7);
		CHECK(parties[1].search.number == parties[0].search.number, "the fixed record is read: field %d", index);
	}
}

static int party_lists(const struct mm_party *party, const char *name)
{
	struct p2p_party_status status;
	int index;

	mm_party_status(party, &status);
	for (index = 0; index < status.count; index++)
		if (!strcmp(status.members[index].name, name))
			return 1;
	return 0;
}

static void check_party_link(void)
{
	unsigned char seed[P2P_SEED_SIZE];
	char text[2 * P2P_PARTY_LINK_SIZE];
	struct mm_party party;

	case_name = "party link";
	party_form(2);
	CHECK(strlen(parties[0].link) == P2P_PARTY_LINK_SIZE - 1 && !strncmp(parties[0].link, "halo://party/", 13),
		"%s", parties[0].link);
	CHECK(strstr(parties[0].link, parties[0].topic + strlen(parties[0].topic) - 32) == NULL, "the link is not the topic");
	party_seed(2, seed);
	memset(&party, 0, sizeof(party));
	sprintf(text, "come along: HALO://PARTY/%s tonight", parties[0].link + 13);
	CHECK(mm_party_link_held(text) && mm_party_join(&party, seed, text, party_now), "a link among words, in any case");
	CHECK(!strcmp(party.topic, parties[0].topic), "the link's topic");
	memset(&party, 0, sizeof(party));
	strcpy(text, parties[0].link);
	text[strlen(text) - 1] = 0;
	CHECK(!mm_party_link_held(text) && !mm_party_join(&party, seed, text, party_now), "a link cut short");
	strcpy(text, parties[0].link);
	strcat(text, "0");
	CHECK(!mm_party_link_held(text) && !mm_party_join(&party, seed, text, party_now), "a link too long");
	CHECK(!mm_party_link_held("halo://join/00") && !mm_party_link_held(""), "a game's invite is no party's");
	memset(text, 0, sizeof(text));
	memcpy(text, parties[0].link, 13 + 32);
	memset(text + 45, '0', 64);
	CHECK(!mm_party_join(&party, seed, text, party_now), "a leader's key with no X25519 form");
	party_seed(0, seed);
	CHECK(!mm_party_join(&party, seed, parties[0].link, party_now), "a leader's own link");
	party_seed(2, seed);
	CHECK(!mm_party_join(&parties[0], seed, parties[1].link, party_now), "a leader with members stays");
	CHECK(!mm_party_join(&parties[1], seed, parties[0].link, party_now), "a member stays");
	memset(&party, 0, sizeof(party));
	party_seed(5, seed);
	mm_party_make(&party, seed, (const unsigned char *)"0123456789abcdef", party_now);
	CHECK(mm_party_join(&party, seed, parties[0].link, party_now) && party.role == MM_PARTY_MEMBER,
		"a leader alone joins another's party");
}

static void check_party_status(void)
{
	struct p2p_party_status status;

	case_name = "party status";
	party_form(3);
	mm_party_status(&parties[1], &status);
	CHECK(status.count == 3, "three in the party: %d", status.count);
	CHECK(!strcmp(status.members[0].name, "CHIEF") && status.members[0].leader && !status.members[0].you, "the leader first");
	CHECK(party_lists(&parties[1], "KAT") && party_lists(&parties[1], "ROOK"), "a member sees the others' names");
	CHECK(status.members[1].you != status.members[2].you && !status.members[1].leader, "this machine is marked");
	mm_party_status(&parties[0], &status);
	CHECK(status.count == 3 && status.members[0].you && status.members[0].leader, "the leader's own list");
}

static void check_party_leaked_link(void)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	struct mm_party stranger;
	int size;

	case_name = "e42_leaked_link";
	party_form(2);
	memset(&stranger, 0, sizeof(stranger));
	party_seed(7, seed);
	CHECK(mm_party_join(&stranger, seed, parties[0].link, party_now), "a stranger holds the link");
	stranger.role = MM_PARTY_LEADER;
	stranger.sequence = 1000;
	stranger.sent = party_now - 3000;
	strcpy(stranger.name, "FAKE");
	size = mm_party_step(&stranger, party_now, bytes);
	party_send(2, parties[1].topic, bytes, size);
	CHECK(strcmp(parties[1].members[0].name, "FAKE"), "E42: members ignore a record signed by another key");
	memcpy(stranger.key, parties[0].key, P2P_KEY_SIZE);
	stranger.sent = party_now - 3000;
	size = mm_party_step(&stranger, party_now, bytes);
	party_send(2, parties[1].topic, bytes, size);
	CHECK(strcmp(parties[1].members[0].name, "FAKE") != 0, "E42: the leader's key without its signature is ignored");
}

static void check_party_removed(void)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE], old_record[MM_MAXIMUM_RECORD_SIZE];
	static const unsigned char secret[P2P_PARTY_SECRET_SIZE] = { 9, 8, 7 };
	char old_topic[P2P_PARTY_TOPIC_SIZE], old_link[P2P_PARTY_LINK_SIZE];
	struct mm_party mallory, copy;
	int size, old_size;

	case_name = "e50_removed";
	party_form(3);
	strcpy(old_topic, parties[0].topic);
	strcpy(old_link, parties[0].link);
	copy = parties[2];
	copy.sent = party_now - 3000;
	old_size = mm_party_step(&copy, party_now, old_record);
	mallory = parties[1];
	party_seed(8, mallory.seed);
	CHECK(mm_party_remove(&parties[1], 1, secret, party_now, bytes) == 0, "a member removes nobody");
	CHECK(mm_party_remove(&parties[0], 0, secret, party_now, bytes) == 0, "the leader stays");
	CHECK(mm_party_remove(&parties[0], 3, secret, party_now, bytes) == 0, "a place past the list");
	CHECK(!strcmp(parties[0].members[2].name, "KAT"), "%s", parties[0].members[2].name);
	size = mm_party_remove(&parties[0], 2, secret, party_now, bytes);
	CHECK(size > 0 && size <= MM_MAXIMUM_RECORD_SIZE, "a new link's record: %d bytes", size);
	CHECK(strcmp(parties[0].topic, old_topic) && strcmp(parties[0].link, old_link), "the leader has a new link");
	party_send(3, old_topic, bytes, size);
	CHECK(parties[2].role == MM_PARTY_NONE && !parties[2].topic[0], "E50: Kat is out of the party");
	CHECK(parties[2].notice == P2P_PARTY_NOTICE_REMOVED && !strcmp(parties[2].notice_name, "CHIEF"),
		"E50: CHIEF REMOVED YOU: %d %s", parties[2].notice, parties[2].notice_name);
	CHECK(!strcmp(parties[1].topic, parties[0].topic), "E50: Rook moves to the new topic");
	{
		struct mm_party_record record;

		CHECK(mm_party_read(&mallory, bytes, size, &record) && mm_party_verify(&record), "the record reads on the old topic");
		mm_party_heard(&mallory, &record, party_now);
		CHECK(!strcmp(mallory.topic, old_topic), "E50: the new secret opens for its member alone");
	}
	party_send(3, parties[0].topic, old_record, old_size);
	party_send(3, old_topic, old_record, old_size);
	party_run(2, 6);
	CHECK(parties[0].count == 2 && !party_lists(&parties[0], "KAT") && !party_lists(&parties[1], "KAT"),
		"E50: Kat's record on the old topic reaches nobody's list");
	CHECK(parties[1].notice == P2P_PARTY_NOTICE_LEFT && !strcmp(parties[1].notice_name, "KAT"), "Rook is told");
	party_seed(2, seed);
	CHECK(mm_party_join(&parties[2], seed, old_link, party_now), "Kat opens the old link again");
	party_run(3, 6);
	CHECK(parties[0].count == 2 && !party_lists(&parties[0], "KAT"), "E50: the old link reaches nobody");
	party_run(3, 16);
	CHECK(parties[2].role == MM_PARTY_NONE && parties[2].notice == P2P_PARTY_NOTICE_ENDED, "a link nobody answers ends");
}

static void check_party_leaves(void)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE], late[MM_MAXIMUM_RECORD_SIZE];
	char topic[P2P_PARTY_TOPIC_SIZE];
	struct mm_party copy;
	int size, late_size;

	case_name = "e61_leave_party";
	party_form(3);
	strcpy(topic, parties[2].topic);
	copy = parties[2];
	copy.sent = party_now - 3000;
	late_size = mm_party_step(&copy, party_now, late);
	CHECK(mm_party_leave(&parties[0], bytes) == 0, "the leader has no LEAVE PARTY");
	size = mm_party_leave(&parties[2], bytes);
	party_send(3, topic, bytes, size);
	CHECK(parties[2].role == MM_PARTY_NONE && !parties[2].topic[0] && parties[2].notice == P2P_PARTY_NOTICE_NONE,
		"E61: the member is solo");
	CHECK(parties[0].count == 2 && !party_lists(&parties[0], "KAT"), "E61: the leader's party is smaller at once");
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_LEFT && !strcmp(parties[0].notice_name, "KAT"), "KAT LEFT THE PARTY");
	party_run(3, 6);
	CHECK(parties[1].count == 2 && !party_lists(&parties[1], "KAT"), "E61: the others' lists within 3 s");
	party_send(3, topic, late, late_size);
	CHECK(parties[0].count == 2, "a copy that comes late from another broker brings nobody back");
	party_seed(2, seed);
	CHECK(mm_party_join(&parties[2], seed, parties[0].link, party_now), "Kat comes back");
	party_run(3, 6);
	CHECK(parties[0].count == 3 && party_lists(&parties[0], "KAT"), "a member who left may join again");
}

static void check_party_expiry(void)
{
	unsigned long heard;

	case_name = "member_expiry";
	party_form(3);
	party_silent = 2;
	heard = parties[0].members[2].heard;
	while (parties[0].count == 3 && party_now - heard < 30000)
		party_run(3, 1);
	CHECK(party_now - heard == 10000 || party_now - heard == 10500, "E23: out of the list after 10 s: %lu",
		party_now - heard);
	CHECK(parties[0].count == 2 && !party_lists(&parties[0], "KAT"), "E23: Kat is gone");
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_LEFT && !strcmp(parties[0].notice_name, "KAT"), "KAT LEFT THE PARTY");
	party_run(3, 6);
	CHECK(parties[1].count == 2, "the others follow the leader's list");
	party_silent = -1;
	party_run(3, 6);
	CHECK(parties[0].count == 3, "a member heard again is back");

	party_form(3);
	party_silent = 0;
	heard = parties[1].members[0].heard;
	while (parties[1].role != MM_PARTY_NONE && party_now - heard < 30000)
		party_run(3, 1);
	CHECK(party_now - heard == 10000 || party_now - heard == 10500, "E30: the party ends after 10 s: %lu",
		party_now - heard);
	CHECK(!parties[1].topic[0], "E30: the member leaves the topic");
	CHECK(parties[1].notice == P2P_PARTY_NOTICE_ENDED && !strcmp(parties[1].notice_name, "CHIEF"),
		"THE PARTY ENDED: CHIEF LEFT");
}

static void check_party_sealed(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE], secret[P2P_PARTY_SECRET_SIZE], digest[P2P_SHA256_SIZE];
	unsigned char plain[MM_MAXIMUM_RECORD_SIZE];
	int size, index;

	case_name = "party sealed";
	party_form(2);
	parties[0].sent = party_now - 3000;
	size = mm_party_step(&parties[0], party_now, bytes);
	CHECK(size > 0, "a record");
	memset(secret, 0x31, sizeof(secret));
	p2p_sha256(secret, sizeof(secret), digest);
	CHECK(p2p_open(digest, bytes, size, plain) < 0, "the hash a watcher reads in the topic does not open it");
	for (index = 0; index + 5 <= size; index++)
		CHECK(memcmp(bytes + index, "CHIEF", 5), "no name in the clear");
}

static void check_party_copies(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	case_name = "party copies";
	party_form(2);
	parties[0].sent = party_now - 3000;
	size = mm_party_step(&parties[0], party_now, bytes);
	verifies = 0;
	party_send(2, parties[0].topic, bytes, size);
	CHECK(verifies == 1, "one signature for the first copy: %d", verifies);
	party_send(2, parties[0].topic, bytes, size);
	CHECK(verifies == 1, "none for a copy from another broker: %d", verifies);
	parties[1].sent = party_now - 3000;
	size = mm_party_step(&parties[1], party_now, bytes);
	verifies = 0;
	party_send(2, parties[1].topic, bytes, size);
	party_send(2, parties[1].topic, bytes, size);
	CHECK(verifies == 2, "a member's record too, with its search signature: %d", verifies);
}

static void check_party_full(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE], seed[P2P_SEED_SIZE];
	static const unsigned char secret[P2P_PARTY_SECRET_SIZE] = { 5 };
	char topic[P2P_PARTY_TOPIC_SIZE];
	int index, size;

	case_name = "party full";
	party_form(P2P_PARTY_MAXIMUM);
	memset(parties[3].name, 'J', P2P_PARTY_NAME_SIZE);
	party_run(P2P_PARTY_MAXIMUM, 6);
	CHECK(parties[0].count == P2P_PARTY_MAXIMUM && parties[7].count == P2P_PARTY_MAXIMUM, "a full party: %d %d",
		parties[0].count, parties[7].count);
	CHECK(party_lists(&parties[7], parties[3].name), "the longest name");
	parties[0].sent = party_now - 3000;
	size = mm_party_step(&parties[0], party_now, bytes);
	CHECK(size + 7 * (2 * P2P_KEY_HASH_SIZE + P2P_SEAL_OVERHEAD) <= MM_MAXIMUM_RECORD_SIZE,
		"a leader's record leaves room for each member's sealed invite: %d", size);
	party_send(P2P_PARTY_MAXIMUM, parties[0].topic, bytes, size);
	party_seed(8, seed);
	mm_party_join(&parties[8], seed, parties[0].link, party_now);
	strcpy(parties[8].name, "SIX");
	party_run(P2P_PARTY_MAXIMUM + 1, 6);
	CHECK(parties[0].count == P2P_PARTY_MAXIMUM && !party_lists(&parties[0], "SIX"), "a ninth is not taken");
	CHECK(parties[8].notice == P2P_PARTY_NOTICE_FULL && parties[8].role == MM_PARTY_NONE && !parties[8].topic[0],
		"the ninth is told and leaves: notice %d role %d", parties[8].notice, parties[8].role);
	CHECK(parties[1].role == MM_PARTY_MEMBER && !parties[1].notice, "the eight stay");
	memset(&parties[8], 0, sizeof(parties[8]));
	strcpy(topic, parties[0].topic);
	size = mm_party_remove(&parties[0], 7, secret, party_now, bytes);
	CHECK(size > 0 && size <= MM_MAXIMUM_RECORD_SIZE, "a full party's new link fits: %d bytes", size);
	party_send(P2P_PARTY_MAXIMUM, topic, bytes, size);
	for (index = 1; index < P2P_PARTY_MAXIMUM; index++)
		if (parties[index].role != MM_PARTY_NONE)
			CHECK(!strcmp(parties[index].topic, parties[0].topic), "member %d moves", index);
}

/* as the p2p thread calls them */
static void party_update_locked(void)
{
	pthread_mutex_lock(&p2p_lock);
	p2p_party_update();
	pthread_mutex_unlock(&p2p_lock);
}

static int party_link_locked(const char *text)
{
	int received;

	pthread_mutex_lock(&p2p_lock);
	received = p2p_party_link_received(text);
	pthread_mutex_unlock(&p2p_lock);
	return received;
}

static void check_party_machine(void)
{
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	char link[P2P_PARTY_LINK_SIZE], old_topic[P2P_PARTY_TOPIC_SIZE];
	struct p2p_party_status status;
	int size;

	case_name = "party machine";
	CHECK(!p2p_party_active(), "no party yet");
	p2p_party_set_name("CHIEF");
	CHECK(!p2p_party_invite(link, 8), "a link needs its room");
	CHECK(p2p_party_invite(link, sizeof(link)) && p2p_party_active(), "INVITE makes a party");
	CHECK(!strcmp(clipboard, link), "INVITE puts the link on the clipboard");
	CHECK(!p2p_party_can_join(link), "a leader's own link can't be joined, so it leaves its search running");
	CHECK(signal_topic[0], "the brokers carry the party's topic");
	party_update_locked();
	CHECK(party_published_size > 0, "the leader publishes");
	CHECK(!strcmp(offered_link, link) && offered_count == 1, "Discord offers the link: %d", offered_count);

	memset(parties, 0, sizeof(parties));
	party_seed(1, seed);
	strcpy(parties[1].name, "ROOK");
	CHECK(mm_party_join(&parties[1], seed, link, clock_now), "Rook holds the link");
	parties[1].sent = clock_now - 3000;
	size = mm_party_step(&parties[1], clock_now, bytes);
	p2p_party_heard(bytes, 0);
	p2p_party_heard(bytes, MM_MAXIMUM_RECORD_SIZE + 1);
	p2p_party_get_status(&status);
	CHECK(status.count == 1, "nothing is taken before its turn");
	party_update_locked();
	p2p_party_get_status(&status);
	CHECK(status.count == 1, "sizes outside a record are dropped");
	p2p_party_heard(bytes, size);
	p2p_party_heard(bytes, size);
	party_update_locked();
	p2p_party_get_status(&status);
	CHECK(status.count == 2 && !strcmp(status.members[1].name, "ROOK"), "a heard member joins: %d", status.count);
	CHECK(!strcmp(status.members[0].name, "CHIEF") && status.members[0].you, "the name set is shown");
	CHECK(offered_count == 2, "Discord's count follows: %d", offered_count);

	strcpy(old_topic, signal_topic);
	p2p_party_remove(1);
	CHECK(clipboard[0] && strcmp(clipboard, link), "the new link is offered as INVITE's is");
	CHECK(!strcmp(publish_topic, old_topic), "the new link goes out on the old topic");
	CHECK(strcmp(signal_topic, old_topic) && signal_topic[0], "and then the topic moves");
	memcpy(bytes, party_published, sizeof(bytes));
	size = party_published_size;
	clock_now += 3000;
	party_update_locked();
	CHECK(!strcmp(resent_topic, old_topic) && resent_size == size && !memcmp(resent, bytes, (size_t)size),
		"the new link goes out again for a member who missed it");
	resent_size = 0;
	clock_now += 9000;
	party_update_locked();
	CHECK(!resent_size, "and stops after ten seconds");
	CHECK(strcmp(offered_link, link), "Discord offers the new link");
	p2p_party_invite(link, sizeof(link));
	CHECK(!strcmp(clipboard, link) && !strcmp(link, offered_link), "INVITE gives the new link");

	memset(&parties[0], 0, sizeof(parties[0]));
	party_seed(0, seed);
	mm_party_make(&parties[0], seed, (const unsigned char *)"0123456789abcdef", clock_now);
	CHECK(!party_link_locked("no link here"), "text with no party link is left for the game's invites");
	CHECK(p2p_party_can_join(parties[0].link), "a lone leader may join another's party");
	CHECK(party_link_locked(parties[0].link), "a party link joins the party");
	CHECK(!p2p_party_can_join(parties[0].link), "a member may not join again");
	CHECK(!strcmp(signal_topic, parties[0].topic), "the brokers carry the new party's topic");
	CHECK(party_link_locked(parties[0].link), "a link for a party this machine is in is still a party link");
	strcpy(old_topic, signal_topic);
	p2p_party_leave();
	CHECK(!strcmp(publish_topic, old_topic) && party_published_size > 0, "the goodbye goes out on the party's topic");
	CHECK(!signal_topic[0] && !p2p_party_active(), "the machine leaves the topic");
	CHECK(!offered_link[0], "Discord stops offering a link");
}

static unsigned char window_record[MM_MAXIMUM_RECORD_SIZE];
static int window_member, window_size;

/* the game thread while p2p_party_update has the lock released */
static void remove_in_window(void)
{
	verify_window = NULL;
	clock_now += 5;
	party_published_size = 0;
	p2p_party_remove(window_member);
	memcpy(window_record, party_published, sizeof(window_record));
	window_size = party_published_size;
}

static void party_machine_hears(int member, int window)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	parties[member].sent = clock_now - 3000;
	size = mm_party_step(&parties[member], clock_now, bytes);
	p2p_party_heard(bytes, size);
	window_member = window;
	verify_window = window ? remove_in_window : NULL;
	party_update_locked();
}

static void check_party_verify_window(void)
{
	static const char *const names[] = { "ROOK", "KAT", "PIP" };
	unsigned char seed[P2P_SEED_SIZE];
	char link[P2P_PARTY_LINK_SIZE];
	struct p2p_party_status status;
	int index;

	case_name = "party verify window";
	CHECK(p2p_party_invite(link, sizeof(link)), "the machine leads");
	memset(parties, 0, sizeof(parties));
	for (index = 1; index <= 3; index++)
	{
		party_seed(index, seed);
		strcpy(parties[index].name, names[index - 1]);
		CHECK(mm_party_join(&parties[index], seed, link, clock_now), "%s holds the link", names[index - 1]);
		party_machine_hears(index, 0);
	}
	p2p_party_get_status(&status);
	CHECK(status.count == 4, "the party forms: %d", status.count);

	party_machine_hears(1, 2);
	p2p_party_get_status(&status);
	CHECK(status.count == 3 && !strcmp(status.members[2].name, "PIP"),
		"a removal while a record is checked takes only that member: %d", status.count);

	for (index = 1; index <= 3; index += 2)
	{
		struct mm_party_record record;

		CHECK(mm_party_read(&parties[index], window_record, window_size, &record), "the new link reads");
		mm_party_heard(&parties[index], &record, clock_now);
	}
	party_machine_hears(3, 2);
	p2p_party_get_status(&status);
	CHECK(status.count == 2, "a record being checked when its sender is removed does not bring them back: %d", status.count);
}

static void wiring_hears_leader(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	parties[0].sent = clock_now - 3000;
	size = mm_party_step(&parties[0], clock_now, bytes);
	p2p_party_heard(bytes, size);
	clock_now += 3000;
	party_update_locked();
}

static void wiring_leader_hears(void)
{
	party_send(1, parties[0].topic, party_published, party_published_size);
}

static void check_party_wiring(void)
{
	static const unsigned char secret[P2P_PARTY_SECRET_SIZE] = { 9 };
	static struct mm_search leading;
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_search *search = p2p_mm_local();
	unsigned char seed[P2P_SEED_SIZE];
	char link[P2P_PARTY_LINK_SIZE];
	int calls;

	case_name = "party wiring";
	party_machines = 0;
	memset(parties, 0, sizeof(parties));
	p2p_party_remove(1);
	p2p_party_invite(link, sizeof(link));
	party_seed(5, seed);
	strcpy(parties[5].name, "EMILE");
	mm_party_join(&parties[5], seed, link, clock_now);
	party_machine_hears(5, 0);
	choice.size = P2P_MM_1V1;
	p2p_mm_search(&choice);
	CHECK(search->state == P2P_MM_OFF && p2p_party_too_big(), "SEARCH refuses a size the party does not fit");
	choice.size = P2P_MM_2V2;
	p2p_mm_search(&choice);
	CHECK(search->state != P2P_MM_OFF && !p2p_party_too_big(), "one it fits goes, and the notice with it");
	party_update_locked();
	CHECK(search->party.count == 2, "a leader's search carries its member: %d", search->party.count);
	p2p_mm_cancel();
	p2p_party_remove(1);

	memset(parties, 0, sizeof(parties));
	party_seed(0, seed);
	strcpy(parties[0].name, "CHIEF");
	mm_party_make(&parties[0], seed, secret, clock_now);
	party_now = clock_now;
	CHECK(party_link_locked(parties[0].link) && p2p_party_is_member(), "the machine joins as a member");
	p2p_mm_search(&choice);
	CHECK(search->state == P2P_MM_OFF, "a member cannot search: state %d", search->state);
	clock_now += 3000;
	party_update_locked();
	wiring_leader_hears();
	CHECK(parties[0].count == 2, "the leader lists the machine: %d", parties[0].count);
	wiring_hears_leader();
	party_seed(0, seed);
	mm_start(&leading, seed, &choice, clock_now, 1);
	mm_party_search_begin(&parties[0], &leading, clock_now);
	wiring_hears_leader();
	CHECK(search->party.following, "the member follows the leader's search");
	party_published_size = 0;
	p2p_mm_cancel();
	party_update_locked();
	wiring_leader_hears();
	CHECK(parties[0].notice == P2P_PARTY_NOTICE_LEFT_SEARCH, "its cancel tells the leader: %d", parties[0].notice);

	calls = browse_calls;
	search->party.following = 1;
	search->left_game = 1;
	CHECK(p2p_mm_take_left_game() == 1 && !search->left_game, "the game thread takes the leaving");
	CHECK(!p2p_mm_take_left_game(), "once");
	CHECK(browse_calls == calls, "following, the browser is the leader's search's: %d", browse_calls - calls);
	search->in_game = 1;
	p2p_mm_game_lost();
	CHECK(browse_calls == calls, "and a lost game does not hold it: %d", browse_calls - calls);
	p2p_mm_cancel();
	calls = browse_calls;
	search->left_game = 1;
	CHECK(p2p_mm_take_left_game() == 1 && browse_calls == calls + 1 && browsing, "searching alone, the browser restarts");
	p2p_mm_cancel();
	p2p_party_leave();
	CHECK(!p2p_party_active(), "the machine is out of the party");
}

/* under loss two machines can elect themselves at once, so the lossy runs check only the end state */
int main(int argc, char **argv)
{
	(void)argv;
	check_pool();
	check_election();
	check_records();
	check_hosting();
	check_playlists();
	check_topics();
	check_party_topic();
	check_topic_moves();
	check_verify_budget();
	check_run_key();
	check_joined_then_lost();
	check_lost_in_verify();
	check_failed_join_waits();
	check_fake_fuller_game();
	check_look();
	check_big_look();
	check_big_listing();
	check_ffa_wanted();
	check_widened_pick();
	check_failed_pick();
	check_region();
	check_party_pool();
	check_party_search();
	check_party_record_search();
	check_party_journeys();
	check_party_pool_invited();
	check_party_sizes();
	check_party_records();
	check_party_link();
	check_party_status();
	check_party_leaked_link();
	check_party_removed();
	check_party_leaves();
	check_party_expiry();
	check_party_sealed();
	check_party_copies();
	check_party_full();
	check_party_flags();
	check_party_machine();
	check_party_verify_window();
	check_party_wiring();
	check_worlds();
	check_faults();
	if (argc > 1)
	{
		sweep_drop = 10;
		for (sweep_seed = 2; sweep_seed <= 31; sweep_seed++)
			check_faults();
	}
	printf(failures ? "FAILED %d\n" : "PASS\n", failures);
	return failures != 0;
}
