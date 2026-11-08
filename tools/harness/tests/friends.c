/* FRIENDS.C (test): the friends list (port/linux/src/p2p_friends.c): the tag of fixed keys; friend.key made, read
again, and left alone when it cannot be read; the records, friends.txt, and requests sent and answered between players
in one process; test_friends.py takes the code (under_test.inc) */

#include "harness.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang optimize off
#include "monocypher.inc"
#define hash_reduce ed25519_hash_reduce
#include "monocypher_ed25519.inc"
#undef hash_reduce
#pragma clang optimize on
#pragma clang diagnostic pop
#undef FOR
#undef COPY
#undef ZERO
#undef WIPE_CTX
#undef WIPE_BUFFER
#undef MIN

#include "p2p_crypto.inc"

#include <pthread.h>

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static char log_text[4096];
static char base[256], save_root[300];
static int friends_setting = 1, read_fails;

void platform_log(const char *format, ...)
{
	va_list arguments;
	size_t length = strlen(log_text);

	va_start(arguments, format);
	vsnprintf(log_text + length, sizeof(log_text) - length - 1, format, arguments);
	va_end(arguments);
	strcat(log_text, "\n");
}

static unsigned random_counter;
void posix_random_bytes(void *buffer, posix_ulong size)
{
	unsigned char *bytes = buffer;
	posix_ulong index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(++random_counter * 37 + 11);
}

int config_boolean(const char *name)
{
	CHECK(!strcmp(name, "network.friends"), "setting %s", name);
	return friends_setting;
}

const char *platform_save_root(void)
{
	return save_root;
}

static const char *party_name = "Chief";
const char *p2p_party_name(void)
{
	return party_name;
}

char *config_file_read(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *text;

	if (!file || read_fails)
	{
		if (file)
			fclose(file);
		return NULL;
	}
	text = calloc(1, 65536);
	*size = fread(text, 1, 65535, file);
	fclose(file);
	return text;
}

#include "config.inc"

static struct p2p_party_status party;
void p2p_party_get_status(struct p2p_party_status *status)
{
	*status = party;
}

static int can_join = 1, join_works = 1, joined, led = 1, clipboard_offers, cancelled;
static char joined_link[P2P_PARTY_LINK_SIZE], steps[8];
static const char party_link[] = "hceu-party-link-0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789";
int p2p_party_can_join(const char *text)
{
	int held = pthread_mutex_trylock(&p2p_lock);

	if (!held)
		pthread_mutex_unlock(&p2p_lock);
	CHECK(held != 0, "p2p_party_can_join is called with p2p_lock held");
	(void)text;
	return can_join;
}

int p2p_join_invite(const char *text)
{
	joined++;
	strcat(steps, "J");
	snprintf(joined_link, sizeof(joined_link), "%s", text);
	return join_works;
}

void p2p_mm_cancel(void)
{
	cancelled++;
	strcat(steps, "C");
	party.search_state = P2P_MM_OFF;
}

int p2p_party_invite(char *link, int size)
{
	CHECK(size >= P2P_PARTY_LINK_SIZE, "room for the link: %d", size);
	clipboard_offers++;
	if (led)
		strcpy(link, party_link);
	return led;
}

int p2p_party_invite_quiet(char *link, int size)
{
	CHECK(size >= P2P_PARTY_LINK_SIZE, "room for the link: %d", size);
	if (led)
		strcpy(link, party_link);
	return led;
}

/* the fields of p2p_signal.c's broker that the throttle reads; the throttle itself is p2p_signal.c's own */
enum { BROKERS = 4 };
struct broker
{
	int state, protocol, retain_available, publish_tokens;
	unsigned long publish_time;
};
static struct
{
	int broker_count;
	struct broker brokers[BROKERS];
} signalling = { BROKERS };

static long long test_now = 1800000000LL;
static unsigned long test_ms;
unsigned long p2p_now(void)
{
	return (unsigned long)test_now * 1000 + test_ms;
}

static int flushed;
void p2p_signal_flush(void)
{
	flushed++;
}

enum { RECORD_MAXIMUM = 320 };

static struct sent
{
	char topic[64];
	int size, kept;
	unsigned char bytes[RECORD_MAXIMUM];
} published[64], presence[128], cards[16];
static unsigned char last_copy[BROKERS][RECORD_MAXIMUM];
static int last_copy_size[BROKERS];
static char quit_events[16];
static int published_count, presence_count, card_count, card_to[BROKERS], card_kept, presence_to[BROKERS], sent_total[BROKERS], party_to[BROKERS], disconnected, lost;

static void broker_publish(struct broker *broker, const char *topic, const unsigned char *payload, int size)
{
	(void)topic;
	(void)payload;
	(void)size;
	party_to[broker - signalling.brokers]++;
}

static void broker_publish_friend(struct broker *broker, const char *topic, const unsigned char *payload, int size,
	int kept_seconds)
{
	int index = (int)(broker - signalling.brokers), hello = !strncmp(topic + strlen(P2P_FRIEND_TOPIC_PREFIX), "p/", 2);
	struct sent *sent;

	CHECK(size <= 320 && strlen(topic) < 64, "a publish of %d bytes on %s", size, topic);
	if (disconnected)
	{
		lost++;
		return;
	}
	if (!hello && !strncmp(topic + strlen(P2P_FRIEND_TOPIC_PREFIX), "t/", 2))
	{
		card_to[index]++;
		if (!index)
		{
			snprintf(cards[card_count & 15].topic, sizeof(cards[0].topic), "%s", topic);
			memcpy(cards[card_count & 15].bytes, payload, (size_t)size);
			cards[card_count & 15].size = size;
			cards[card_count++ & 15].kept = kept_seconds;
		}
		return;
	}
	presence_to[index] += hello;
	if (hello && !index && strlen(quit_events) < 8)
		strcat(quit_events, "B");
	sent_total[index] += !hello;
	if (!hello)
	{
		memcpy(last_copy[index], payload, (size_t)size);
		last_copy_size[index] = size;
	}
	if (index)
		return;
	sent = hello ? &presence[presence_count < 128 ? presence_count++ : 127] :
		&published[published_count < 64 ? published_count++ : 63];
	snprintf(sent->topic, sizeof(sent->topic), "%s", topic);
	memcpy(sent->bytes, payload, (size_t)size);
	sent->size = size;
	sent->kept = kept_seconds;
}

#include "throttle.inc"

static void brokers_mask(unsigned mask)
{
	int index;

	for (index = 0; index < BROKERS; index++)
	{
		struct broker *broker = &signalling.brokers[index];

		if (mask >> index & 1 && broker->state != _broker_ready)
		{
			broker->protocol = 5;
			broker->retain_available = 1;
			broker->publish_tokens = PUBLISH_BURST;
			broker->publish_time = p2p_now();
		}
		broker->state = mask >> index & 1 ? _broker_ready : 0;
	}
}

static void brokers_up(int count)
{
	brokers_mask((1u << count) - 1);
}

static void starve(int from)
{
	int index;

	for (index = from; index < BROKERS; index++)
		signalling.brokers[index].publish_tokens = 0;
}

static void brokers_full(void)
{
	int index;

	for (index = 0; index < BROKERS; index++)
	{
		signalling.brokers[index].publish_tokens = PUBLISH_BURST;
		signalling.brokers[index].publish_time = p2p_now();
	}
}

unsigned p2p_signal_up(void)
{
	unsigned up = 0;
	int index;

	for (index = 0; index < BROKERS; index++)
		up |= (unsigned)(signalling.brokers[index].state == _broker_ready) << index;
	return up;
}

int p2p_signal_connected(void)
{
	int index, count = 0;

	for (index = 0; index < BROKERS; index++)
		count += signalling.brokers[index].state == _broker_ready;
	return count;
}

static int wildcards_off;

int p2p_signal_wildcards(void)
{
	return wildcards_off ? 0 : p2p_signal_connected();
}

static char topic_requests[64], topic_inbox[64], topic_lookup[64];
void p2p_signal_friend_topics(const char *requests, const char *inbox, const char *lookup)
{
	snprintf(topic_requests, sizeof(topic_requests), "%s", requests);
	snprintf(topic_inbox, sizeof(topic_inbox), "%s", inbox);
	snprintf(topic_lookup, sizeof(topic_lookup), "%s", lookup);
}

/* friend_tag answers this for every key while it is set (two players on one tag); test_friends.py wraps it */
static const char *tag_forced;

static int server_mode;
int halo_server_mode(void)
{
	return server_mode;
}

static int rename_fails, write_short;
int posix_server_rename(const char *from, const char *to)
{
	return !rename_fails && rename(from, to) == 0;
}

static long test_write(int descriptor, const void *bytes, size_t size)
{
	if (write_short)
		return write_short++ == 1 ? (long)write(descriptor, bytes, size / 2) : 0;
	return (long)write(descriptor, bytes, size);
}
#define time(x) ((time_t)test_now)
#define write test_write

void p2p_lobby_quit(void)
{
	strcat(quit_events, "T");
}

void p2p_signal_disconnect(void)
{
	strcat(quit_events, "D");
	disconnected = 1;
}

#include "under_test.inc"
#include "quit.inc"

#undef time
#undef write

static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

enum { ALICE, BOB, CARA, DAVE, COPY, PEOPLE };
static __typeof__(friends) people[PEOPLE];
static char roots[PEOPLE][300];
static const char *names[PEOPLE] = { "Alice", "Bob", "Cara", "Dave", "Dave" };
static int current = -1;

static void become(int who)
{
	if (current >= 0)
		people[current] = friends;
	friends = people[who];
	current = who;
	brokers_full();
	snprintf(save_root, sizeof(save_root), "%s", roots[who]);
	party_name = names[who];
}

static void people_make(void)
{
	int who;

	for (who = 0; who < COPY; who++)
	{
		snprintf(roots[who], sizeof(roots[who]), "%s/%s", base, names[who]);
		CHECK(mkdir(roots[who], 0700) == 0, "a folder for %s", names[who]);
		snprintf(save_root, sizeof(save_root), "%s", roots[who]);
		party_name = names[who];
		memset(&friends, 0, sizeof(friends));
		CHECK(p2p_friends_wanted(), "%s's friends on", names[who]);
		people[who] = friends;
	}
	current = -1;
	become(ALICE);
	log_text[0] = 0;
}

static void reload(int who)
{
	become(who);
	memset(&friends, 0, sizeof(friends));
	CHECK(p2p_friends_wanted(), "%s's friends on again", names[who]);
}

static void copy_of(int who)
{
	become(who);
	people[COPY] = friends;
	snprintf(roots[COPY], sizeof(roots[COPY]), "%s", roots[who]);
	names[COPY] = names[who];
}

static const unsigned char *key_of(int who)
{
	return who == current ? friends.public_key : people[who].public_key;
}

static void party_with(int who)
{
	memset(&party, 0, sizeof(party));
	party.count = 2;
	snprintf(party.members[0].name, sizeof(party.members[0].name), "%s", names[current]);
	party.members[0].you = party.members[0].leader = 1;
	memcpy(party.members[0].friend_key, friends.public_key, P2P_KEY_SIZE);
	snprintf(party.members[1].name, sizeof(party.members[1].name), "%s", names[who]);
	memcpy(party.members[1].friend_key, key_of(who), P2P_KEY_SIZE);
}

static int heard(const struct sent *record)
{
	return friend_heard(record->topic, record->bytes, record->size);
}

static struct sent last_published(int clear)
{
	int index;

	for (index = published_count - 1; index >= 0; index--)
	{
		if (!published[index].size == !!clear)
			return published[index];
	}
	CHECK(0, "no %s publish among %d", clear ? "empty" : "record", published_count);
	return published[0];
}

static struct sent last_presence(void)
{
	CHECK(presence_count > 0, "a presence record among %d", presence_count);
	return presence[presence_count - 1];
}

static struct sent request(int from, int to)
{
	become(from);
	party_with(to);
	CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "%s's request to %s", names[from], names[to]);
	memset(&party, 0, sizeof(party));
	published_count = 0;
	p2p_friends_update();
	return last_published(0);
}

static struct sent answer(int to, int from, int yes)
{
	char id[17];

	become(to);
	friend_id(key_of(from), id);
	published_count = 0;
	p2p_friend_answer(id, yes);
	p2p_friends_update();
	return yes ? last_published(0) : last_published(1);
}

static void befriend(int from, int to)
{
	struct sent sent = request(from, to), accept;

	become(to);
	CHECK(heard(&sent) == FRIEND_REQUEST, "%s hears %s's request", names[to], names[from]);
	accept = answer(to, from, 1);
	become(from);
	CHECK(heard(&accept) == FRIEND_ACCEPT, "%s hears %s's accept", names[from], names[to]);
}

static struct sent record_at(int kind, int to, unsigned long long sequence, const unsigned char *nonce,
	const unsigned char *body, int size)
{
	static const unsigned char no_nonce[FRIEND_NONCE_SIZE];
	long long saved = test_now;
	struct sent sent;

	memset(&sent, 0, sizeof(sent));
	test_now = 0;
	friends.sequence = sequence - 1;
	sent.size = friend_record_write(kind, key_of(to), nonce ? nonce : no_nonce, body, size, sent.bytes);
	test_now = saved;
	CHECK(sent.size > 0, "a record to %s", names[to]);
	if (kind == FRIEND_REQUEST || kind == FRIEND_ACCEPT)
		CHECK(friend_pair_topic(key_of(to), key_of(to), sent.topic), "a topic to %s", names[to]);
	else
		snprintf(sent.topic, sizeof(sent.topic), P2P_FRIEND_TOPIC_PREFIX "p/%s",
			to == current ? friends.inbox : people[to].inbox);
	return sent;
}

static const unsigned char presence_body[3] = { P2P_FRIEND_MENUS, HALO_PORT_NETWORK_VERSION >> 8,
	HALO_PORT_NETWORK_VERSION & 255 };

static struct sent presence_at(int kind, int from, int to, int state, int version, unsigned long long sequence)
{
	unsigned char body[3] = { (unsigned char)state, (unsigned char)(version >> 8), (unsigned char)version };

	become(from);
	return record_at(kind, to, sequence, NULL, body, 3);
}

static unsigned long long now_ms(void)
{
	return (unsigned long long)test_now * 1000;
}

static int received(const struct sent *record, int reason)
{
	int before = friends.drops[-reason];

	p2p_friends_received(record->topic, record->bytes, record->size);
	return friends.drops[-reason] - before;
}

static int occurrences(const char *text, const char *part)
{
	int count = 0;

	for (text = strstr(text, part); text; text = strstr(text + 1, part))
		count++;
	return count;
}

static void list_path_of(int who, const char *suffix, char *path)
{
	snprintf(path, 600, "%s/friends.txt%s", roots[who], suffix);
}

static int text_of(const char *path, char *text, int size)
{
	FILE *file = fopen(path, "rb");
	int count;

	if (!file)
		return -1;
	count = (int)fread(text, 1, (size_t)size - 1, file);
	text[count] = 0;
	fclose(file);
	return count;
}

static void text_set(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	CHECK(file, "%s written", path);
	fwrite(text, 1, strlen(text), file);
	fclose(file);
}

static struct friend friend_made(int index, const char *name)
{
	unsigned char seed[P2P_SEED_SIZE] = { 0 };
	struct friend friend;

	memset(&friend, 0, sizeof(friend));
	seed[0] = (unsigned char)(index + 1);
	seed[1] = 0x55;
	p2p_ed25519_public(seed, friend.key, NULL);
	friend_id(friend.key, friend.id);
	friend.sent = index % 2;
	if (friend.sent)
		memset(friend.nonce, 0x40 + index, sizeof(friend.nonce));
	else
		snprintf(friend.inbox, sizeof(friend.inbox), "%016x", 0xa0b0c0 + index);
	friend.last_sequence = 1000ULL + (unsigned long long)index;
	friend.last_day = friend_today() - index % 5;
	friend_name_tag(friend.key, (const unsigned char *)name, (int)strlen(name), friend.name_tag);
	return friend;
}

static int line_of(const struct friend *friend, char *line)
{
	char key[65], nonce[33];

	p2p_hex(friend->key, P2P_KEY_SIZE, key);
	p2p_hex(friend->nonce, FRIEND_NONCE_SIZE, nonce);
	return sprintf(line, "%s %s %s %s %llu %ld %s\n", key, friend->inbox[0] ? friend->inbox : "-",
		friend->sent ? "sent" : "friend", friend->sent ? nonce : "-", friend->last_sequence, friend->last_day,
		friend->name_tag);
}

static void key_path(char *path)
{
	snprintf(path, 600, "%s/friend.key", save_root);
}

static void file_set(const char *text)
{
	char path[600];
	FILE *file;

	key_path(path);
	file = fopen(path, "wb");
	fwrite(text, 1, strlen(text), file);
	fclose(file);
}

static int file_get(char *text)
{
	char path[600];
	FILE *file;
	int size;

	key_path(path);
	file = fopen(path, "rb");
	if (!file)
		return -1;
	size = (int)fread(text, 1, 1023, file);
	text[size] = 0;
	fclose(file);
	return size;
}

static void connected_fans(int count)
{
	int index;

	for (index = 0; index < count; index++)
		friends.list[friends.count++] = friend_made(2 * index, "Fan");
}

static void restart(void)
{
	memset(&friends, 0, sizeof(friends));
	log_text[0] = 0;
}

static struct p2p_friends_status status_now(void)
{
	struct p2p_friends_status status;

	p2p_friends_get_status(&status);
	return status;
}

static void hex_of(const unsigned char *bytes, int size, char *text)
{
	p2p_hex(bytes, size, text);
}

static int drops_left, dropped_from = -1;

static void deliver(void)
{
	int index, who, sender = current;

	for (index = 0; index < presence_count; index++)
	{
		const char *topic = presence[index].topic + strlen(presence[index].topic) - 16;

		if (sender == dropped_from && drops_left > 0)
		{
			drops_left--;
			continue;
		}
		for (who = 0; who < COPY; who++)
		{
			if (!strcmp(topic, who == sender ? friends.inbox : people[who].inbox))
			{
				become(who);
				heard(&presence[index]);
				become(sender);
			}
		}
	}
	presence_count = 0;
}

static int index_of(int who)
{
	int index;

	for (index = 0; index < friends.count; index++)
	{
		if (!memcmp(friends.list[index].key, key_of(who), P2P_KEY_SIZE))
			return index;
	}
	return -1;
}

static struct sent invited(int from, int to)
{
	char id[17];
	int index = -1;

	become(from);
	friend_id(key_of(to), id);
	index = index_of(to);
	CHECK(index >= 0, "%s has %s", names[from], names[to]);
	friends.list[index].heard = 1;
	friends.list[index].heard_time = p2p_now();
	friends.list[index].state = P2P_FRIEND_MATCHMAKING;
	presence_count = 0;
	CHECK(p2p_friend_invite(id) == 1, "%s invites %s", names[from], names[to]);
	p2p_friends_update();
	for (index = presence_count - 1; index >= 0; index--)
	{
		if (presence[index].bytes[1] == FRIEND_INVITE)
			return presence[index];
	}
	CHECK(0, "an invite among %d records", presence_count);
	return presence[0];
}


static void tag_hash(const char *tag, char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256((const unsigned char *)tag, (int)strlen(tag), digest);
	p2p_hex(digest, FRIEND_INBOX_BYTES, hash);
}

static void card_topic_of(const char *tag, const char *id, char *topic)
{
	char hash[17];

	tag_hash(tag, hash);
	snprintf(topic, 64, P2P_FRIEND_TOPIC_PREFIX "t/%s/%s", hash, id);
}

static struct sent card_of(int who)
{
	become(who);
	card_count = 0;
	p2p_friends_update();
	CHECK(card_count == 1, "%s published a card: %d", names[who], card_count);
	return cards[0];
}

static struct sent card_at(int who, unsigned long long sequence, const char *under_tag)
{
	static const unsigned char zero[P2P_KEY_SIZE];
	long long saved = test_now;
	struct sent sent;
	int self = current;

	memset(&sent, 0, sizeof(sent));
	become(who);
	test_now = 0;
	friends.sequence = sequence - 1;
	sent.size = friend_record_write(FRIEND_CARD, zero, zero, names[who], (int)strlen(names[who]), sent.bytes);
	test_now = saved;
	card_topic_of(under_tag ? under_tag : friends.tag, friends.id, sent.topic);
	become(self);
	return sent;
}

static void lookup_ends(void)
{
	test_ms += 3000;
	p2p_friends_update();
}

static void cleanup(void)
{
	char command[300];

	snprintf(command, sizeof(command), "rm -rf %s", base);
	if (system(command))
		fputs("friends.c: the scratch folder stays\n", stderr);
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";

	umask(022);
	snprintf(base, sizeof(base), "/tmp/friends-test-XXXXXX");
	CHECK(mkdtemp(base), "a scratch folder");
	snprintf(save_root, sizeof(save_root), "%s", base);
	atexit(cleanup);
	brokers_up(4);

	CASE("tag_vectors")
	{
		static const struct
		{
			unsigned char seed[32];
			const char *tag, *id;
		} vectors[3] =
		{
			{ { 0 }, "K948JNL7", "1de37a61126fcff0" },
			{ { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
				30, 31, 32 }, "YVBCNFUW", "48a12a5939b510e5" },
			{ { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
				255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }, "L5SGK3B5", "2f349b75cbb15421" },
		};
		int vector, index;

		for (vector = 0; vector < 3; vector++)
		{
			unsigned char public_key[32], digest[32];
			char tag[P2P_FRIEND_TAG_SIZE + 1], id[17], tag_hash[17];

			p2p_ed25519_public(vectors[vector].seed, public_key, NULL);
			friend_tag(public_key, tag);
			friend_id(public_key, id);
			friend_hash(tag_label, (int)sizeof(tag_label) - 1, public_key, digest);
			hex_of(digest, 8, tag_hash);
			CHECK(!strcmp(tag, vectors[vector].tag), "vector %d: tag %s, expected %s", vector, tag, vectors[vector].tag);
			CHECK(strlen(tag) == 8, "vector %d: tag %s is not 8 characters", vector, tag);
			for (index = 0; index < 8; index++)
				CHECK(strchr(alphabet, tag[index]), "vector %d: %c is outside the alphabet", vector, tag[index]);
			CHECK(!strcmp(id, vectors[vector].id), "vector %d: id %s, expected %s", vector, id, vectors[vector].id);
			CHECK(strcmp(id, tag_hash), "vector %d: the id is the tag's hash", vector);
		}
	}
	CASE("key_file")
	{
		char path[600], first[1024], second[1024], line[256], seed_hex[65], inbox_hex[17];
		struct stat information;
		struct p2p_friends_status status;
		static const char *const unreadable[] =
		{
			"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n0123456789abcdef\n",
			"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg\n0123456789abcdef\n",
			"",
			"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
			"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n0123456789abcde\n",
			"# only a comment\n",
		};
		int index;

		key_path(path);
		status = status_now();
		CHECK(status.on == 1, "a missing file is made: on %d", status.on);
		CHECK(strstr(log_text, "friends: new key"), "the new key is logged: %s", log_text);
		CHECK(stat(path, &information) == 0 && (information.st_mode & 0777) == 0600, "mode %o, expected 600",
			(unsigned)(information.st_mode & 0777));
		file_get(first);
		CHECK(first[0] == '#', "a header line first");
		CHECK(sscanf(strchr(first, '\n') + 1, "%64[0-9a-f]\n%16[0-9a-f]\n", seed_hex, inbox_hex) == 2 &&
			strlen(seed_hex) == 64 && strlen(inbox_hex) == 16, "64 hex, then a 16-hex inbox: %s", first);
		CHECK(!strncmp(status.me, "Chief#", 6) && strlen(status.me) == 14, "name#tag: %s", status.me);
		CHECK(!strcmp(friends.inbox, inbox_hex), "the inbox is the file's");

		restart();
		status = status_now();
		file_get(second);
		CHECK(status.on == 1 && !strcmp(second, first), "a second load reads the same file");
		CHECK(!strstr(log_text, "new key") && !log_text[0], "a second load logs nothing: %s", log_text);
		{
			struct p2p_friends_status again;

			restart();
			again = status_now();
			CHECK(!strcmp(again.me, status.me), "the same tag: %s, %s", again.me, status.me);
		}

		for (index = 0; index < (int)(sizeof(unreadable) / sizeof(unreadable[0])); index++)
		{
			restart();
			file_set(unreadable[index]);
			status = status_now();
			file_get(second);
			CHECK(status.on == 0 && !status.me[0], "file %d: friends off", index);
			CHECK(strstr(log_text, "friends: friend.key unreadable"), "file %d: logged: %s", index, log_text);
			CHECK(!strstr(log_text, "new key"), "file %d: no new key", index);
			CHECK(!strcmp(second, unreadable[index]), "file %d: the file is left byte for byte", index);
		}

		restart();
		file_set("keep me\n");
		read_fails = 1;
		status = status_now();
		read_fails = 0;
		file_get(second);
		CHECK(status.on == 0 && !strcmp(second, "keep me\n"), "a file that cannot be read is not written over: %s", second);

		restart();
		unlink(path);
		friends_setting = 0;
		status = status_now();
		friends_setting = 1;
		CHECK(status.on == 0 && file_get(second) < 0 && !log_text[0], "friends off in the settings: no file, no log");

		restart();
		write_short = 1;
		status = status_now();
		write_short = 0;
		CHECK(status.on == 0 && status.key_unmade && file_get(second) < 0, "a key that cannot be written: friends off");
		CHECK(strstr(log_text, "friends: friend.key could not be created") && !strstr(log_text, "unreadable"),
			"logged as not created: %s", log_text);
	}
	CASE("record_round_trip")
	{
		static const int kinds[] = { FRIEND_REQUEST, FRIEND_ACCEPT, FRIEND_HELLO, FRIEND_HERE, FRIEND_BYE, FRIEND_INVITE };
		unsigned char nonce[FRIEND_NONCE_SIZE], body[FRIEND_BODY_MAXIMUM], card[FRIEND_RECORD_MAXIMUM];
		unsigned char plain[FRIEND_PLAIN_SIZE + FRIEND_BODY_MAXIMUM];
		struct friend_record record;
		int index, size;

		people_make();
		for (index = 0; index < (int)(sizeof(kinds) / sizeof(kinds[0])); index++)
		{
			struct sent sent;

			become(ALICE);
			memset(nonce, 0x30 + index, sizeof(nonce));
			size = friend_body(body);
			sent = record_at(kinds[index], BOB, now_ms() + (unsigned long long)index, nonce, body, size);
			become(BOB);
			CHECK(friend_record_read(sent.bytes, sent.size, now_ms(), &record) == kinds[index], "kind %d reads back",
				kinds[index]);
			CHECK(record.kind == kinds[index] && record.body_size == size && !memcmp(record.body, body, (size_t)size),
				"kind %d: the same body", kinds[index]);
			CHECK(!memcmp(record.sender, people[ALICE].public_key, P2P_KEY_SIZE), "kind %d: the sender", kinds[index]);
			CHECK(!memcmp(record.nonce, nonce, sizeof(nonce)) && record.sequence == now_ms() + (unsigned long long)index,
				"kind %d: the nonce and sequence", kinds[index]);
			CHECK(sent.bytes[0] == 1 && sent.bytes[1] == kinds[index], "kind %d: format and kind bytes", kinds[index]);
			become(CARA);
			CHECK(friend_record_read(sent.bytes, sent.size, now_ms(), &record) == FRIEND_DROP_SEAL,
				"kind %d: only the recipient opens it", kinds[index]);
		}
		become(ALICE);
		memset(nonce, 0x77, sizeof(nonce));
		size = friend_seal(FRIEND_CARD, friends.public_key, plain,
			friend_sign(FRIEND_CARD, (const unsigned char *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
			nonce, "card", 4, plain), card);
		CHECK(size == 2 + FRIEND_PLAIN_SIZE + 4, "a card is not sealed: %d bytes", size);
		become(BOB);
		CHECK(friend_record_read(card, size, now_ms(), &record) == FRIEND_CARD && record.body_size == 4 &&
			!memcmp(record.body, "card", 4) && !memcmp(record.sender, people[ALICE].public_key, P2P_KEY_SIZE),
			"a card reads with no recipient");
	}
	CASE("bad_records")
	{
		unsigned char body[4] = { 0 }, nonce[FRIEND_NONCE_SIZE] = { 0 }, plain[FRIEND_PLAIN_SIZE + 8];
		struct sent good, bad;
		int before;

		people_make();
		befriend(ALICE, BOB);
		become(ALICE);
		good = record_at(FRIEND_HERE, BOB, now_ms() + 10, NULL, presence_body, 3);
		become(BOB);
		before = published_count;

		bad = good;
		bad.size = 0;
		CHECK(received(&bad, FRIEND_DROP_EMPTY) == 1, "an empty payload");
		bad = good;
		bad.size = 321;
		CHECK(received(&bad, FRIEND_DROP_LONG) == 1, "1 byte over the maximum");
		bad = good;
		bad.size = 40;
		CHECK(received(&bad, FRIEND_DROP_SHORT) == 1, "truncated");
		bad = good;
		bad.size--;
		CHECK(received(&bad, FRIEND_DROP_SEAL) == 1, "1 byte short");
		bad = good;
		bad.bytes[0] = 2;
		CHECK(received(&bad, FRIEND_DROP_FORMAT) == 1, "format byte 2");
		p2p_friends_update();
		CHECK(strstr(log_text, "1 empty, 1 long, 1 format, 1 short, 1 seal\n"), "the counters so far: %s", log_text);
		CHECK(published_count == before, "a format 2 record is never cleared");
		bad = good;
		bad.bytes[1] = 9;
		CHECK(received(&bad, FRIEND_DROP_KIND) == 1, "kind 9");

		become(ALICE);
		friends.sequence = now_ms() + 20;
		{
			unsigned long long saved = (unsigned long long)test_now;
			int size;

			test_now = 0;
			size = friend_sign(FRIEND_HERE, people[BOB].public_key, nonce, body, 0, plain);
			plain[size - 1] ^= 1;
			bad.size = friend_seal(FRIEND_HERE, people[BOB].public_key, plain, size, bad.bytes);
			size = friend_sign(FRIEND_HERE, people[CARA].public_key, nonce, body, 0, plain);
			test_now = (long long)saved;
			become(BOB);
			CHECK(received(&bad, FRIEND_DROP_SIGNATURE) == 1, "a flipped signature byte");
			become(ALICE);
			bad.size = friend_seal(FRIEND_HERE, people[BOB].public_key, plain, size, bad.bytes);
			become(BOB);
			CHECK(received(&bad, FRIEND_DROP_SIGNATURE) == 1, "signed for Cara, sealed to Bob");
		}

		bad = record_at(FRIEND_HERE, BOB, now_ms() + 30, NULL, body, 0);
		CHECK(received(&bad, FRIEND_DROP_SELF) == 1, "the player's own record");
		CHECK(heard(&good) == FRIEND_HERE, "the good record");
		CHECK(received(&good, FRIEND_DROP_REPLAY) == 1, "a same-sequence replay");
		CHECK(friends.drops[-FRIEND_DROP_SIGNATURE] == 2 && !friends.drops[-FRIEND_DROP_EMPTY], "the counters");
	}
	CASE("once_from_four")
	{
		struct sent sent, accept, here;
		int copy, kinds[4];

		people_make();
		sent = request(ALICE, BOB);
		become(BOB);
		for (copy = 0; copy < 4; copy++)
			kinds[copy] = heard(&sent);
		CHECK(kinds[0] == FRIEND_REQUEST && kinds[1] == FRIEND_DROP_REPLAY && kinds[2] == FRIEND_DROP_REPLAY &&
			kinds[3] == FRIEND_DROP_REPLAY && friends.request_count == 1, "one request from four brokers");
		accept = answer(BOB, ALICE, 1);
		become(ALICE);
		for (copy = 0; copy < 4; copy++)
			kinds[copy] = heard(&accept);
		CHECK(kinds[0] == FRIEND_ACCEPT && kinds[1] < 0 && kinds[2] < 0 && kinds[3] < 0 && friends.count == 1,
			"one accept from four brokers");
		here = record_at(FRIEND_HERE, BOB, now_ms() + 50, NULL, presence_body, 3);
		become(BOB);
		for (copy = 0; copy < 4; copy++)
			kinds[copy] = heard(&here);
		CHECK(kinds[0] == FRIEND_HERE && kinds[1] == FRIEND_DROP_REPLAY && kinds[2] == FRIEND_DROP_REPLAY &&
			kinds[3] == FRIEND_DROP_REPLAY, "one presence from four brokers");
	}
	CASE("age")
	{
		unsigned char body[FRIEND_BODY_MAXIMUM];
		struct sent record;
		char line[100];
		int size;

		people_make();
		befriend(ALICE, BOB);
		test_now += 1000;
		become(ALICE);
		record = record_at(FRIEND_HERE, BOB, now_ms() - 301000, NULL, presence_body, 3);
		become(BOB);
		log_text[0] = 0;
		CHECK(heard(&record) == FRIEND_DROP_AGE, "a HERE 301 s old");
		snprintf(line, sizeof(line), "friends: friend %d clock off by -301 s", friend_find(people[ALICE].public_key)->number);
		CHECK(strstr(log_text, line), "logged by number: %s", log_text);
		CHECK(!strstr(log_text, people[ALICE].id), "no id in the log: %s", log_text);
		become(ALICE);
		record = record_at(FRIEND_HERE, BOB, now_ms() + 301000, NULL, presence_body, 3);
		become(BOB);
		CHECK(heard(&record) == FRIEND_DROP_AGE, "a HERE 301 s ahead");
		CHECK(occurrences(log_text, "clock off") == 1, "logged once per sender: %s", log_text);
		become(ALICE);
		record = record_at(FRIEND_HERE, BOB, now_ms() - 299000, NULL, presence_body, 3);
		become(BOB);
		CHECK(heard(&record) == FRIEND_HERE, "a HERE 299 s old");

		become(CARA);
		size = friend_body(body);
		record = record_at(FRIEND_REQUEST, BOB, now_ms() - 6ULL * 86400000, NULL, body, size);
		become(BOB);
		CHECK(heard(&record) == FRIEND_REQUEST, "a request 6 days old");
		become(DAVE);
		size = friend_body(body);
		record = record_at(FRIEND_REQUEST, BOB, now_ms() - 7ULL * 86400000 - 1000, NULL, body, size);
		become(BOB);
		CHECK(heard(&record) == FRIEND_DROP_AGE, "a request 7 days and 1 s old");
	}
	CASE("list_file")
	{
		static struct friend made[P2P_FRIENDS_MAXIMUM];
		static char text[65536], again[65536];
		char path[600], name[16];
		struct stat information;
		int index, size = 0;

		people_make();
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		{
			snprintf(name, sizeof(name), "P%d", index);
			made[index] = friend_made(index, index ? name : "Mr #1 \xc3\x9cn\xc3\xaf");
			friends.list[index] = made[index];
		}
		friends.count = P2P_FRIENDS_MAXIMUM;
		CHECK(friends_save(friends.list_path), "64 saved");
		list_path_of(ALICE, "", path);
		CHECK(stat(path, &information) == 0 && (information.st_mode & 0777) == 0600, "mode %o",
			(unsigned)(information.st_mode & 0777));
		reload(ALICE);
		CHECK(friends.count == P2P_FRIENDS_MAXIMUM, "64 read back: %d", friends.count);
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		{
			const struct friend *friend = &friends.list[index];

			CHECK(!memcmp(friend->key, made[index].key, P2P_KEY_SIZE) && !strcmp(friend->inbox, made[index].inbox) &&
				friend->sent == made[index].sent && !memcmp(friend->nonce, made[index].nonce, FRIEND_NONCE_SIZE) &&
				friend->last_sequence == made[index].last_sequence && friend->last_day == made[index].last_day &&
				!strcmp(friend->name_tag, made[index].name_tag) && !strcmp(friend->id, made[index].id),
				"friend %d the same: %s", index, friend->name_tag);
		}
		CHECK(!strncmp(friends.list[0].name_tag, "Mr #1 \xc3\x9cn\xc3\xaf#", 12), "the name: %s",
			friends.list[0].name_tag);

		for (index = 0; index < 80; index++)
		{
			struct friend friend = friend_made(index, "P");

			size += line_of(&friend, text + size);
		}
		{
			struct friend friend = friend_made(0, "P"), old = friend_made(90, "Old");

			size += line_of(&friend, text + size);
			size += sprintf(text + size, "not a friend line\n");
			friend = friend_made(91, "Tag");
			memcpy(friend.name_tag + strlen(friend.name_tag) - 8, "AAAAAAAA", 8);
			size += line_of(&friend, text + size);
			old.sent = 0;
			snprintf(old.inbox, sizeof(old.inbox), "00000000000000aa");
			old.last_day = friend_today() - 61;
			size += line_of(&old, text + size);
			old = friend_made(92, "Sixty");
			old.sent = 0;
			snprintf(old.inbox, sizeof(old.inbox), "00000000000000ab");
			old.last_day = friend_today() - 60;
			memmove(text + line_of(&old, again), text, (size_t)size + 1);
			memcpy(text, again, strlen(again));
			size += (int)strlen(again);
		}
		text_set(path, text);
		reload(ALICE);
		CHECK(friends.count == P2P_FRIENDS_MAXIMUM, "84 lines load 64: %d", friends.count);
		CHECK(!strncmp(friends.list[0].name_tag, "Sixty#", 6), "a friend unheard 60 days stays");
		CHECK(strstr(log_text, "friends: friends.txt: 2 unreadable, 1 repeated, 17 past 64, 1 unheard lines left out"),
			"the dropped counts: %s", log_text);
		list_path_of(ALICE, ".bad", path);
		CHECK(text_of(path, again, sizeof(again)) == size && !strcmp(again, text), "the original kept as .bad");

		list_path_of(ALICE, "", path);
		CHECK(friends_save(friends.list_path), "saved");
		text_of(path, text, sizeof(text));
		rename_fails = 1;
		friends.list[1].last_sequence = 999999;
		log_text[0] = 0;
		CHECK(!friends_save(friends.list_path), "a rename that fails");
		rename_fails = 0;
		CHECK(status_now().unsaved, "the failed save is shown");
		CHECK(text_of(path, again, sizeof(again)) > 0 && !strcmp(again, text), "the old file stays");
		CHECK(friends.count == P2P_FRIENDS_MAXIMUM && friends.list[1].last_sequence == 999999, "the list stays");
		CHECK(strstr(log_text, "friends: friends.txt could not be saved"), "logged: %s", log_text);
		list_path_of(ALICE, ".tmp", path);
		CHECK(access(path, F_OK) != 0, "no .tmp left");
		CHECK(friends_save(friends.list_path) && !status_now().unsaved, "the next save clears it");
	}
	CASE("topics")
	{
		char to_bob[FRIEND_TOPIC_SIZE], to_alice[FRIEND_TOPIC_SIZE], expected[FRIEND_TOPIC_SIZE], hash[17], id[17];
		unsigned char message[13 + 64], digest[32], bob_x25519[32];

		people_make();
		CHECK(friend_pair_topic(people[BOB].public_key, people[BOB].public_key, to_bob), "a topic to Bob");
		CHECK(strlen(to_bob) == 49 && strlen(friends.requests_topic) <= 49 && strlen(friends.inbox_topic) <= 49,
			"at most 49 bytes: %s, %s, %s", to_bob, friends.requests_topic, friends.inbox_topic);
		p2p_friends_update();
		CHECK(!strcmp(topic_requests, friends.requests_topic) && !strcmp(topic_inbox, friends.inbox_topic),
			"the topics subscribed: %s %s", topic_requests, topic_inbox);
		snprintf(expected, sizeof(expected), "hceu/3/friend/%s/r/+", friends.id);
		CHECK(!strcmp(friends.requests_topic, expected), "the requests topic: %s", friends.requests_topic);
		snprintf(expected, sizeof(expected), "hceu/3/friend/p/%s", friends.inbox);
		CHECK(!strcmp(friends.inbox_topic, expected), "the inbox topic: %s", friends.inbox_topic);

		memcpy(message, "hceu-friend-r", 13);
		memcpy(message + 13, people[BOB].public_key, 32);
		p2p_ed25519_to_x25519(people[BOB].public_key, bob_x25519);
		p2p_x25519(message + 45, friends.x25519_secret, bob_x25519);
		p2p_sha256(message, (int)sizeof(message), digest);
		p2p_hex(digest, 8, hash);
		friend_id(people[BOB].public_key, id);
		snprintf(expected, sizeof(expected), "hceu/3/friend/%s/r/%s", id, hash);
		CHECK(!strcmp(to_bob, expected), "the request topic: %s, expected %s", to_bob, expected);

		become(BOB);
		CHECK(friend_pair_topic(people[ALICE].public_key, people[ALICE].public_key, to_alice), "a topic to Alice");
		CHECK(strcmp(to_alice, to_bob), "the topics differ by direction");
		CHECK(friend_pair_topic(friends.public_key, people[ALICE].public_key, expected) && !strcmp(expected, to_bob),
			"Bob expects Alice's on the topic she sends to");
		{
			struct sent sent = request(ALICE, BOB), moved;
			unsigned char body[FRIEND_BODY_MAXIMUM];

			moved = record_at(FRIEND_REQUEST, BOB, now_ms() + 5, NULL, body, friend_body(body));
			become(BOB);
			CHECK(!strcmp(sent.topic, to_bob) && heard(&sent) == FRIEND_REQUEST, "on its own topic");
			snprintf(moved.topic, sizeof(moved.topic), "hceu/3/friend/%s/r/0123456789abcdef", friends.id);
			CHECK(heard(&moved) == FRIEND_DROP_TOPIC, "a request on another pair's topic");
			snprintf(moved.topic, sizeof(moved.topic), "%s", friends.inbox_topic);
			CHECK(heard(&moved) == FRIEND_DROP_KIND, "a request on the inbox");
		}
	}
	CASE("names")
	{
		static const char *const bad[] = { "Bad\x1f", "Bad\x7f", "\xc3\x28", "\xc0\x80", "\xed\xa0\x80",
			"ABCDEFGHIJKL", "\xf4\x90\x80\x80", "" };
		static const char *const good[] = { "ABCDEFGHIJK", "\xc3\x9cn\xc3\xaf" "c\xc3\xb6" "d\xc3\xa9", "Mr #1" };
		unsigned char body[64];
		struct sent record;
		int index, step = 0;

		people_make();
		for (index = 0; index < (int)(sizeof(bad) / sizeof(bad[0])) + 3; index++)
		{
			int is_bad = index < (int)(sizeof(bad) / sizeof(bad[0]));
			const char *name = is_bad ? bad[index] : good[index - (int)(sizeof(bad) / sizeof(bad[0]))];

			become(ALICE);
			friend_body(body);
			memcpy(body + FRIEND_INBOX_BYTES, name, strlen(name));
			record = record_at(FRIEND_REQUEST, BOB, now_ms() + (unsigned long long)++step, NULL, body,
				FRIEND_INBOX_BYTES + (int)strlen(name));
			become(BOB);
			CHECK(heard(&record) == (is_bad ? FRIEND_DROP_NAME : FRIEND_REQUEST), "name %d (%s)", index, name);
		}
	}
	CASE("replay")
	{
		unsigned char body[FRIEND_BODY_MAXIMUM], nonce[FRIEND_NONCE_SIZE];
		struct sent record, sent, accept;
		unsigned long long stored;
		int size;

		people_make();
		sent = request(ALICE, BOB);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST, "the request");
		memcpy(nonce, friends.requests[0].nonce, sizeof(nonce));
		accept = answer(BOB, ALICE, 1);
		become(ALICE);
		CHECK(heard(&accept) == FRIEND_ACCEPT && !friends.list[0].sent, "the accept deletes the sent line");
		reload(BOB);
		stored = friend_find(people[ALICE].public_key)->last_sequence;
		become(ALICE);
		record = record_at(FRIEND_HERE, BOB, stored, NULL, presence_body, 3);
		become(BOB);
		CHECK(heard(&record) == FRIEND_DROP_REPLAY, "at the stored sequence after a reload");
		become(ALICE);
		record = record_at(FRIEND_HERE, BOB, stored + 1, NULL, presence_body, 3);
		become(BOB);
		CHECK(heard(&record) == FRIEND_HERE, "above it");

		size = friend_body(body);
		record = record_at(FRIEND_ACCEPT, ALICE, now_ms() + 100, nonce, body, size);
		become(ALICE);
		CHECK(heard(&record) == FRIEND_DROP_NONCE, "a second accept with the used nonce");

		sent = request(ALICE, CARA);
		become(CARA);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Cara hears it");
		memcpy(nonce, friends.requests[0].nonce, sizeof(nonce));
		nonce[0] ^= 1;
		size = friend_body(body);
		record = record_at(FRIEND_ACCEPT, ALICE, now_ms() + 100, nonce, body, size);
		become(ALICE);
		CHECK(heard(&record) == FRIEND_DROP_NONCE && friend_find(people[CARA].public_key)->sent,
			"an accept with another nonce");
		nonce[0] ^= 1;
		become(CARA);
		record = record_at(FRIEND_ACCEPT, ALICE, now_ms() + 101, nonce, body, size);
		become(ALICE);
		CHECK(heard(&record) == FRIEND_ACCEPT && !friend_find(people[CARA].public_key)->sent, "the right nonce");
	}
	CASE("small_order")
	{
		static const unsigned char identity[32] = { 1 };
		struct sent record;
		int index;

		people_make();
		become(BOB);
		memset(&record, 0, sizeof(record));
		snprintf(record.topic, sizeof(record.topic), "hceu/3/friend/%s/r/0123456789abcdef", friends.id);
		record.size = 200;
		record.bytes[0] = 1;
		record.bytes[1] = FRIEND_REQUEST;
		for (index = 2 + P2P_KEY_SIZE; index < record.size; index++)
			record.bytes[index] = (unsigned char)index;
		CHECK(heard(&record) == FRIEND_DROP_KEY, "an all-zero one-time key");
		record.bytes[2] = 1;
		CHECK(heard(&record) == FRIEND_DROP_KEY, "a one-time key of small order");

		become(ALICE);
		party_with(BOB);
		memcpy(party.members[1].friend_key, identity, sizeof(identity));
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_PLAYER, "a member's key of small order");
		memset(party.members[1].friend_key, 0, P2P_KEY_SIZE);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_PLAYER, "a member with no key");
		CHECK(friends.count == 0, "nothing added");
	}
	CASE("add")
	{
		people_make();
		party_with(BOB);
		CHECK(p2p_friend_add_member(0) == P2P_FRIEND_ADD_NO_PLAYER, "the player itself");
		CHECK(p2p_friend_add_member(2) == P2P_FRIEND_ADD_NO_PLAYER && p2p_friend_add_member(-1) == P2P_FRIEND_ADD_NO_PLAYER,
			"no member there");
		memcpy(party.members[1].friend_key, friends.public_key, P2P_KEY_SIZE);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_PLAYER, "the player's own key");
		party_with(BOB);
		brokers_up(0);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_SERVER, "no broker");
		brokers_up(4);
		party_name = "";
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_SERVER && friends.count == 0, "no name of its own");
		party_name = "Al\x01";
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_NO_SERVER && friends.count == 0, "a name recipients drop");
		party_name = names[ALICE];
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT && friends.count == 1 && friends.list[0].sent,
			"a sent line");
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT && friends.count == 1, "only once");
		CHECK(status_now().sent == 1, "REQUEST SENT");
		CHECK(p2p_friend_add_tag("k948jnl7") == P2P_FRIEND_ADD_LOOKING, "a typed tag in lower case");
		CHECK(p2p_friend_add_tag("K948JNL1") == P2P_FRIEND_ADD_NO_PLAYER, "1 is not in the alphabet");
		CHECK(p2p_friend_add_tag("K948JNL") == P2P_FRIEND_ADD_NO_PLAYER, "7 characters");
		CHECK(p2p_friend_add_tag("") == P2P_FRIEND_ADD_NO_PLAYER && p2p_friend_add_tag("K948JNL77") ==
			P2P_FRIEND_ADD_NO_PLAYER, "no tag or 9 characters");
		{
			int index;

			for (index = friends.count; index < P2P_FRIENDS_MAXIMUM; index++)
				friends.list[friends.count++] = friend_made(index, "P");
			party_with(CARA);
			CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_FULL, "64 friends");
		}
		server_mode = 1;
		memset(&friends, 0, sizeof(friends));
		CHECK(!p2p_friends_wanted(), "no friends on a dedicated server");
		server_mode = 0;
	}
	CASE("party_add")
	{
		struct sent sent, accept;

		people_make();
		become(ALICE);
		party_with(DAVE);
		memcpy(party.members[1].friend_key, key_of(CARA), P2P_KEY_SIZE);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "a request for the key in the record");
		published_count = 0;
		p2p_friends_update();
		sent = last_published(0);
		become(DAVE);
		CHECK(heard(&sent) < 0, "the name's owner cannot open it");
		become(CARA);
		CHECK(heard(&sent) == FRIEND_REQUEST, "the key's owner hears it");
		become(ALICE);
		friends.count = 0;
		party_with(BOB);
		strcpy(party.members[1].name, "Mallory");
		p2p_friends_mark(&party);
		CHECK(!party.members[0].friend && !party.members[1].friend, "no friend before");
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "a request to Bob's key");
		p2p_friends_mark(&party);
		CHECK(!party.members[0].friend && party.members[1].friend == 2, "request sent: %d", party.members[1].friend);
		published_count = 0;
		p2p_friends_update();
		sent = last_published(0);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Bob hears it");
		accept = answer(BOB, ALICE, 1);
		become(ALICE);
		CHECK(heard(&accept) == FRIEND_ACCEPT, "Alice hears the accept");
		CHECK(!strncmp(friends.list[0].name_tag, "Bob#", 4), "the name from the accept: %s", friends.list[0].name_tag);
		p2p_friends_mark(&party);
		CHECK(party.members[1].friend == 1, "friends after the accept: %d", party.members[1].friend);
	}
	CASE("drops_logged")
	{
		struct sent bad;

		people_make();
		become(ALICE);
		memset(&bad, 0, sizeof(bad));
		snprintf(bad.topic, sizeof(bad.topic), "hceu/3/friend/%s/r/%016x", friends.id, 1);
		p2p_friends_update();
		CHECK(!strstr(log_text, "dropped"), "nothing dropped, nothing logged: %s", log_text);
		CHECK(received(&bad, FRIEND_DROP_EMPTY) == 1, "an empty record");
		bad.size = 321;
		CHECK(received(&bad, FRIEND_DROP_LONG) == 1, "a long record");
		bad.size = 40;
		bad.bytes[0] = 1;
		bad.bytes[1] = FRIEND_REQUEST;
		CHECK(received(&bad, FRIEND_DROP_SHORT) == 1 && received(&bad, FRIEND_DROP_SHORT) == 1, "two short records");
		p2p_friends_update();
		CHECK(strstr(log_text, "friends: 4 records dropped: 1 empty, 1 long, 2 short\n"), "the counts by reason: %s", log_text);
		CHECK(occurrences(log_text, "dropped") == 1, "one line: %s", log_text);
		CHECK(!strstr(log_text, friends.id) && !strstr(log_text, "hceu/"), "no id or topic: %s", log_text);
		log_text[0] = 0;
		CHECK(received(&bad, FRIEND_DROP_SHORT) == 1, "another short record");
		test_now += 60;
		p2p_friends_update();
		CHECK(!log_text[0], "not again within the hour: %s", log_text);
		test_now += 3600;
		p2p_friends_update();
		CHECK(strstr(log_text, "friends: 1 record dropped: 1 short\n"), "the next hour: %s", log_text);
		log_text[0] = 0;
		p2p_friends_update();
		CHECK(!log_text[0], "counted once: %s", log_text);
	}
	CASE("junk")
	{
		struct sent record;
		int index;

		people_make();
		become(BOB);
		memset(&record, 0, sizeof(record));
		record.size = 200;
		record.bytes[0] = 1;
		record.bytes[1] = FRIEND_REQUEST;
		for (index = 0; index < 1000; index++)
		{
			int at;

			snprintf(record.topic, sizeof(record.topic), "hceu/3/friend/%s/r/%016x", friends.id, index);
			for (at = 2; at < record.size; at++)
				record.bytes[at] = (unsigned char)(at * 7 + index * 13 + (index >> 8));
			memcpy(record.bytes + 2, &index, sizeof(index));
			p2p_friends_received(record.topic, record.bytes, record.size);
		}
		CHECK(friends.opened == 32, "32 opened: %d", friends.opened);
		CHECK(friends.drops[-FRIEND_DROP_SEAL] + friends.drops[-FRIEND_DROP_KEY] == 32 &&
			friends.drops[-FRIEND_DROP_FLOOD] == 968, "the rest dropped and counted: %d, %d",
			friends.drops[-FRIEND_DROP_SEAL], friends.drops[-FRIEND_DROP_FLOOD]);
		CHECK(occurrences(log_text, "friends: ") == 1, "one log line: %s", log_text);
	}
	CASE("inbox_restart")
	{
		char inbox[17];

		people_make();
		befriend(ALICE, BOB);
		snprintf(inbox, sizeof(inbox), "%s", friends.inbox);
		reload(ALICE);
		CHECK(!strcmp(friends.inbox, inbox), "the player's own inbox");
		CHECK(!strcmp(friend_find(people[BOB].public_key)->inbox, people[BOB].inbox), "Bob's inbox: %s",
			friend_find(people[BOB].public_key)->inbox);
		reload(BOB);
		CHECK(!strcmp(friend_find(people[ALICE].public_key)->inbox, people[ALICE].inbox), "Alice's inbox");
	}
	CASE("answers")
	{
		struct sent sent, accept, clear, again;
		unsigned char body[FRIEND_BODY_MAXIMUM];
		char topic[FRIEND_TOPIC_SIZE];
		int size;

		people_make();
		sent = request(ALICE, BOB);
		CHECK(sent.kept == 604800, "a request kept 7 days: %d", sent.kept);
		CHECK(status_now().sent == 1, "REQUEST SENT");
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST && status_now().request_count == 1, "Bob's request row");
		CHECK(!strncmp(status_now().requests[0].name_tag, "Alice#", 6), "its name: %s", status_now().requests[0].name_tag);
		names[BOB] = "";
		become(BOB);
		published_count = 0;
		p2p_friend_answer(status_now().requests[0].id, 1);
		p2p_friends_update();
		CHECK(published_count == 0 && status_now().request_count == 1 && !friend_find(people[ALICE].public_key),
			"no YES without a name of its own: the row stays");
		names[BOB] = "Bob";
		accept = answer(BOB, ALICE, 1);
		clear = last_published(1);
		CHECK(!strcmp(clear.topic, sent.topic) && clear.kept == -1, "YES clears the request: %s", clear.topic);
		CHECK(accept.kept == 604800 && status_now().request_count == 0, "the accept, and no row");
		CHECK(friend_find(people[ALICE].public_key) && !friend_find(people[ALICE].public_key)->sent, "Bob's friend");
		become(ALICE);
		CHECK(heard(&accept) == FRIEND_ACCEPT && status_now().sent == 0, "REQUEST SENT clears");
		CHECK(!strncmp(friend_find(people[BOB].public_key)->name_tag, "Bob#", 4), "Bob's name");

		sent = request(ALICE, CARA);
		become(CARA);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Cara hears Alice");
		answer(CARA, ALICE, 1);
		test_now++;
		reload(ALICE);
		published_count = 0;
		p2p_friends_update();
		again = last_published(0);
		become(CARA);
		published_count = 0;
		CHECK(heard(&again) == FRIEND_REQUEST, "Alice's request again, its accept lost");
		p2p_friends_update();
		accept = last_published(0);
		CHECK(friend_pair_topic(key_of(ALICE), key_of(ALICE), topic) && !strcmp(accept.topic, topic) &&
			accept.kept == 604800, "Cara accepts again: %s", accept.topic);
		become(ALICE);
		CHECK(heard(&accept) == FRIEND_ACCEPT && status_now().sent == 0, "Alice's REQUEST SENT clears");
		become(CARA);
		CHECK(heard(&again) == FRIEND_DROP_REPLAY, "the same request again: no second accept");

		sent = request(CARA, BOB);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Cara's request");
		clear = answer(BOB, CARA, 0);
		CHECK(!strcmp(clear.topic, sent.topic) && clear.kept == -1 && published_count == 1, "NO clears it, no accept");
		CHECK(!friend_find(people[CARA].public_key) && status_now().request_count == 0, "no friend, no row");
		become(CARA);
		size = friend_body(body);
		again = record_at(FRIEND_REQUEST, BOB, now_ms() + 100, friend_find(people[BOB].public_key)->nonce, body, size);
		become(BOB);
		CHECK(heard(&again) == FRIEND_DROP_REFUSED, "NO turns Cara away for the session");
		reload(BOB);
		CHECK(heard(&again) == FRIEND_REQUEST, "and only the session");

		sent = request(DAVE, BOB);
		copy_of(DAVE);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Dave's request");
		accept = answer(BOB, DAVE, 1);
		become(DAVE);
		CHECK(heard(&accept) == FRIEND_ACCEPT, "one copy of Dave");
		become(COPY);
		CHECK(heard(&accept) == FRIEND_ACCEPT, "the other copy");
	}
	CASE("answer_full")
	{
		struct sent sent;
		char id[17];
		int index;

		people_make();
		sent = request(ALICE, BOB);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Bob's request row");
		for (index = friends.count; index < P2P_FRIENDS_MAXIMUM; index++)
			friends.list[friends.count++] = friend_made(index, "P");
		friend_id(key_of(ALICE), id);
		friends.out_count = 0;
		CHECK(p2p_friend_answer(id, 1) == P2P_FRIEND_ADD_FULL, "YES on a full list");
		CHECK(friends.out_count == 0, "Alice's request record stays: %d out", friends.out_count);
		CHECK(status_now().request_count == 1 && !friend_find(key_of(ALICE)), "the row stays, no friend");
		friends.count--;
		CHECK(p2p_friend_answer(id, 1) == P2P_FRIEND_ADD_SENT && status_now().request_count == 0 &&
			friend_find(key_of(ALICE)), "YES with room");
	}
	CASE("answer_forms")
	{
		struct sent sent, other, accept, clear;

		people_make();
		sent = request(ALICE, BOB);
		become(BOB);
		CHECK(heard(&sent) == FRIEND_REQUEST && status_now().request_count == 1, "Bob's request row");
		party_with(ALICE);
		published_count = 0;
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "ADD FRIEND on Alice answers her request");
		memset(&party, 0, sizeof(party));
		p2p_friends_update();
		accept = last_published(0);
		clear = last_published(1);
		CHECK(status_now().request_count == 0 && friend_find(key_of(ALICE)) && !friend_find(key_of(ALICE))->sent,
			"a friend, and no row");
		CHECK(!strcmp(clear.topic, sent.topic), "her request's record cleared: %s", clear.topic);
		party_with(ALICE);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_FRIEND, "ADD FRIEND on a friend");
		memset(&party, 0, sizeof(party));
		become(ALICE);
		CHECK(heard(&accept) == FRIEND_ACCEPT && friend_find(key_of(BOB)) && !friend_find(key_of(BOB))->sent,
			"Alice hears the accept");

		sent = request(CARA, DAVE);
		other = request(DAVE, CARA);
		become(CARA);
		CHECK(heard(&other) == FRIEND_REQUEST && status_now().request_count == 1, "Cara has Dave's row");
		become(DAVE);
		CHECK(heard(&sent) == FRIEND_REQUEST, "Dave has Cara's row");
		accept = answer(DAVE, CARA, 1);
		become(CARA);
		published_count = 0;
		CHECK(heard(&accept) == FRIEND_ACCEPT && status_now().request_count == 0, "Dave's row goes with the friendship");
		p2p_friends_update();
		clear = last_published(1);
		CHECK(!strcmp(clear.topic, other.topic) && !strcmp(clear.topic, accept.topic),
			"Dave's request and accept records cleared: %s", clear.topic);
	}
	CASE("list_safety")
	{
		static char text[65536], again[65536];
		char path[600];
		struct friend future = friend_made(4, "Later");

		people_make();
		list_path_of(ALICE, ".tmp", path);
		text_set(path, "stale");
		future.sent = 0;
		snprintf(future.inbox, sizeof(future.inbox), "00000000000000cc");
		future.last_day = friend_today() + 5;
		line_of(&future, text);
		list_path_of(ALICE, "", path);
		text_set(path, text);
		reload(ALICE);
		list_path_of(ALICE, ".tmp", path);
		CHECK(access(path, F_OK) != 0, "a stale .tmp deleted at load");
		CHECK(friends.count == 1, "a friend last heard on a future day is kept");

		list_path_of(ALICE, "", path);
		write_short = 1;
		friends.list[0].last_sequence = 999999;
		CHECK(!friends_save(friends.list_path), "a short write");
		write_short = 0;
		CHECK(text_of(path, again, sizeof(again)) > 0 && !strcmp(again, text), "the old file stays");

		copy_of(ALICE);
		become(ALICE);
		CHECK(friend_add(people[CARA].public_key, "Cara") == P2P_FRIEND_ADD_SENT, "one copy adds Cara");
		become(COPY);
		CHECK(friend_add(people[DAVE].public_key, "Dave") == P2P_FRIEND_ADD_SENT, "the other adds Dave");
		reload(ALICE);
		CHECK(friends.count == 3 && friend_find(people[CARA].public_key) && friend_find(people[DAVE].public_key),
			"two copies merge before writing: %d", friends.count);
	}
	CASE("publish")
	{
		char id[17];
		int index;

		people_make();
		become(ALICE);
		party_with(BOB);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "Alice's request to Bob");
		published_count = 0;
		memset(sent_total, 0, sizeof(sent_total));
		starve(1);
		p2p_friends_update();
		CHECK(published_count == 1 && sent_total[0] == 1 && !sent_total[1], "broker 0 took it: %d", published_count);
		p2p_friends_update();
		CHECK(published_count == 1, "1 of 4 brokers: not again to the one that took it");
		test_ms += 1000;
		p2p_friends_update();
		CHECK(published_count == 1 && sent_total[0] == 1 && sent_total[1] == 1 && sent_total[2] == 1 &&
			sent_total[3] == 1, "the others once they have room: %d %d %d %d", sent_total[0], sent_total[1],
			sent_total[2], sent_total[3]);
		for (index = 1; index < BROKERS; index++)
			CHECK(last_copy_size[index] == published[0].size && !memcmp(last_copy[index], published[0].bytes,
				(size_t)published[0].size), "the same record to broker %d", index);
		p2p_friends_update();
		p2p_friends_update();
		CHECK(sent_total[0] + sent_total[1] + sent_total[2] + sent_total[3] == 4, "all 4: done");
		brokers_up(0);
		p2p_friends_update();
		brokers_up(4);
		p2p_friends_update();
		CHECK(published_count == 2 && sent_total[3] == 2, "again at the next connect: %d", published_count);
		test_now += 8 * 86400;
		brokers_up(0);
		p2p_friends_update();
		brokers_up(4);
		p2p_friends_update();
		CHECK(published_count == 2, "not after 7 days");
		test_now -= 8 * 86400;

		become(BOB);
		heard(&published[0]);
		published_count = 0;
		memset(sent_total, 0, sizeof(sent_total));
		friend_id(key_of(ALICE), id);
		p2p_friend_answer(id, 1);
		starve(1);
		p2p_friends_update();
		CHECK(published_count == 2 && friends.out_count == 2, "the clear and the accept wait");
		p2p_friends_update();
		CHECK(published_count == 2, "not again to the one that took them");
		test_ms += 1000;
		p2p_friends_update();
		p2p_friends_update();
		CHECK(published_count == 2 && sent_total[3] == 2 && friends.out_count == 0, "and done");
		for (index = 0; index < published_count; index++)
			CHECK(published[index].kept == (published[index].size ? 604800 : -1), "kept %d", published[index].kept);
	}
	CASE("presence_timer")
	{
		struct sent hello, here, bye;
		struct p2p_friends_status status;

		people_make();
		befriend(ALICE, BOB);
		befriend(BOB, CARA);
		become(BOB);
		p2p_friends_update();
		become(ALICE);
		presence_count = 0;
		p2p_friends_update();
		hello = last_presence();
		CHECK(presence_count == 1 && !strcmp(hello.topic + strlen(hello.topic) - 16, people[BOB].inbox),
			"Alice says hello to Bob alone: %d", presence_count);
		become(BOB);
		CHECK(heard(&hello) == FRIEND_HELLO, "Bob hears the hello");
		status = status_now();
		CHECK(status.online_count == 1 && status.rows[0].state == P2P_FRIEND_MENUS &&
			!strncmp(status.rows[0].name_tag, "Alice#", 6), "Alice is in menus: %d", status.online_count);

		test_now++;
		reload(ALICE);
		presence_count = 0;
		log_text[0] = 0;
		p2p_friends_update();
		hello = last_presence();
		CHECK(presence_count == 1 && strstr(log_text, "friends: 4 brokers, 1 hello"), "hello again: %s", log_text);
		become(BOB);
		presence_count = 0;
		CHECK(heard(&hello) == FRIEND_HELLO, "Bob hears it again");
		p2p_friends_update();
		CHECK(presence_count == 1, "one answer: %d", presence_count);
		here = last_presence();
		p2p_friends_update();
		CHECK(presence_count == 1, "and no more");
		become(ALICE);
		CHECK(heard(&here) == FRIEND_HERE && status_now().online_count == 1, "Alice hears the answer");

		become(BOB);
		test_now += 58;
		presence_count = 0;
		p2p_friends_update();
		CHECK(presence_count == 0, "nothing at 59 s");
		test_now++;
		p2p_friends_update();
		CHECK(presence_count == 1, "here at 60 s, to Alice alone: %d", presence_count);
		here = last_presence();
		p2p_friends_set_state(P2P_FRIEND_IN_GAME);
		p2p_friends_update();
		CHECK(presence_count == 2, "here on a change of state: %d", presence_count);
		p2p_friends_update();
		CHECK(presence_count == 2, "once");
		here = last_presence();
		become(ALICE);
		CHECK(heard(&here) == FRIEND_HERE && status_now().rows[0].state == P2P_FRIEND_IN_GAME, "in a game");

		test_now += 89;
		p2p_friends_update();
		CHECK(status_now().online_count == 1, "online at 89 s");
		test_now++;
		p2p_friends_update();
		CHECK(status_now().online_count == 0, "offline at 90 s");
		CHECK(heard(&here) == FRIEND_DROP_REPLAY, "the old record stays old");
		bye = presence_at(FRIEND_HERE, BOB, ALICE, P2P_FRIEND_MATCHMAKING, HALO_PORT_NETWORK_VERSION, now_ms() + 5000);
		become(ALICE);
		CHECK(heard(&bye) == FRIEND_HERE && status_now().rows[0].state == P2P_FRIEND_MATCHMAKING, "a new record");
		become(BOB);
		bye = record_at(FRIEND_BYE, ALICE, now_ms() + 6000, NULL, (const unsigned char *)"", 0);
		become(ALICE);
		CHECK(heard(&bye) == FRIEND_BYE && status_now().online_count == 0, "goodbye: offline at once");
	}
	CASE("hello_schedule")
	{
		int index, step;

		people_make();
		brokers_up(0);
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
			friends.list[friends.count++] = friend_made(2 * index, "Fan");
		p2p_friends_update();
		brokers_up(4);
		presence_count = 0;
		log_text[0] = 0;
		p2p_friends_update();
		CHECK(presence_count == 5, "the burst keeps 3 back: %d", presence_count);
		CHECK(strstr(log_text, "friends: 4 brokers, 64 hello"), "the log: %s", log_text);
		for (step = 0; step < 64 && presence_count < 64; step++)
		{
			test_ms += 125;
			p2p_friends_update();
		}
		CHECK(presence_count == 64 && test_ms <= 8000, "64 hellos in %lu ms: %d", test_ms, presence_count);
		for (index = 0; index < 64; index++)
		{
			char topic[64];

			snprintf(topic, sizeof(topic), P2P_FRIEND_TOPIC_PREFIX "p/%s", friends.list[index].inbox);
			CHECK(!strcmp(presence[index].topic, topic), "hello %d goes in the list's order: %s", index,
				presence[index].topic);
		}
		for (step = 0; step < 10; step++)
		{
			test_ms += 125;
			p2p_friends_update();
		}
		CHECK(presence_count == 64, "each once: %d", presence_count);
	}
	CASE("version")
	{
		struct sent hello;
		unsigned char body[4] = { P2P_FRIEND_IN_GAME, 0, HALO_PORT_NETWORK_VERSION, 0 };
		struct p2p_friends_status status;

		people_make();
		befriend(ALICE, BOB);
		hello = presence_at(FRIEND_HELLO, BOB, ALICE, P2P_FRIEND_IN_GAME, HALO_PORT_NETWORK_VERSION + 1, now_ms() + 1000);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_HELLO, "another version's hello");
		status = status_now();
		CHECK(status.online_count == 1 && status.rows[0].state == P2P_FRIEND_UPDATE, "UPDATE: %d",
			status.rows[0].state);
		hello = presence_at(FRIEND_HERE, BOB, ALICE, P2P_FRIEND_IN_GAME, HALO_PORT_NETWORK_VERSION, now_ms() + 2000);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_HERE && status_now().rows[0].state == P2P_FRIEND_IN_GAME, "this version's here");
		hello = presence_at(FRIEND_HERE, BOB, ALICE, 0, HALO_PORT_NETWORK_VERSION, now_ms() + 3000);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_DROP_BODY, "a state of 0");
		hello = presence_at(FRIEND_HERE, BOB, ALICE, 4, HALO_PORT_NETWORK_VERSION, now_ms() + 4000);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_DROP_BODY, "a state past IN_GAME");
		become(BOB);
		hello = record_at(FRIEND_HERE, ALICE, now_ms() + 5000, NULL, body, 2);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_DROP_BODY, "a short body");
		become(BOB);
		hello = record_at(FRIEND_HERE, ALICE, now_ms() + 6000, NULL, body, 4);
		become(ALICE);
		CHECK(heard(&hello) == FRIEND_DROP_BODY, "a long body");
		CHECK(status_now().rows[0].state == P2P_FRIEND_IN_GAME, "and the state stays");
	}
	CASE("two_copies")
	{
		struct sent first, second;

		people_make();
		befriend(ALICE, BOB);
		first = presence_at(FRIEND_HERE, BOB, ALICE, P2P_FRIEND_IN_GAME, HALO_PORT_NETWORK_VERSION, now_ms() + 1000);
		second = presence_at(FRIEND_HERE, BOB, ALICE, P2P_FRIEND_MENUS, HALO_PORT_NETWORK_VERSION, now_ms() + 2000);
		become(ALICE);
		CHECK(heard(&second) == FRIEND_HERE && status_now().rows[0].state == P2P_FRIEND_MENUS, "the newer");
		CHECK(heard(&first) == FRIEND_DROP_REPLAY && status_now().rows[0].state == P2P_FRIEND_MENUS,
			"the older one after it changes nothing");
		CHECK(heard(&second) == FRIEND_DROP_REPLAY, "nor the newer again");
	}
	CASE("here_recovery")
	{
		int second, offline_from = 0, flaps = 0;

		people_make();
		befriend(ALICE, BOB);
		presence_count = 0;
		dropped_from = ALICE;
		for (second = 0; second < 400; second++)
		{
			int who;

			if (second == 60)
				drops_left = 1;
			for (who = BOB; who >= ALICE; who--)
			{
				become(who);
				p2p_friends_update();
				deliver();
			}
			if (second >= 5)
			{
				become(BOB);
				if (!status_now().online_count && !offline_from)
					offline_from = second;
				become(ALICE);
				if (second >= 125)
				{
					flaps += !status_now().online_count;
					become(BOB);
					flaps += !status_now().online_count;
				}
			}
			test_now++;
		}
		CHECK(offline_from >= 89 && offline_from <= 91 && drops_left == 0, "Bob lost Alice at %d s, after the lost HERE",
			offline_from);
		CHECK(!flaps, "no flapping once Alice's next HERE is answered: %d", flaps);
		dropped_from = -1;
	}
	CASE("quit_byes")
	{
		int index, who;

		people_make();
		brokers_up(4);
		for (index = 0; index < 20; index++)
		{
			friends.list[friends.count] = friend_made(2 * index, "Fan");
			friends.list[friends.count].heard = 1;
			friends.list[friends.count++].heard_time = p2p_now();
		}
		for (who = 0; who < BROKERS; who++)
			signalling.brokers[who].publish_tokens = 4;
		p2p_friends_quit();
		for (who = 0; who < BROKERS; who++)
			CHECK(presence_to[who] == 20, "20 heard friends all get a BYE on broker %d: %d", who, presence_to[who]);
		for (index = 0; index < 20; index++)
		{
			char topic[64];

			snprintf(topic, sizeof(topic), P2P_FRIEND_TOPIC_PREFIX "p/%s", friends.list[index].inbox);
			CHECK(!strcmp(presence[index].topic, topic) && !friends.list[index].heard, "BYE %d to its friend: %s", index,
				presence[index].topic);
		}
		CHECK(flushed == 1, "written out at once: %d", flushed);
		p2p_friends_quit();
		CHECK(presence_to[0] == 20 && flushed == 2, "a second quit sends no more: %d", presence_to[0]);
	}
	CASE("drops_at_quit")
	{
		struct sent bad;

		people_make();
		become(ALICE);
		memset(&bad, 0, sizeof(bad));
		snprintf(bad.topic, sizeof(bad.topic), "hceu/3/friend/%s/r/%016x", friends.id, 1);
		CHECK(received(&bad, FRIEND_DROP_EMPTY) == 1, "an empty record");
		p2p_friends_update();
		CHECK(strstr(log_text, "friends: 1 record dropped: 1 empty\n"), "the first line: %s", log_text);
		log_text[0] = 0;
		CHECK(received(&bad, FRIEND_DROP_EMPTY) == 1, "another empty record");
		test_now += 60;
		p2p_friends_update();
		CHECK(!log_text[0], "not within the hour: %s", log_text);
		p2p_friends_quit();
		CHECK(strstr(log_text, "friends: 1 record dropped: 1 empty\n"), "logged at quit: %s", log_text);
		log_text[0] = 0;
		p2p_friends_quit();
		CHECK(!log_text[0], "counted once: %s", log_text);
	}
	CASE("quit_order")
	{
		struct sent bye;

		people_make();
		befriend(ALICE, BOB);
		friends.list[0].heard = 1;
		friends.list[0].heard_time = p2p_now();
		presence_count = 0;
		quit_events[0] = 0;
		lobby_quit();
		p2p_friends_quit();
		CHECK(!strcmp(quit_events, "TBD"), "the tombstone, the BYE, the DISCONNECT: %s", quit_events);
		CHECK(lost == 0 && disconnected == 1, "no BYE written after the DISCONNECT: %d lost", lost);
		CHECK(presence_count == 1, "one BYE to Bob: %d", presence_count);
		bye = last_presence();
		become(BOB);
		CHECK(heard(&bye) == FRIEND_BYE, "Bob hears it");
	}
	CASE("party_first")
	{
		int index, who;

		people_make();
		connected_fans(64);
		for (who = 0; who < BROKERS; who++)
			signalling.brokers[who].publish_tokens = PUBLISH_BURST;
		publish_everywhere("party", (const unsigned char *)"x", 1);
		p2p_friends_update();
		for (who = 0; who < BROKERS; who++)
			CHECK(party_to[who] == 1 && presence_to[who] == PUBLISH_BURST - 1 - UNPROVEN_RESERVE,
				"the party record, then the HELLOs that leave the reserve: broker %d %d %d", who, party_to[who],
				presence_to[who]);
		for (index = 0; index < 6; index++)
			publish_everywhere("search", (const unsigned char *)"x", 1);
		for (who = 0; who < BROKERS; who++)
			CHECK(party_to[who] == 1 + UNPROVEN_RESERVE, "the reserve is for the party and search records: %d",
				party_to[who]);
	}
	CASE("hello_later")
	{
		int who;

		people_make();
		brokers_up(0);
		connected_fans(3);
		brokers_up(1);
		log_text[0] = 0;
		p2p_friends_update();
		CHECK(presence_to[0] == 3 && !presence_to[1], "hello to the one broker: %d", presence_to[0]);
		brokers_up(4);
		p2p_friends_update();
		for (who = 1; who < BROKERS; who++)
			CHECK(presence_to[who] == 3, "hello to broker %d when it comes up: %d", who, presence_to[who]);
		CHECK(presence_to[0] == 3, "and none again to the first: %d", presence_to[0]);
		CHECK(strstr(log_text, "friends: 1 broker, 3 hello"), "the log: %s", log_text);
	}
	CASE("broker_swap")
	{
		people_make();
		brokers_up(0);
		connected_fans(3);
		brokers_mask(3);
		p2p_friends_update();
		CHECK(presence_to[0] == 3 && presence_to[1] == 3, "hello to brokers 0 and 1: %d %d", presence_to[0], presence_to[1]);
		brokers_mask(5);
		p2p_friends_update();
		CHECK(presence_to[2] == 3, "hello to broker 2, which came up as broker 1 went: %d", presence_to[2]);
		CHECK(presence_to[0] == 3 && presence_to[1] == 3, "nothing new to the others: %d %d", presence_to[0], presence_to[1]);
	}
	CASE("invite")
	{
		struct sent invite, dave;
		char id[17], name[48], expected[48];
		int state;

		people_make();
		befriend(ALICE, BOB);
		befriend(DAVE, BOB);
		become(ALICE);
		friends.list[0].heard = 1;
		friends.list[0].heard_time = p2p_now();
		friend_id(key_of(BOB), id);
		for (state = P2P_FRIEND_MENUS; state <= P2P_FRIEND_UPDATE; state++)
		{
			if (state == P2P_FRIEND_MATCHMAKING)
				continue;
			friends.list[0].state = state;
			p2p_friends_update();
			presence_count = 0;
			CHECK(!p2p_friend_invite(id), "no invite to a friend in state %d", state);
			p2p_friends_update();
			CHECK(!presence_count, "nothing sent to a friend in state %d: %d", state, presence_count);
		}
		friends.list[0].state = P2P_FRIEND_MATCHMAKING;
		led = 0;
		CHECK(!p2p_friend_invite(id), "no invite from a member of a party");
		led = 1;
		CHECK(!p2p_friend_invite("0000000000000000"), "no invite to an id that is no friend");
		invite = invited(ALICE, BOB);
		CHECK(invite.size <= 320 && !strcmp(invite.topic + strlen(invite.topic) - 16, people[BOB].inbox),
			"a record that fits, to Bob's inbox: %d %s", invite.size, invite.topic);
		become(BOB);
		p2p_friends_set_state(P2P_FRIEND_MENUS);
		CHECK(received(&invite, FRIEND_DROP_STATE) == 1, "dropped while Bob is not on MATCHMAKING");
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "nothing pending");
		invite = invited(ALICE, BOB);
		dave = invited(DAVE, BOB);
		become(BOB);
		p2p_friends_set_state(P2P_FRIEND_MATCHMAKING);
		CHECK(heard(&invite) == FRIEND_INVITE, "Bob takes Alice's invite on MATCHMAKING");
		snprintf(expected, sizeof(expected), "Alice#%s", people[ALICE].tag);
		CHECK(p2p_friend_invite_pending(name, sizeof(name)) == 1 && !strcmp(name, expected), "pending from %s: %s",
			expected, name);
		CHECK(received(&dave, FRIEND_DROP_BUSY) == 1, "Dave's invite waits out Alice's");
		CHECK(p2p_friend_invite_pending(name, sizeof(name)) == 1 && !strcmp(name, expected), "still Alice's: %s", name);
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_DONE && joined == 1 && !strcmp(joined_link, party_link),
			"YES joins the link: %d %s", joined, joined_link);
		CHECK(!clipboard_offers, "no invite touched the clipboard: %d", clipboard_offers);
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_DONE && p2p_friend_invite_answer(0) == P2P_FRIEND_ANSWER_DONE &&
			joined == 1, "an answer with no invite waiting is no refusal: %d", joined);
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "answered");
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a second invite");
		can_join = 0;
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_LEAVE_FIRST && joined == 1,
			"YES in a party of 2 joins nothing: %d", joined);
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "and the prompt ends");
		can_join = 1;
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a third invite");
		CHECK(p2p_friend_invite_answer(0) == P2P_FRIEND_ANSWER_DONE && joined == 1, "NO joins nothing: %d", joined);
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "NO ends the prompt");
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "an invite that cannot be joined");
		join_works = 0;
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_FAILED, "a join that fails is not LEAVE YOUR PARTY FIRST");
		join_works = 1;
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a fourth invite");
		p2p_friends_set_state(P2P_FRIEND_IN_GAME);
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "leaving MATCHMAKING clears the prompt");
		p2p_friends_set_state(P2P_FRIEND_MATCHMAKING);
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a fifth invite");
		invite = presence_at(FRIEND_BYE, ALICE, BOB, P2P_FRIEND_MENUS, HALO_PORT_NETWORK_VERSION, now_ms() + 90000);
		become(BOB);
		heard(&invite);
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "a friend who goes offline takes the invite back");
	}
	CASE("request_restart")
	{
		people_make();
		party_with(BOB);
		CHECK(p2p_friend_add_member(1) == P2P_FRIEND_ADD_SENT, "Alice's request to Bob");
		names[ALICE] = "";
		reload(ALICE);
		published_count = 0;
		p2p_friends_update();
		CHECK(published_count == 0, "no request without a name after a restart: %d", published_count);
		party_name = "Alice";
		p2p_friends_update();
		CHECK(published_count == 1, "the request once the name is back: %d", published_count);
		become(BOB);
		CHECK(heard(&published[0]) == FRIEND_REQUEST && !strncmp(status_now().requests[0].name_tag, "Alice#", 6),
			"Bob reads it: %s", status_now().requests[0].name_tag);
	}
	CASE("card_publish")
	{
		struct sent card, again;
		struct friend_record record;
		char expected[64];

		people_make();
		party_name = "";
		card_count = 0;
		p2p_friends_update();
		CHECK(card_count == 0, "no card without a name: %d", card_count);
		party_name = "Alice";
		p2p_friends_update();
		CHECK(card_count == 1, "a card once the name is known: %d", card_count);
		card = cards[0];
		card_topic_of(friends.tag, friends.id, expected);
		CHECK(!strcmp(card.topic, expected) && card.kept == FRIEND_CARD_SECONDS, "card topic %s, kept %d", card.topic, card.kept);
		CHECK(friend_record_read(card.bytes, card.size, now_ms(), &record) == FRIEND_CARD && record.body_size == 5 &&
			!memcmp(record.body, "Alice", 5) && !memcmp(record.sender, friends.public_key, P2P_KEY_SIZE), "the card reads back");
		CHECK(card_to[0] == 1 && card_to[3] == 1, "every broker took it: %d %d", card_to[0], card_to[3]);
		p2p_friends_update();
		CHECK(card_count == 1, "published once: %d", card_count);
		test_now += 60;
		brokers_mask(0);
		p2p_friends_update();
		brokers_up(4);
		p2p_friends_update();
		CHECK(card_count == 2, "renewed at the next connect: %d", card_count);
		again = cards[1];
		CHECK(!strcmp(again.topic, card.topic) && memcmp(again.bytes, card.bytes, (size_t)card.size), "a fresh signature");
	}
	CASE("lookup")
	{
		struct sent alice, forged, request, lent, empty, old;
		char typed[40], hash[17], expected[64], alice_tag[9], dave_tag[9];
		struct p2p_friends_status status;

		people_make();
		CHECK(status_now().lookup_result == P2P_FRIEND_ADD_LOOKING && !status_now().looking, "no lookup yet: %d",
			status_now().lookup_result);
		alice = card_of(ALICE);
		become(DAVE);
		strcpy(dave_tag, friends.tag);
		become(ALICE);
		strcpy(alice_tag, friends.tag);
		become(BOB);
		CHECK(received(&alice, FRIEND_DROP_STATE) == 1, "a card with no lookup running is dropped");
		CHECK(p2p_friend_add_tag(friends.tag) == P2P_FRIEND_ADD_SELF, "the player's own tag");
		snprintf(typed, sizeof(typed), "%s", alice_tag);
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_LOOKING, "looking");
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_LOOKING, "a second ask joins the lookup");
		p2p_friends_update();
		tag_hash(alice_tag, hash);
		snprintf(expected, sizeof(expected), P2P_FRIEND_TOPIC_PREFIX "t/%s/+", hash);
		CHECK(!strcmp(topic_lookup, expected), "lookup topic %s, expected %s", topic_lookup, expected);
		status = status_now();
		CHECK(status.looking == 1, "the status says looking");
		forged = card_at(CARA, now_ms(), alice_tag);
		become(BOB);
		CHECK(received(&forged, FRIEND_DROP_TAG) == 1, "a key whose tag is not the topic's is dropped (E14)");
		lent = alice;
		become(CARA);
		card_topic_of(alice_tag, friends.id, lent.topic);
		become(BOB);
		CHECK(received(&lent, FRIEND_DROP_TOPIC) == 1, "a card under another key's topic is dropped");
		empty = alice;
		empty.size = 0;
		CHECK(received(&empty, FRIEND_DROP_EMPTY) == 1, "an empty retained record changes nothing (E15)");
		old = card_at(ALICE, now_ms() - 31ULL * 86400 * 1000, NULL);
		CHECK(received(&old, FRIEND_DROP_AGE) == 1, "a card signed 31 days ago is dropped");
		names[ALICE] = "Al\x01";
		old = card_at(ALICE, now_ms(), NULL);
		names[ALICE] = "Alice";
		CHECK(received(&old, FRIEND_DROP_NAME) == 1, "a card with a control character in the name is dropped");
		p2p_friends_received(alice.topic, alice.bytes, alice.size);
		p2p_friends_received(alice.topic, alice.bytes, alice.size);
		p2p_friends_update();
		CHECK(status_now().looking == 1 && friends.count == 0, "nothing before the 3 s are over");
		published_count = 0;
		lookup_ends();
		status = status_now();
		CHECK(!status.looking && status.lookup_result == P2P_FRIEND_ADD_SENT, "found: %d", status.lookup_result);
		CHECK(friends.count == 1 && friends.list[0].sent && !strncmp(friends.list[0].name_tag, "Alice#", 6),
			"Alice is added as a sent request");
		CHECK(!topic_lookup[0], "the lookup topic is dropped: %s", topic_lookup);
		request = last_published(0);
		become(ALICE);
		CHECK(heard(&request) == FRIEND_REQUEST && !strncmp(status_now().requests[0].name_tag, "Bob#", 4),
			"Alice sees Bob's request (E2)");
		become(BOB);
		snprintf(typed, sizeof(typed), "%s", dave_tag);
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_LOOKING, "looking for Dave");
		p2p_friends_update();
		lookup_ends();
		status = status_now();
		CHECK(!status.looking && status.lookup_result == P2P_FRIEND_ADD_NO_PLAYER && friends.count == 1,
			"no card: no player (%d)", status.lookup_result);
		brokers_up(0);
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_NO_SERVER, "no broker");
		brokers_up(4);
		wildcards_off = 1;
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_NO_SERVER && !status_now().looking, "no broker with wildcards");
		wildcards_off = 0;
		party_name = "";
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_NO_SERVER && !status_now().looking, "no name: refused at once");
		party_name = names[BOB];
	}
	CASE("lookup_two")
	{
		struct sent alice, cara;
		char typed[40], alice_tag[9];
		struct p2p_friends_status status;

		people_make();
		alice = card_of(ALICE);
		strcpy(alice_tag, friends.tag);
		cara = card_at(CARA, now_ms(), alice_tag);
		become(BOB);
		snprintf(typed, sizeof(typed), "%s", alice_tag);
		CHECK(p2p_friend_add_tag(typed) == P2P_FRIEND_ADD_LOOKING, "looking");
		p2p_friends_update();
		tag_forced = alice_tag;
		p2p_friends_received(alice.topic, alice.bytes, alice.size);
		p2p_friends_received(cara.topic, cara.bytes, cara.size);
		tag_forced = NULL;
		published_count = 0;
		lookup_ends();
		status = status_now();
		CHECK(status.lookup_result == P2P_FRIEND_ADD_TWO_PLAYERS && friends.count == 0 && published_count == 0,
			"two players on one tag add no one: %d %d", status.lookup_result, friends.count);
	}
	CASE("invite_searching")
	{
		struct sent invite;
		char name[48];

		people_make();
		befriend(ALICE, BOB);
		memset(&party, 0, sizeof(party));
		party.search_state = P2P_MM_LOOKING;
		invite = invited(ALICE, BOB);
		become(BOB);
		p2p_friends_set_state(P2P_FRIEND_MATCHMAKING);
		CHECK(heard(&invite) == FRIEND_INVITE && p2p_friend_invite_pending(name, sizeof(name)),
			"an invite reaches Bob while he searches");
		steps[0] = 0;
		CHECK(p2p_friend_invite_answer(0) == P2P_FRIEND_ANSWER_DONE && !cancelled && !joined, "NO keeps searching: %d %d", cancelled, joined);
		CHECK(party.search_state == P2P_MM_LOOKING, "the search runs on");
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a second invite");
		can_join = 0;
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_LEAVE_FIRST && !cancelled, "YES with no joining ends nothing: %d", cancelled);
		can_join = 1;
		invite = invited(ALICE, BOB);
		become(BOB);
		CHECK(heard(&invite) == FRIEND_INVITE, "a third invite");
		CHECK(p2p_friend_invite_answer(1) == P2P_FRIEND_ANSWER_DONE && cancelled == 1 && joined == 1 && !strcmp(steps, "CJ"),
			"YES ends the search, then joins: %d %d %s", cancelled, joined, steps);
	}
	CASE("invite_strangers")
	{
		struct sent record;
		char name[48];

		people_make();
		befriend(ALICE, BOB);
		become(BOB);
		p2p_friends_set_state(P2P_FRIEND_MATCHMAKING);
		become(CARA);
		record = record_at(FRIEND_INVITE, BOB, now_ms() + 1000, NULL, (const unsigned char *)party_link, (int)strlen(party_link));
		become(BOB);
		CHECK(received(&record, FRIEND_DROP_STRANGER) == 1, "an invite from a stranger");
		become(ALICE);
		record = record_at(FRIEND_INVITE, BOB, now_ms() + 2000, NULL, (const unsigned char *)party_link, 0);
		become(BOB);
		CHECK(received(&record, FRIEND_DROP_BODY) == 1, "an invite without a link");
		become(ALICE);
		record = record_at(FRIEND_INVITE, BOB, now_ms() + 3000, NULL, (const unsigned char *)"hceu-party\nlink", 16);
		become(BOB);
		CHECK(received(&record, FRIEND_DROP_BODY) == 1, "an invite with a control character");
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "none pending");
	}
	CASE("remove")
	{
		struct sent here, invite;
		char id[17], path[600], text[1200], hex[2 * P2P_KEY_SIZE + 1], name[48];

		people_make();
		befriend(ALICE, BOB);
		befriend(ALICE, CARA);
		become(ALICE);
		p2p_friends_set_state(P2P_FRIEND_MATCHMAKING);
		friends.list[0].heard = 1;
		friends.list[0].heard_time = p2p_now();
		invite = invited(BOB, ALICE);
		become(ALICE);
		CHECK(heard(&invite) == FRIEND_INVITE && p2p_friend_invite_pending(name, sizeof(name)), "Bob's invite is pending");
		friend_id(key_of(BOB), id);
		p2p_friend_remove("0000000000000000");
		CHECK(friends.count == 2, "an id that is no friend removes nobody");
		p2p_friend_remove(id);
		CHECK(friends.count == 1 && !memcmp(friends.list[0].key, key_of(CARA), P2P_KEY_SIZE), "Bob is out, Cara stays");
		CHECK(!p2p_friend_invite_pending(name, sizeof(name)), "Bob's invite goes with him");
		CHECK(status_now().row_count == 1, "no row for Bob");
		list_path_of(ALICE, "", path);
		CHECK(text_of(path, text, sizeof(text)) > 0, "the list is saved");
		hex_of(people[BOB].public_key, P2P_KEY_SIZE, hex);
		CHECK(!strstr(text, hex), "the save omits Bob: %s", text);
		hex_of(people[CARA].public_key, P2P_KEY_SIZE, hex);
		CHECK(strstr(text, hex), "and keeps Cara");
		here = presence_at(FRIEND_HERE, BOB, ALICE, P2P_FRIEND_MATCHMAKING, HALO_PORT_NETWORK_VERSION, now_ms() + 5000);
		become(ALICE);
		CHECK(received(&here, FRIEND_DROP_STRANGER) == 1, "a HERE from Bob is from a stranger now");
		CHECK(status_now().row_count == 1 && friends.count == 1, "it does not bring the row back");
		reload(ALICE);
		CHECK(friends.count == 1, "nor does a restart: %d", friends.count);
		friend_id(key_of(CARA), id);
		rename_fails = 1;
		p2p_friend_remove(id);
		rename_fails = 0;
		CHECK(!friends.count, "Cara is out of the list");
		befriend(ALICE, DAVE);
		become(ALICE);
		CHECK(friends.count == 1 && !memcmp(friends.list[0].key, key_of(DAVE), P2P_KEY_SIZE),
			"a removal whose save failed holds at the next save: %d", friends.count);
		hex_of(people[CARA].public_key, P2P_KEY_SIZE, hex);
		CHECK(text_of(path, text, sizeof(text)) > 0 && !strstr(text, hex), "and the file omits Cara: %s", text);
	}
	CASE("remove_twice")
	{
		char id[17], path[600], text[1200], hex[2 * P2P_KEY_SIZE + 1];

		people_make();
		befriend(ALICE, BOB);
		befriend(ALICE, CARA);
		become(ALICE);
		rename_fails = 1;
		friend_id(key_of(BOB), id);
		p2p_friend_remove(id);
		friend_id(key_of(CARA), id);
		p2p_friend_remove(id);
		rename_fails = 0;
		CHECK(!friends.count, "both are out of the list");
		befriend(ALICE, DAVE);
		become(ALICE);
		CHECK(friends.count == 1 && !memcmp(friends.list[0].key, key_of(DAVE), P2P_KEY_SIZE),
			"both removals hold at the next save: %d", friends.count);
		list_path_of(ALICE, "", path);
		CHECK(text_of(path, text, sizeof(text)) > 0, "the list is saved");
		hex_of(people[BOB].public_key, P2P_KEY_SIZE, hex);
		CHECK(!strstr(text, hex), "the file omits Bob: %s", text);
		hex_of(people[CARA].public_key, P2P_KEY_SIZE, hex);
		CHECK(!strstr(text, hex), "and Cara: %s", text);
	}
	CASE("online_rows")
	{
		struct p2p_friends_status status;
		int index;

		people_make();
		befriend(ALICE, DAVE);
		befriend(ALICE, CARA);
		befriend(ALICE, BOB);
		become(ALICE);
		memset(&party, 0, sizeof(party));
		for (index = 0; index < 3; index++)
		{
			friends.list[index].heard = 1;
			friends.list[index].heard_time = p2p_now();
			friends.list[index].state = P2P_FRIEND_MATCHMAKING;
		}
		status = status_now();
		CHECK(status.online_count == 3 && !strncmp(status.rows[0].name_tag, "Bob#", 4) &&
			!strncmp(status.rows[1].name_tag, "Cara#", 5) && !strncmp(status.rows[2].name_tag, "Dave#", 5),
			"by name: %s %s %s", status.rows[0].name_tag, status.rows[1].name_tag, status.rows[2].name_tag);
		party_with(CARA);
		status = status_now();
		CHECK(status.online_count == 2 && !strncmp(status.rows[0].name_tag, "Bob#", 4) &&
			!strncmp(status.rows[1].name_tag, "Dave#", 5), "Cara is in the party, so not under FRIENDS: %d",
			status.online_count);
		memset(&party, 0, sizeof(party));
		for (index = 0; index < 3; index++)
			friends.list[index].heard = !strncmp(friends.list[index].name_tag, "Dave#", 5);
		status = status_now();
		CHECK(status.online_count == 1 && status.row_count == 3 && !strncmp(status.rows[0].name_tag, "Dave#", 5) &&
			!strncmp(status.rows[1].name_tag, "Bob#", 4) && status.rows[1].state == P2P_FRIEND_OFFLINE &&
			!strncmp(status.rows[2].name_tag, "Cara#", 5) && status.rows[2].state == P2P_FRIEND_OFFLINE,
			"online first, then offline by name: %s %s %s", status.rows[0].name_tag, status.rows[1].name_tag,
			status.rows[2].name_tag);
		for (index = 0; index < 3; index++)
			friends.list[index].sent = !strncmp(friends.list[index].name_tag, "Bob#", 4);
		status = status_now();
		CHECK(status.row_count == 2 && status.sent == 1, "a request not yet answered is no friend row: %d",
			status.row_count);
	}
	CASE("by_id")
	{
		struct p2p_friends_status status;
		struct sent invite = { { 0 } };
		char id[17], cara[17];
		int index;

		people_make();
		befriend(ALICE, DAVE);
		befriend(ALICE, CARA);
		befriend(ALICE, BOB);
		become(ALICE);
		memset(&party, 0, sizeof(party));
		for (index = 0; index < 3; index++)
		{
			friends.list[index].heard = 1;
			friends.list[index].heard_time = p2p_now();
			friends.list[index].state = P2P_FRIEND_MATCHMAKING;
		}
		status = status_now();
		strcpy(id, status.rows[0].id);
		strcpy(cara, status.rows[1].id);
		p2p_friend_remove(cara);
		presence_count = 0;
		CHECK(!p2p_friend_invite(cara), "no invite to Cara, who has gone from the list");
		CHECK(p2p_friend_invite(id) == 1, "an invite by the id shown, though the rows moved");
		p2p_friends_update();
		for (index = 0; index < presence_count; index++)
		{
			if (presence[index].bytes[1] == FRIEND_INVITE)
				invite = presence[index];
		}
		CHECK(!strcmp(invite.topic + strlen(invite.topic) - 16, people[BOB].inbox), "it went to Bob: %s", invite.topic);
		p2p_friend_remove(id);
		CHECK(friends.count == 1 && !memcmp(friends.list[0].key, key_of(DAVE), P2P_KEY_SIZE), "the removal took Bob only");
	}
	return 0;
}
