/* P2P_MATCHMAKING_CHECK.C: matchmaking's records, and machines searching through a stand-in broker */

#include "../port/linux/src/p2p_matchmaking.h"
#include "halo_port_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_OF_(x) #x
#define TEXT_OF(x) TEXT_OF_(x)
#define HALO_PORT_NETWORK_VERSION_TEXT TEXT_OF(HALO_PORT_NETWORK_VERSION)

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long clock_now = 1800000000UL;

void posix_random_bytes(void *buffer, unsigned long size) { memset(buffer, 0x5a, size); }
unsigned long p2p_now(void) { return clock_now; }
int config_boolean(const char *name) { (void)name; return 1; }
void platform_log(const char *format, ...) { (void)format; }

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + index * 2, "%02x", bytes[index]);
}

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

static unsigned char run_seed[P2P_SEED_SIZE] = { 0x77, 0x13 }, run_key[P2P_KEY_SIZE];

const unsigned char *p2p_signing_seed(void) { return run_seed; }
const unsigned char *p2p_signing_key(void)
{
	p2p_ed25519_public(run_seed, run_key, NULL);
	return run_key;
}
void p2p_signal_mm_topics(const char topics[][P2P_MM_TOPIC_SIZE], int count) { (void)topics; (void)count; }

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
	bytes[2] = MM_FORMAT + 1;
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

static void check_run_key(void)
{
	struct p2p_mm_choice choice = { 1u << P2P_MM_SLAYER, P2P_MM_2V2 };
	struct mm_record record;

	case_name = "run_key";
	published_size = 0;
	p2p_mm_search(&choice);
	pthread_mutex_lock(&p2p_lock);
	p2p_mm_update(clock_now, 1);
	pthread_mutex_unlock(&p2p_lock);
	CHECK(published_size && mm_record_read(published, published_size, &record), "a searching record is published");
	CHECK(published_size && !memcmp(record.key, p2p_signing_key(), P2P_KEY_SIZE),
		"the search is the run's identity: its key is the signing key");
	p2p_mm_cancel();
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
	count = mm_pool(units, 6, 4, pool);
	CHECK(count == 4, "E4: a pool of 4 from 6: %d", count);
	for (index = 0; index < count; index++)
		CHECK(hash_rank(units, 6, pool[index]) == index, "E4: the 4 lowest key hashes, in order: %d at %d",
			pool[index], index);
	for (index = 0; index < 6; index++)
		shuffled[index] = units[5 - index];
	CHECK(mm_pool(shuffled, 6, 4, again) == 4, "shuffled: a pool");
	for (index = 0; index < 4; index++)
		CHECK(!memcmp(units[pool[index]].key, shuffled[again[index]].key, P2P_KEY_SIZE), "shuffled: the same pool at %d", index);
	host = mm_elect(units, pool, count);
	host_again = mm_elect(shuffled, again, 4);
	CHECK(host >= 0 && host_again >= 0 && !memcmp(units[host].key, shuffled[host_again].key, P2P_KEY_SIZE),
		"E4: the same host on every machine");
	CHECK(mm_pool(units, 3, 4, pool) == 0, "3 of 4: no pool");
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
		every[index].failed = 1;
	every[MM_MAXIMUM_UNITS].can_host = 1;
	CHECK(mm_pool(every, MM_MAXIMUM_UNITS + 1, 1, pool) == 1 && pool[0] == MM_MAXIMUM_UNITS,
		"this machine and a full peers table: every unit is walked");
	/* four strict, then one that can host with the highest key hash: it can take only the last place */
	for (index = 0; index < 5; index++)
		units[index] = unit(index, 0);
	for (index = 0; index < 5; index++)
		if (hash_rank(units, 5, index) == 4)
			last = index;
	units[last].can_host = 1;
	CHECK(mm_pool(units, 5, 4, pool) == 4 && pool[3] == last && mm_elect(units, pool, 4) == last,
		"E38: the host takes the last place and hosts");
	units[last].can_host = 0;
	CHECK(mm_pool(units, 5, 4, pool) == 0, "E38: five strict routers form no pool");
	units[last].can_host = 1;
	units[last].failed = 1;
	CHECK(mm_pool(units, 5, 4, pool) == 0, "E12: the only host failed: no pool until another arrives");
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
	int in, hosting, players, most, quit;
};

static struct machine machines[8];
static int machine_count;
static unsigned long now_ms, now_unix;
static int drop_percent;
static int sweep_seed = 1, sweep_drop;
static int cancel_on_join, cancel_sent, never_joins = -1;
static int fail_join = -1, hosted_while_joining, joins_taken;
static int ignore_first_host, ignore_only = -1, ignored = -1, fail_first_host, failed_host = -1;
static int capture_from = -1, captured_count;
static struct mm_out captured[256], quit_out[MM_MAXIMUM_OUT];

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
		}
	}
}

static void world(int count)
{
	memset(machines, 0, sizeof(machines));
	machine_count = count;
	for (count = 0; count < 8; count++)
		machines[count].in = -1;
	ignore_first_host = fail_first_host = 0;
	ignored = failed_host = ignore_only = -1;
	now_ms = 1000;
	now_unix = 1800000000UL;
	drop_percent = flight_count = captured_count = cancel_on_join = cancel_sent = 0;
	never_joins = capture_from = fail_join = -1;
	hosted_while_joining = joins_taken = 0;
	drop_percent = sweep_drop;
	srand(sweep_seed);
}

static void start_searching(int index, unsigned playlists, int size, int can_host)
{
	struct p2p_mm_choice choice;
	unsigned char seed[P2P_SEED_SIZE];

	choice.playlists = playlists;
	choice.size = size;
	seed_of(100 + index, seed);
	mm_start(&machines[index].search, seed, &choice, now_ms, now_unix);
	machines[index].search.can_host = can_host;
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

/* a minute's listening and 5 s of it hold a searcher at the same place in the queue */
static void check_late_listener(void)
{
	int index;

	case_name = "e37_late_listener";
	world(6);
	for (index = 0; index < 5; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 1);
	run(55.0);
	CHECK(host_count() == 0 && machines[0].search.pool_count == 0, "E37: five searchers are no pool of six");
	start_searching(5, 1u << P2P_MM_SLAYER, P2P_MM_3V3, 1);
	run(20.0);
	CHECK(same_pool(&machines[0].search, &machines[5].search), "E37: the same pool, in the same order");
	CHECK(host_count() == 1, "E37: one host: %d", host_count());
}

static void heard_kind(struct mm_search *search, int machine, unsigned long sequence, int kind)
{
	struct mm_record record = searching(machine, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	record.kind = kind;
	seed_of(machine, seed);
	size = mm_record_write(&record, seed, bytes, sizeof(bytes));
	if (mm_record_read(bytes, size, &read_back))
		mm_heard(search, &read_back, now_ms);
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

static void hear_game(int host, int players, int held, unsigned long sequence, unsigned char *key_hash)
{
	struct mm_record record = searching(host, sequence), read_back;
	unsigned char seed[P2P_SEED_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE], token[P2P_TOKEN_SIZE];

	seed_of(host, seed);
	memset(token, host, sizeof(token));
	record.kind = MM_KIND_HOSTING;
	record.players = players;
	record.most_players = 4;
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
			kept &= machines[index].search.state == P2P_MM_QUEUED && machines[index].search.started == starts[index];
	CHECK(kept, "E11: back in the queue with their original starts");
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
	run(125.0);
	CHECK(host_count() == 0, "E39: a player short, no lone host: %d", host_count());
	for (index = 0; index < 4; index++)
		CHECK(index == host || machines[index].search.state == P2P_MM_QUEUED,
			"E39: machine %d searches on", index);
}

static void lobby_left_open(int index)
{
	struct mm_search *search = &machines[index].search;

	search->decision.kind = search->decided = P2P_MM_DECIDE_HOST;
	search->decision.playlist = search->hosting_playlist = P2P_MM_SLAYER;
	search->decision.size = search->hosting_size = search->choice.size;
	search->pool_count = 0;
	search->state = P2P_MM_FOUND;
}

static void check_two_lone_hosts(void)
{
	case_name = "e40_two_lone_hosts";
	world(2);
	drop_percent = 100;
	start_searching(0, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	start_searching(1, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(120.5);
	CHECK(host_count() == 0, "E40: unheard, neither hosts alone: %d hosts", host_count());
	lobby_left_open(0);
	lobby_left_open(1);
	run(0.5);
	CHECK(host_count() == 2, "E40: two lobbies, each left alone: %d hosts", host_count());
	drop_percent = sweep_drop;
	run(25.0);
	CHECK(host_count() == 1 && (machines[0].in >= 0 || machines[1].in >= 0),
		"E40: the higher key hash hands over and joins the other's game");
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

static void check_pool_never_hosts(void)
{
	int index;

	case_name = "pool_never_hosts";
	world(4);
	ignore_first_host = 1;
	for (index = 0; index < 4; index++)
		start_searching(index, 1u << P2P_MM_SLAYER, P2P_MM_2V2, 1);
	run(150.0);
	CHECK(ignored >= 0, "a host was elected");
	if (!sweep_drop)
		for (index = 0; index < 4; index++)
			CHECK(index == ignored || (machines[index].in < 0 && !machines[index].hosting &&
				machines[index].search.state == P2P_MM_QUEUED),
				"machine %d, a player short once the host is passed over, searches on", index);
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
}

static void check_faults(void)
{
	check_invited_twice();
	check_passed_over_hosts();
	check_host_quits_lobby();
	check_never_hosts();
	check_return();
	check_two_lone_hosts();
	check_host_failed();
	check_two_queues();
	check_pool_never_hosts();
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
	check_topic_moves();
	check_run_key();
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
