/* P2P_MATCHMAKING.C: MATCHMAKING's signed records, queues and search */

#include "p2p_matchmaking.h"
#include "halo_port_limits.h"
#include "master_list.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <time.h>

void platform_log(const char *format, ...);

static const char record_label[] = "hceu-mm-1";
static const char party_label[] = "hceu-mm-party-1";

const char *const p2p_mm_playlist_names[NUMBER_OF_P2P_MM_PLAYLISTS] = { "slayer", "ctf", "king", "oddball", "race" };
const char *const p2p_mm_size_names[NUMBER_OF_P2P_MM_SIZES] = { "1v1", "2v2", "3v3", "4v4", "ffa6", "big-team", "big-ffa" };

void mm_topic(int playlist, int size, char *topic, int topic_size)
{
	snprintf(topic, (size_t)topic_size, "hceu/3/mm/%d/%s/%s", HALO_PORT_NETWORK_VERSION, p2p_mm_playlist_names[playlist],
		p2p_mm_size_names[size]);
}

void mm_party_topic(const unsigned char *secret, char *topic, int topic_size)
{
	unsigned char digest[P2P_SHA256_SIZE];
	char text[2 * P2P_SHA256_SIZE + 1];

	p2p_sha256(secret, P2P_PARTY_SECRET_SIZE, digest);
	p2p_hex(digest, 16, text);
	snprintf(topic, (size_t)topic_size, "hceu/3/party/%d/%s", HALO_PORT_NETWORK_VERSION, text);
}

int mm_topic_listed(const char *const *topics, int count, const char *topic)
{
	int index;

	for (index = 0; index < count; index++)
		if (topic[0] && !strcmp(topics[index], topic))
			return 1;
	return 0;
}

int mm_interval(int queues)
{
	if (queues <= 12)
		return MM_INTERVAL;
	return (queues * 10 + MM_RECORDS_PER_SECOND - 1) / MM_RECORDS_PER_SECOND;
}

void mm_put_long(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

unsigned long mm_get_long(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

/* 0: a small-order key, which has no X25519 form */
static int key_hash_of(const unsigned char *key, unsigned char *hash)
{
	unsigned char x25519[P2P_KEY_SIZE];

	if (!p2p_ed25519_to_x25519(key, x25519))
		return 0;
	p2p_key_hash(x25519, hash);
	return 1;
}

static int signed_message(const unsigned char *bytes, int size, unsigned char *message)
{
	memcpy(message, record_label, sizeof(record_label) - 1);
	memcpy(message + sizeof(record_label) - 1, bytes, (size_t)size);
	return (int)(sizeof(record_label) - 1) + size;
}

static int party_message(const unsigned char *leader_key, const unsigned char *party_hash, unsigned char *message)
{
	memcpy(message, party_label, sizeof(party_label) - 1);
	memcpy(message + sizeof(party_label) - 1, leader_key, P2P_KEY_SIZE);
	memcpy(message + sizeof(party_label) - 1 + P2P_KEY_SIZE, party_hash, P2P_KEY_HASH_SIZE);
	return (int)(sizeof(party_label) - 1) + P2P_KEY_SIZE + P2P_KEY_HASH_SIZE;
}

void mm_party_hash(const unsigned char *seal, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(seal, P2P_SHA256_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

void mm_party_sign(const unsigned char *seed, const unsigned char *leader_key, const unsigned char *party_hash,
	unsigned char *signature)
{
	unsigned char message[sizeof(party_label) + P2P_KEY_SIZE + P2P_KEY_HASH_SIZE], key[P2P_KEY_SIZE];

	p2p_ed25519_public(seed, key, NULL);
	p2p_ed25519_sign(seed, key, message, party_message(leader_key, party_hash, message), signature);
}

int mm_party_signed(const unsigned char *key, const unsigned char *leader_key, const unsigned char *party_hash,
	const unsigned char *signature)
{
	unsigned char message[sizeof(party_label) + P2P_KEY_SIZE + P2P_KEY_HASH_SIZE];

	return p2p_ed25519_verify(key, message, party_message(leader_key, party_hash, message), signature);
}

/* key hashes, not keys: a key and its negative are different bytes with one X25519 form */
static int members_signed(const struct mm_record *record)
{
	unsigned char hashes[MM_MAXIMUM_PARTY][P2P_KEY_HASH_SIZE];
	int member, other;

	memcpy(hashes[0], record->key_hash, P2P_KEY_HASH_SIZE);
	for (member = 1; member < record->party; member++)
	{
		if (!key_hash_of(record->member_keys[member - 1], hashes[member]))
			return 0;
		for (other = 0; other < member && memcmp(hashes[other], hashes[member], P2P_KEY_HASH_SIZE); other++)
			;
		if (other < member ||
			!mm_party_signed(record->member_keys[member - 1], record->key, record->party_hash,
				record->member_signatures[member - 1]))
		{
			return 0;
		}
	}
	return 1;
}

int mm_playlist_has_size(unsigned playlists, int size)
{
	return (size != P2P_MM_FFA_6 && size != P2P_MM_BIG_FFA) || playlists != 1u << P2P_MM_CTF;
}

int mm_record_write(const struct mm_record *record, const unsigned char *seed, unsigned char *bytes, int size)
{
	unsigned char message[sizeof(record_label) + MM_MAXIMUM_RECORD_SIZE];
	int at = 0, index;

	if (size < MM_MAXIMUM_RECORD_SIZE || record->pool_count < 0 || record->pool_count > MM_MAXIMUM_POOL ||
		record->party > MM_MAXIMUM_PARTY || (record->party > 1 && record->kind != MM_KIND_SEARCHING))
	{
		return 0;
	}
	bytes[at++] = 'H';
	bytes[at++] = 'M';
	bytes[at++] = record->party > 1 ? MM_FORMAT_PARTY : MM_FORMAT;
	bytes[at++] = (unsigned char)(record->network_version >> 8);
	bytes[at++] = (unsigned char)record->network_version;
	bytes[at++] = (unsigned char)record->kind;
	mm_put_long(bytes + at, record->sequence);
	at += 4;
	bytes[at++] = (unsigned char)record->interval;
	bytes[at++] = (unsigned char)((record->can_host ? MM_FLAG_CAN_HOST : 0) |
		(record->under_way ? MM_FLAG_UNDER_WAY : 0) | (record->held ? MM_FLAG_HELD : 0) |
		(record->red_players < MM_RED_MAXIMUM ? record->red_players : MM_RED_MAXIMUM) << MM_FLAG_RED_SHIFT);
	bytes[at++] = (unsigned char)record->playlist;
	bytes[at++] = (unsigned char)record->size;
	memcpy(bytes + at, record->key, P2P_KEY_SIZE);
	at += P2P_KEY_SIZE;
	if (record->party > 1)
	{
		bytes[at++] = (unsigned char)record->party;
		memcpy(bytes + at, record->party_hash, P2P_KEY_HASH_SIZE);
		at += P2P_KEY_HASH_SIZE;
		for (index = 0; index < record->party - 1; index++)
		{
			memcpy(bytes + at, record->member_keys[index], P2P_KEY_SIZE);
			at += P2P_KEY_SIZE;
			memcpy(bytes + at, record->member_signatures[index], P2P_SIGNATURE_SIZE);
			at += P2P_SIGNATURE_SIZE;
		}
	}
	if (record->kind == MM_KIND_HOSTING)
	{
		bytes[at++] = (unsigned char)record->players;
		bytes[at++] = (unsigned char)record->most_players;
		bytes[at++] = (unsigned char)record->pool_count;
		for (index = 0; index < record->pool_count; index++)
		{
			memcpy(bytes + at, record->pool[index], P2P_KEY_HASH_SIZE);
			at += P2P_KEY_HASH_SIZE;
			if (record->held)
			{
				memcpy(bytes + at, record->sealed[index], MM_SEALED_TOKEN_SIZE);
				at += MM_SEALED_TOKEN_SIZE;
			}
		}
		if (!record->held)
		{
			memcpy(bytes + at, record->token, P2P_TOKEN_SIZE);
			at += P2P_TOKEN_SIZE;
		}
	}
	p2p_ed25519_sign(seed, record->key, message, signed_message(bytes, at, message), bytes + at);
	return at + P2P_SIGNATURE_SIZE;
}

int mm_record_read(const unsigned char *bytes, int size, struct mm_record *record)
{
	unsigned char message[sizeof(record_label) + MM_MAXIMUM_RECORD_SIZE];
	int at = MM_HEADER_SIZE, index, entry;

	if (size < MM_HEADER_SIZE + P2P_SIGNATURE_SIZE || size > MM_MAXIMUM_RECORD_SIZE ||
		bytes[0] != 'H' || bytes[1] != 'M' || (bytes[2] != MM_FORMAT_PARTY && bytes[2] != MM_FORMAT))
	{
		return 0;
	}
	memset(record, 0, sizeof(*record));
	record->party = 1;
	record->network_version = (unsigned short)(bytes[3] << 8 | bytes[4]);
	record->kind = bytes[5];
	record->sequence = mm_get_long(bytes + 6);
	record->interval = bytes[10];
	record->can_host = (bytes[11] & MM_FLAG_CAN_HOST) != 0;
	record->under_way = (bytes[11] & MM_FLAG_UNDER_WAY) != 0;
	record->held = (bytes[11] & MM_FLAG_HELD) != 0;
	record->red_players = bytes[11] >> MM_FLAG_RED_SHIFT;
	record->playlist = bytes[12];
	record->size = bytes[13];
	memcpy(record->key, bytes + 14, P2P_KEY_SIZE);
	if (record->kind < MM_KIND_SEARCHING || record->kind > MM_KIND_CANCEL ||
		record->playlist >= NUMBER_OF_P2P_MM_PLAYLISTS || record->size >= NUMBER_OF_P2P_MM_SIZES ||
		record->interval < MM_MINIMUM_INTERVAL)
	{
		return 0;
	}
	if (bytes[2] == MM_FORMAT_PARTY)
	{
		if (record->kind != MM_KIND_SEARCHING || size < at + 1 + P2P_KEY_HASH_SIZE + P2P_SIGNATURE_SIZE)
			return 0;
		record->party = bytes[at++];
		memcpy(record->party_hash, bytes + at, P2P_KEY_HASH_SIZE);
		at += P2P_KEY_HASH_SIZE;
		if (record->party < 2 || !mm_party_size_allowed(record->party, record->size) ||
			size < at + (record->party - 1) * MM_MEMBER_SIZE + P2P_SIGNATURE_SIZE)
		{
			return 0;
		}
		for (index = 0; index < record->party - 1; index++)
		{
			memcpy(record->member_keys[index], bytes + at, P2P_KEY_SIZE);
			at += P2P_KEY_SIZE;
			memcpy(record->member_signatures[index], bytes + at, P2P_SIGNATURE_SIZE);
			at += P2P_SIGNATURE_SIZE;
		}
	}
	if (record->kind == MM_KIND_HOSTING)
	{
		if (size < at + 3 + P2P_SIGNATURE_SIZE)
			return 0;
		record->players = bytes[at++];
		record->most_players = bytes[at++];
		record->pool_count = bytes[at++];
		entry = P2P_KEY_HASH_SIZE + (record->held ? MM_SEALED_TOKEN_SIZE : 0);
		if (record->pool_count > MM_MAXIMUM_POOL ||
			size < at + record->pool_count * entry + (record->held ? 0 : P2P_TOKEN_SIZE) + P2P_SIGNATURE_SIZE)
		{
			return 0;
		}
		for (index = 0; index < record->pool_count; index++)
		{
			memcpy(record->pool[index], bytes + at, P2P_KEY_HASH_SIZE);
			at += P2P_KEY_HASH_SIZE;
			if (record->held)
			{
				memcpy(record->sealed[index], bytes + at, MM_SEALED_TOKEN_SIZE);
				at += MM_SEALED_TOKEN_SIZE;
			}
		}
		if (!record->held)
		{
			memcpy(record->token, bytes + at, P2P_TOKEN_SIZE);
			at += P2P_TOKEN_SIZE;
		}
	}
	if (size != at + P2P_SIGNATURE_SIZE)
		return 0;
	if (!p2p_ed25519_verify(record->key, message, signed_message(bytes, at, message), bytes + at))
		return 0;
	return key_hash_of(record->key, record->key_hash) && members_signed(record);
}

static int token_key(const unsigned char *seed, const unsigned char *other_key, unsigned char *key)
{
	unsigned char public_key[P2P_KEY_SIZE], secret[P2P_KEY_SIZE], other[P2P_KEY_SIZE], shared[P2P_KEY_SIZE];

	p2p_ed25519_public(seed, public_key, secret);
	if (!p2p_ed25519_to_x25519(other_key, other))
		return 0;
	p2p_x25519(shared, secret, other);
	p2p_sha256(shared, P2P_KEY_SIZE, key);
	return 1;
}

int mm_seal_token(const unsigned char *seed, const unsigned char *member_key, const unsigned char *token,
	unsigned char *sealed)
{
	unsigned char key[P2P_KEY_SIZE];

	return token_key(seed, member_key, key) &&
		p2p_seal(key, token, P2P_TOKEN_SIZE, sealed) == MM_SEALED_TOKEN_SIZE;
}

int mm_open_token(const unsigned char *seed, const unsigned char *host_key, const unsigned char *sealed,
	unsigned char *token)
{
	unsigned char key[P2P_KEY_SIZE];

	return token_key(seed, host_key, key) && p2p_open(key, sealed, MM_SEALED_TOKEN_SIZE, token) == P2P_TOKEN_SIZE;
}

/* every machine must sort alike: key hash, never clock or arrival order */
static int hash_before(const struct mm_unit *units, int a, int b)
{
	return memcmp(units[a].key_hash, units[b].key_hash, P2P_KEY_HASH_SIZE) < 0;
}

static void indices_in_order(const struct mm_unit *units, const int *indices, int count, int *order)
{
	int index, other;

	for (index = 0; index < count; index++)
	{
		for (other = index; other > 0 && hash_before(units, indices[index], order[other - 1]); other--)
			order[other] = order[other - 1];
		order[other] = indices[index];
	}
}

static int teams_split(const struct mm_unit *units, const int *pool, int count, int team)
{
	unsigned long sums = 1;
	int index, total = 0, sum;

	for (index = 0; index < count; index++)
	{
		sums |= sums << units[pool[index]].players;
		total += units[pool[index]].players;
	}
	for (sum = total - team; sum <= team; sum++)
		if (sum >= 0 && sums >> sum & 1)
			return 1;
	return 0;
}

int mm_pool(const struct mm_unit *units, int count, int players, int teamed, int *pool)
{
	int all[MM_MAXIMUM_UNITS + 1], order[MM_MAXIMUM_UNITS + 1], index, taken = 0, filled = 0, has_host = 0;

	for (index = 0; index < count; index++)
		all[index] = index;
	indices_in_order(units, all, count, order);
	for (index = 0; index < count && filled < players; index++)
	{
		const struct mm_unit *candidate = &units[order[index]];
		int can_host = candidate->can_host;

		if (candidate->failed)
			continue;
		if (filled + candidate->players > players)
			continue;
		if (!can_host && !has_host && filled + candidate->players == players)
			continue;
		pool[taken] = order[index];
		if (teamed && !teams_split(units, pool, taken + 1, players / 2))
			continue;
		taken++;
		filled += candidate->players;
		has_host |= can_host;
	}
	return filled == players && has_host ? taken : 0;
}

int mm_elect(const struct mm_unit *units, const int *pool, int pool_count)
{
	unsigned char material[MM_MAXIMUM_POOL * P2P_KEY_HASH_SIZE + P2P_KEY_SIZE], digest[P2P_SHA256_SIZE];
	unsigned char best[P2P_SHA256_SIZE];
	int sorted[MM_MAXIMUM_POOL], index, at = 0, unit, host = -1;

	if (pool_count > MM_MAXIMUM_POOL)
		return -1;
	/* sorted so every machine hashes the same bytes */
	indices_in_order(units, pool, pool_count, sorted);
	for (index = 0; index < pool_count; index++)
	{
		memcpy(material + at, units[sorted[index]].key_hash, P2P_KEY_HASH_SIZE);
		at += P2P_KEY_HASH_SIZE;
	}
	for (index = 0; index < pool_count; index++)
	{
		unit = pool[index];
		if (!units[unit].can_host || units[unit].failed)
			continue;
		memcpy(material + at, units[unit].key, P2P_KEY_SIZE);
		p2p_sha256(material, at + P2P_KEY_SIZE, digest);
		if (host < 0 || memcmp(digest, best, sizeof(best)) < 0)
		{
			host = unit;
			memcpy(best, digest, sizeof(best));
		}
	}
	return host;
}

int p2p_mm_size_players(int size)
{
	static const int players[NUMBER_OF_P2P_MM_SIZES] = { 2, 4, 6, 8, 6, 16, 16 };

	return players[size];
}

int p2p_mm_size_big(int size)
{
	return size >= P2P_MM_BIG_TEAM;
}

int mm_party_size_allowed(int party, int size)
{
	return party <= MM_MAXIMUM_PARTY && party * 2 <= p2p_mm_size_players(size);
}

/* each playlist's game engine (a listing's engine_type) */
static const int engines[NUMBER_OF_P2P_MM_PLAYLISTS] = { 2, 1, 4, 3, 5 };

static int party_players(const struct mm_search *search)
{
	return search->party.count > 1 ? search->party.count : 1;
}

void mm_wanted(const struct mm_search *search, int queue, struct p2p_lobby_wanted *wanted)
{
	int queue_size = search->queues[queue][1], playlist;

	memset(wanted, 0, sizeof(*wanted));
	for (playlist = 0; playlist < NUMBER_OF_P2P_MM_PLAYLISTS; playlist++)
	{
		if (!(search->choice.playlists & 1u << playlist))
			continue;
		if (!mm_playlist_has_size(1u << playlist, queue_size))
			continue;
		wanted->engines |= 1u << engines[playlist];
	}
	wanted->players = p2p_mm_size_players(queue_size);
	wanted->teams = queue_size != P2P_MM_FFA_6 && queue_size != P2P_MM_BIG_FFA;
	wanted->big = p2p_mm_size_big(queue_size);
	snprintf(wanted->region, sizeof(wanted->region), "%s", search->choice.region);
	wanted->party = party_players(search);
}

int mm_counted(const struct mm_peer *peer)
{
	return peer->refreshes >= MM_REFRESHES_COUNTED;
}

static void set_queues(struct mm_search *search)
{
	int playlist, size;

	search->queue_count = search->next_queue = 0;
	for (playlist = 0; playlist < NUMBER_OF_P2P_MM_PLAYLISTS; playlist++)
	{
		if (!(search->choice.playlists & 1u << playlist))
			continue;
		for (size = 0; size < NUMBER_OF_P2P_MM_SIZES; size++)
		{
			if (search->widened && !p2p_mm_size_big(search->choice.size) ? p2p_mm_size_big(size) :
				size != search->choice.size)
				continue;
			if (!mm_playlist_has_size(1u << playlist, size) || !mm_party_size_allowed(party_players(search), size))
				continue;
			search->queues[search->queue_count][0] = playlist;
			search->queues[search->queue_count++][1] = size;
		}
	}
}

void mm_start(struct mm_search *search, const unsigned char *seed, const struct p2p_mm_choice *choice,
	unsigned long now, unsigned long unix_now)
{
	unsigned long sequence = search->sequence;

	memset(search, 0, sizeof(*search));
	search->sequence = sequence;
	memcpy(search->seed, seed, P2P_SEED_SIZE);
	p2p_ed25519_public(seed, search->key, NULL);
	key_hash_of(search->key, search->key_hash);
	search->choice = *choice;
	search->started = unix_now;
	search->marks_from = search->next_record = now;
	set_queues(search);
	search->state = P2P_MM_LOOKING;
}

void mm_game_lost(struct mm_search *search, unsigned long now)
{
	struct p2p_mm_choice choice = search->choice;
	struct mm_search_party party = search->party;
	unsigned char seed[P2P_SEED_SIZE];
	unsigned long kept_start = search->started;
	int can_host = search->can_host;

	memcpy(seed, search->seed, sizeof(seed));
	mm_start(search, seed, &choice, now, kept_start);
	search->can_host = can_host;
	search->party = party;
	set_queues(search);
	if (party.following)
		search->state = P2P_MM_QUEUED;
}

void mm_game_left(struct mm_search *search, unsigned long now)
{
	unsigned char game[P2P_KEY_HASH_SIZE];

	memcpy(game, search->joining, sizeof(game));
	if (search->picked_set)
		lobby_mark_failed_unlocked(search->picked);
	mm_game_lost(search, now);
	memcpy(search->failed_game, game, sizeof(game));
	search->left_game = 1;
}

void mm_forget_game(struct mm_search *search, const unsigned char *key)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	int index;

	if (!key_hash_of(key, key_hash))
		return;
	if (!memcmp(search->failed_game, key_hash, P2P_KEY_HASH_SIZE))
		memset(search->failed_game, 0, P2P_KEY_HASH_SIZE);
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		struct mm_peer *peer = &search->peers[index];

		if (peer->used && peer->record.kind == MM_KIND_HOSTING &&
			!memcmp(peer->record.key_hash, key_hash, P2P_KEY_HASH_SIZE))
		{
			peer->used = 0;
		}
	}
}

void mm_follow(struct mm_search *search, const unsigned char *seed, const struct p2p_mm_choice *choice,
	const unsigned char *leader, int players, unsigned long now, unsigned long unix_now)
{
	int can_host = search->can_host;

	mm_start(search, seed, choice, now, unix_now);
	search->can_host = can_host;
	search->party.following = 1;
	search->party.count = players;
	memcpy(search->party.leader, leader, P2P_KEY_SIZE);
	key_hash_of(leader, search->party.leader_hash);
	set_queues(search);
	search->state = P2P_MM_QUEUED;
}

void mm_follow_game(struct mm_search *search, const char *invite, int playlist, int size, int public_game,
	unsigned long now)
{
	search->decision.kind = P2P_MM_DECIDE_JOIN;
	search->decision.playlist = playlist;
	search->decision.size = size;
	search->decision.public_game = public_game;
	snprintf(search->decision.invite, sizeof(search->decision.invite), "%s", invite);
	memset(search->joining, 0, sizeof(search->joining));
	search->joining_since = now;
	search->joining_players = 0;
	search->joining_most_players = p2p_mm_size_big(size) ? 0 : p2p_mm_size_players(size);
	search->joining_ping = -1;
	search->state = P2P_MM_FOUND;
	search->decided = P2P_MM_DECIDE_JOIN;
}

void mm_set_party(struct mm_search *search, const struct mm_search_party *party, unsigned long now)
{
	if (!memcmp(&search->party, party, sizeof(*party)))
		return;
	memcpy(&search->party, party, sizeof(*party));
	search->next_record = now;
	if (search->state == P2P_MM_LOOKING || search->state == P2P_MM_QUEUED)
		set_queues(search);
}

static int in_queue(const struct mm_search *search, int playlist, int size)
{
	int index;

	for (index = 0; index < search->queue_count; index++)
		if (search->queues[index][0] == playlist && search->queues[index][1] == size)
			return 1;
	return 0;
}

static struct mm_peer *find_peer(struct mm_search *search, const unsigned char *key_hash, int playlist, int size)
{
	int index;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		struct mm_peer *peer = &search->peers[index];

		if (peer->used && peer->playlist == playlist && peer->size == size &&
			!memcmp(peer->record.key_hash, key_hash, P2P_KEY_HASH_SIZE))
		{
			return peer;
		}
	}
	return NULL;
}

static struct mm_peer *take_slot(struct mm_search *search, unsigned long now)
{
	struct mm_peer *victim = NULL;
	int index;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		struct mm_peer *peer = &search->peers[index];

		if (!peer->used)
			return peer;
		if (mm_counted(peer))
			continue;
		if (!victim || now - peer->heard > now - victim->heard)
			victim = peer;
	}
	return victim;
}

static int fits_its_size(const struct mm_record *record)
{
	if (p2p_mm_size_big(record->size))
		return record->kind != MM_KIND_SEARCHING && record->most_players <= HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	return record->kind != MM_KIND_HOSTING || record->most_players <= p2p_mm_size_players(record->size);
}

void mm_heard(struct mm_search *search, const struct mm_record *record, unsigned long now)
{
	struct mm_peer *peer;

	if (search->state == P2P_MM_OFF || record->network_version != HALO_PORT_NETWORK_VERSION ||
		!memcmp(record->key, search->key, P2P_KEY_SIZE) || !in_queue(search, record->playlist, record->size) ||
		!fits_its_size(record))
	{
		return;
	}
	peer = find_peer(search, record->key_hash, record->playlist, record->size);
	if (peer && record->sequence <= peer->record.sequence)
		return;
	if (!peer)
	{
		peer = take_slot(search, now);
		if (!peer)
			return;
		memset(peer, 0, sizeof(*peer));
		peer->used = 1;
		peer->refreshes = -1;
		peer->playlist = record->playlist;
		peer->size = record->size;
	}
	peer->record = *record;
	peer->heard = now;
	/* a cancelled entry stays as a tombstone, so older records of its key are dropped */
	if (record->kind == MM_KIND_CANCEL)
		peer->refreshes = -1;
	else
		peer->refreshes++;
	if (record->kind == MM_KIND_HOSTING && peer->failed == MM_FAILED_PASSED_OVER)
		peer->failed = MM_FAILED_NONE;
	if (record->kind == MM_KIND_HOSTING && !memcmp(record->key_hash, search->failed_game, P2P_KEY_HASH_SIZE))
		peer->failed = MM_FAILED_GAME_UNJOINABLE;
}

static void expire(struct mm_search *search, unsigned long now)
{
	int index;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		struct mm_peer *peer = &search->peers[index];

		if (peer->used && now - peer->heard >= (unsigned long)MM_EXPIRY_INTERVALS * peer->record.interval * 100)
			peer->used = 0;
	}
}

static struct mm_record base_record(struct mm_search *search, int kind, int playlist, int size, int interval)
{
	struct mm_record record;

	memset(&record, 0, sizeof(record));
	record.kind = kind;
	record.network_version = HALO_PORT_NETWORK_VERSION;
	record.sequence = ++search->sequence;
	record.interval = interval;
	record.can_host = search->can_host > 0 || search->party.member_hosts;
	record.playlist = playlist;
	record.size = size;
	memcpy(record.key, search->key, P2P_KEY_SIZE);
	if (kind == MM_KIND_SEARCHING && search->party.count > 1)
	{
		record.party = search->party.count;
		memcpy(record.party_hash, search->party.party_hash, P2P_KEY_HASH_SIZE);
		memcpy(record.member_keys, search->party.keys, sizeof(record.member_keys));
		memcpy(record.member_signatures, search->party.signatures, sizeof(record.member_signatures));
	}
	return record;
}

static int emit(const struct mm_search *search, const struct mm_record *record, struct mm_out *out)
{
	mm_topic(record->playlist, record->size, out->topic, sizeof(out->topic));
	out->size = mm_record_write(record, search->seed, out->bytes, sizeof(out->bytes));
	return out->size > 0;
}

static int cancel_except(struct mm_search *search, int keep_playlist, int keep_size, struct mm_out *out, int out_size)
{
	int interval = mm_interval(search->queue_count), index, kept = 0, count = 0;

	for (index = 0; index < search->queue_count; index++)
	{
		struct mm_record record;

		if (search->queues[index][0] == keep_playlist && search->queues[index][1] == keep_size)
		{
			search->queues[kept][0] = keep_playlist;
			search->queues[kept++][1] = keep_size;
		}
		else if (count < out_size && !search->party.following)
		{
			record = base_record(search, MM_KIND_CANCEL, search->queues[index][0], search->queues[index][1],
				interval);
			count += emit(search, &record, &out[count]);
		}
	}
	search->queue_count = kept;
	search->next_queue = 0;
	return count;
}

int mm_cancel(struct mm_search *search, struct mm_out *out, int out_size)
{
	int count = cancel_except(search, -1, -1, out, out_size);

	search->in_game = 0;
	search->party.following = 0;
	search->state = P2P_MM_OFF;
	return count;
}

static int make_units(const struct mm_search *search, int playlist, int size, struct mm_unit *units)
{
	int count = 1, index;

	memcpy(units[0].key, search->key, P2P_KEY_SIZE);
	memcpy(units[0].key_hash, search->key_hash, P2P_KEY_HASH_SIZE);
	units[0].can_host = search->can_host > 0 || search->party.member_hosts;
	units[0].failed = 0;
	units[0].players = party_players(search);
	if (search->party.following)
	{
		struct mm_peer *leader = find_peer((struct mm_search *)search, search->party.leader_hash, playlist, size);

		if (!leader || leader->record.kind != MM_KIND_SEARCHING || !mm_counted(leader))
			return 0;
		memcpy(units[0].key, leader->record.key, P2P_KEY_SIZE);
		memcpy(units[0].key_hash, leader->record.key_hash, P2P_KEY_HASH_SIZE);
		units[0].can_host = leader->record.can_host;
		units[0].players = leader->record.party;
	}
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		const struct mm_peer *peer = &search->peers[index];

		if (!peer->used || peer->playlist != playlist || peer->size != size ||
			peer->record.kind != MM_KIND_SEARCHING || !mm_counted(peer) ||
			(search->party.following && !memcmp(peer->record.key_hash, search->party.leader_hash, P2P_KEY_HASH_SIZE)))
		{
			continue;
		}
		memcpy(units[count].key, peer->record.key, P2P_KEY_SIZE);
		memcpy(units[count].key_hash, peer->record.key_hash, P2P_KEY_HASH_SIZE);
		units[count].can_host = peer->record.can_host;
		units[count].failed = peer->failed != MM_FAILED_NONE;
		units[count].players = peer->record.party;
		count++;
	}
	return count;
}

/* the party's one host: its leader, or the member its party record names */
static int hosts_for_party(const struct mm_search *search)
{
	return search->party.following ? search->party.member_hosts : !search->party.member_hosts;
}

static int decide_host(struct mm_search *search, int playlist, int size, struct mm_out *out, int out_size)
{
	search->decision.kind = P2P_MM_DECIDE_HOST;
	search->decision.playlist = search->hosting_playlist = playlist;
	search->decision.size = search->hosting_size = size;
	search->state = P2P_MM_FOUND;
	search->decided = P2P_MM_DECIDE_HOST;
	return cancel_except(search, playlist, size, out, out_size);
}

static void await_host(struct mm_search *search, const struct mm_unit *host, unsigned long now)
{
	struct mm_peer *peer;

	if (memcmp(search->joining, host->key_hash, P2P_KEY_HASH_SIZE))
	{
		memcpy(search->joining, host->key_hash, P2P_KEY_HASH_SIZE);
		search->joining_since = now;
	}
	else if (now - search->joining_since >= MM_HOST_WAIT)
	{
		peer = find_peer(search, host->key_hash, search->pool_playlist, search->pool_size);
		if (peer)
			peer->failed = MM_FAILED_PASSED_OVER;
	}
}

static int settle(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	struct mm_unit units[MM_MAXIMUM_UNITS + 1];
	int pool[MM_MAXIMUM_POOL];
	int queue, member, taken = 0, host, same;

	for (queue = 0; queue < search->queue_count && !taken; queue++)
	{
		taken = mm_pool(units, make_units(search, search->queues[queue][0], search->queues[queue][1], units),
			p2p_mm_size_players(search->queues[queue][1]), search->queues[queue][1] != P2P_MM_FFA_6, pool);
		for (member = 0; member < taken && pool[member] != 0; member++)
			;
		if (member == taken)
			taken = 0;
	}
	if (!taken)
	{
		search->pool_count = 0;
		return 0;
	}
	queue--;
	same = search->pool_count == taken && search->pool_playlist == search->queues[queue][0] &&
		search->pool_size == search->queues[queue][1];
	for (member = 0; member < taken; member++)
		same = same && !memcmp(search->pool[member], units[pool[member]].key_hash, P2P_KEY_HASH_SIZE);
	if (!same)
	{
		search->pool_count = taken;
		search->pool_playlist = search->queues[queue][0];
		search->pool_size = search->queues[queue][1];
		for (member = 0; member < taken; member++)
			memcpy(search->pool[member], units[pool[member]].key_hash, P2P_KEY_HASH_SIZE);
		search->pool_since = now;
		memset(search->joining, 0, sizeof(search->joining));
		return 0;
	}
	if (now - search->pool_since >= MM_SETTLE)
	{
		host = mm_elect(units, pool, taken);
		if (host == 0 && hosts_for_party(search))
		{
			return decide_host(search, search->pool_playlist, search->pool_size, out, out_size);
		}
		if (host > 0)
			await_host(search, &units[host], now);
	}
	return 0;
}

static int searchers_heard(const struct mm_search *search, int playlist, int size, int *lower)
{
	const unsigned char *own = search->party.following ? search->party.leader_hash : search->key_hash;
	int index, count = 0;

	*lower = 0;
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		const struct mm_peer *peer = &search->peers[index];

		if (!peer->used || peer->playlist != playlist || peer->size != size ||
			peer->record.kind != MM_KIND_SEARCHING || !mm_counted(peer))
		{
			continue;
		}
		if (!memcmp(peer->record.key_hash, own, P2P_KEY_HASH_SIZE))
			continue;
		count++;
		*lower |= peer->record.can_host && !peer->failed &&
			memcmp(peer->record.key_hash, own, P2P_KEY_HASH_SIZE) < 0;
	}
	return count;
}

static int host_open(struct mm_search *search, int forced, struct mm_out *out, int out_size)
{
	int playlist, best = -1, most = -1, count, lower, best_lower = 0;

	if (!mm_party_size_allowed(party_players(search), search->choice.size))
		return 0;
	for (playlist = 0; playlist < NUMBER_OF_P2P_MM_PLAYLISTS; playlist++)
	{
		if (!(search->choice.playlists & 1u << playlist) || !mm_playlist_has_size(1u << playlist, search->choice.size))
			continue;
		count = searchers_heard(search, playlist, search->choice.size, &lower);
		if (count > most)
		{
			best = playlist;
			most = count;
			best_lower = lower;
		}
	}
	if (best < 0 || (best_lower && !forced))
		return 0;
	search->host_now = 0;
	search->pool_count = 0;
	return decide_host(search, best, search->choice.size, out, out_size);
}

static void widen(struct mm_search *search)
{
	search->widened = 1;
	set_queues(search);
}

void mm_widen_now(struct mm_search *search)
{
	if (search->state == P2P_MM_QUEUED && !search->widened)
		widen(search);
}

void mm_host_now(struct mm_search *search)
{
	if (search->state == P2P_MM_QUEUED && !search->party.following)
		search->host_now = 1;
}

static int marks(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	if (p2p_mm_size_big(search->choice.size))
		return search->can_host > 0 && hosts_for_party(search) ? host_open(search, 1, out, out_size) : 0;
	if (!search->party.following && !search->widened && now - search->marks_from >= MM_WIDEN)
		widen(search);
	if (search->can_host > 0 && !search->pool_count && now - search->marks_from >= MM_HOST && hosts_for_party(search))
		return host_open(search, 0, out, out_size);
	return 0;
}

static int pool_index(const struct mm_record *record, const unsigned char *key_hash)
{
	int index;

	for (index = 0; index < record->pool_count; index++)
		if (!memcmp(record->pool[index], key_hash, P2P_KEY_HASH_SIZE))
			return index;
	return -1;
}

/* at equal players only the higher key hash moves, so two lobbies never swap */
static int outranks(const struct mm_search *search, const struct mm_record *other)
{
	return (!p2p_mm_size_big(search->hosting_size) || search->players == 1) &&
		other->players + search->players <= other->most_players &&
		(other->players > search->players ||
			(other->players == search->players &&
				memcmp(search->key_hash, other->key_hash, P2P_KEY_HASH_SIZE) > 0));
}

static int pick_game(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	static struct p2p_listing games[MAXIMUM_GAMES];
	struct p2p_lobby_wanted wanted;
	int looking = search->state == P2P_MM_LOOKING, count, queue, other, found, index = -1, picked_queue = 0;
	int waiting = 0, playlist;

	count = lobby_games_unlocked(games, MAXIMUM_GAMES);
	search->games_checked = count;
	for (queue = 0; queue < search->queue_count; queue++)
	{
		int size = search->queues[queue][1], chosen = size == search->choice.size, better;

		for (other = 0; other < queue && search->queues[other][1] != size; other++)
			;
		if (other < queue)
			continue;
		mm_wanted(search, queue, &wanted);
		waiting += p2p_lobby_pick_unpinged(games, count, &wanted);
		waiting += wanted.big && !p2p_lobby_masters_were_heard();
		found = p2p_lobby_pick(games, count, &wanted);
		/* the brokers' games arrive after the masters' list, and a player's game beats a server when no region is set */
		waiting += found >= 0 && games[found].dedicated && !wanted.region[0];
		if (found < 0)
			continue;
		better = index < 0;
		if (!better)
		{
			int held_chosen = search->queues[picked_queue][1] == search->choice.size;

			better = chosen != held_chosen ? chosen :
				games[found].player_count > games[index].player_count ||
				(games[found].player_count == games[index].player_count && games[found].ping < games[index].ping);
		}
		if (better)
		{
			index = found;
			picked_queue = queue;
		}
	}
	if (index < 0 || (looking && waiting && now - search->marks_from < MM_LOOK_TIME))
	{
		if (looking && index < 0 && now - search->marks_from >= MM_LOOK_TIME)
		{
			search->state = P2P_MM_QUEUED;
			search->next_record = now;
		}
		return 0;
	}
	for (playlist = 0; playlist < NUMBER_OF_P2P_MM_PLAYLISTS; playlist++)
	{
		if ((search->choice.playlists & 1u << playlist) &&
			mm_playlist_has_size(1u << playlist, search->queues[picked_queue][1]) &&
			engines[playlist] == games[index].engine_type)
		{
			break;
		}
	}
	search->decision.kind = P2P_MM_DECIDE_JOIN;
	search->decision.playlist = playlist;
	search->decision.size = search->queues[picked_queue][1];
	search->decision.public_game = !games[index].big_game;
	snprintf(search->decision.invite, sizeof(search->decision.invite), "%s", games[index].invite);
	memcpy(search->picked, games[index].identifier, sizeof(search->picked));
	search->picked_set = 1;
	memset(search->joining, 0, sizeof(search->joining));
	search->joining_since = now;
	search->joining_players = games[index].player_count + party_players(search);
	search->joining_most_players = games[index].maximum_player_count;
	search->joining_ping = games[index].ping;
	snprintf(search->joining_region, sizeof(search->joining_region), "%s", games[index].dedicated ? games[index].region : "");
	search->state = P2P_MM_FOUND;
	search->decided = P2P_MM_DECIDE_JOIN;
	if (looking)
	{
		search->queue_count = 0;
		return 0;
	}
	return cancel_except(search, -1, -1, out, out_size);
}

/* a big queue's record says nothing of the host's ping: its lobby listing does */
static int big_host_near(const struct mm_peer *peer)
{
	int ping = lobby_ping_unlocked(peer->record.key_hash);

	return ping >= 0 && ping < P2P_LOBBY_PICK_PING;
}

static int better_host(const struct mm_record *record, const struct mm_record *than)
{
	return record->players > than->players || (record->players == than->players &&
		memcmp(record->key_hash, than->key_hash, P2P_KEY_HASH_SIZE) < 0);
}

/* hosts who start together all wait for the same one: a better host than best, not yet
counted or pinged, may have only just been heard, and leaving for best would split them */
static int better_host_settling(const struct mm_search *search, const struct mm_peer *best)
{
	int index;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		const struct mm_peer *peer = &search->peers[index];
		int ping;

		if (!peer->used || peer->failed || peer->record.kind != MM_KIND_HOSTING || peer->record.held ||
			!in_queue(search, peer->playlist, peer->size) || !outranks(search, &peer->record) ||
			!better_host(&peer->record, &best->record))
		{
			continue;
		}
		ping = lobby_ping_unlocked(peer->record.key_hash);
		if (ping < P2P_LOBBY_PICK_PING && (!mm_counted(peer) || (ping < 0 && peer->refreshes < MM_REFRESHES_PINGED)))
			return 1;
	}
	return 0;
}

void mm_invite_text(const unsigned char *key_hash, const unsigned char *token, char *text)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char hex[2 * sizeof(bytes) + 1];

	memcpy(bytes, key_hash, P2P_KEY_HASH_SIZE);
	memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
	p2p_hex(bytes, sizeof(bytes), hex);
	snprintf(text, P2P_LINK_SIZE, "halo://join/%s", hex);
}

static int game_has_room(int players, int red, int most_players, int teamed, int party)
{
	int blue = players - red;

	/* a host that publishes no red count reads 0, which hides its short team */
	if (teamed && party > 1 && !red && players)
		return 0;
	return players + party <= most_players && (!teamed || (red < blue ? red : blue) + party <= most_players / 2);
}

static int join(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	struct mm_peer *best = NULL;
	unsigned char token[P2P_TOKEN_SIZE];
	int index, member;

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		struct mm_peer *peer = &search->peers[index];

		if (!peer->used || peer->failed || peer->record.kind != MM_KIND_HOSTING ||
			!in_queue(search, peer->playlist, peer->size) ||
			(peer->record.held ? peer->record.players >= peer->record.most_players :
				!game_has_room(peer->record.players, peer->record.red_players, peer->record.most_players,
					peer->size != P2P_MM_FFA_6 && peer->size != P2P_MM_BIG_FFA, party_players(search))) ||
			(peer->record.held && pool_index(&peer->record, search->key_hash) < 0) ||
			(!peer->record.held && (search->party.following || (long)(now - search->join_after) < 0)) ||
			(search->state == P2P_MM_HOSTING && (!mm_counted(peer) || !outranks(search, &peer->record))) ||
			(p2p_mm_size_big(peer->size) && !big_host_near(peer)))
		{
			continue;
		}
		if (!best || better_host(&peer->record, &best->record))
			best = peer;
	}
	if (!best || (search->state == P2P_MM_HOSTING && p2p_mm_size_big(best->size) &&
		better_host_settling(search, best)))
	{
		return 0;
	}
	if (best->record.held)
	{
		member = pool_index(&best->record, search->key_hash);
		if (!mm_open_token(search->seed, best->record.key, best->record.sealed[member], token))
		{
			best->failed = MM_FAILED_GAME_UNJOINABLE;
			return 0;
		}
	}
	else
	{
		memcpy(token, best->record.token, P2P_TOKEN_SIZE);
	}
	search->picked_set = 0;
	search->decision.kind = P2P_MM_DECIDE_JOIN;
	search->decision.playlist = best->playlist;
	search->decision.size = best->size;
	search->decision.public_game = 0;
	mm_invite_text(best->record.key_hash, token, search->decision.invite);
	memcpy(search->joining, best->record.key_hash, P2P_KEY_HASH_SIZE);
	search->joining_since = now;
	search->joining_players = best->record.held ? best->record.pool_count + 1 : best->record.players + party_players(search);
	search->joining_most_players = best->record.most_players;
	search->joining_ping = -1;
	search->joining_region[0] = 0;
	search->state = P2P_MM_FOUND;
	search->decided = P2P_MM_DECIDE_JOIN;
	return cancel_except(search, best->playlist, best->size, out, out_size);
}

void mm_host_gave_up(struct mm_search *search, unsigned long now)
{
	if (search->state != P2P_MM_FOUND || search->decided != P2P_MM_DECIDE_HOST)
		return;
	search->state = P2P_MM_QUEUED;
	search->marks_from = now;
	set_queues(search);
}

void mm_joined(struct mm_search *search, int ok, unsigned long now)
{
	struct mm_peer *host;

	if (search->state != P2P_MM_FOUND)
		return;
	if (ok)
	{
		search->state = P2P_MM_OFF;
		search->in_game = 1;
		return;
	}
	host = find_peer(search, search->joining, search->queues[0][0], search->queues[0][1]);
	if (host)
		host->failed = MM_FAILED_GAME_UNJOINABLE;
	set_queues(search);
	search->state = P2P_MM_QUEUED;
	search->join_after = now + MM_JOIN_BACKOFF;
}

void mm_hosted(struct mm_search *search, const unsigned char *token, int players, int red_players, int most_players,
	int under_way, unsigned long now)
{
	if (search->state == P2P_MM_FOUND)
	{
		if (search->decided != P2P_MM_DECIDE_HOST)
			return;
		search->state = P2P_MM_HOSTING;
		search->held_until = now + (search->pool_count ? MM_HOLD : 0);
		search->next_hosting_record = now;
		memcpy(search->token, token, P2P_TOKEN_SIZE);
	}
	else if (search->state != P2P_MM_HOSTING)
	{
		return;
	}
	else if (players != search->players || red_players != search->red_players || under_way != search->under_way)
	{
		search->next_hosting_record = now;
	}
	search->players = players;
	search->red_players = red_players;
	search->most_players = most_players;
	search->under_way = under_way;
}

/* a searching record names a party once; a hosting record names each machine an invite is for */
static void pool_add(const struct mm_search *search, struct mm_record *record, const unsigned char *key, int held)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	if (record->pool_count == MM_MAXIMUM_POOL || !key_hash_of(key, hash) ||
		!memcmp(hash, search->key_hash, P2P_KEY_HASH_SIZE) ||
		(held && !mm_seal_token(search->seed, key, search->token, record->sealed[record->pool_count])))
	{
		return;
	}
	memcpy(record->pool[record->pool_count++], hash, P2P_KEY_HASH_SIZE);
}

static int hosting_record(struct mm_search *search, unsigned long now, struct mm_out *out)
{
	int held = (long)(now - search->held_until) < 0, index, other;
	struct mm_record record = base_record(search, MM_KIND_HOSTING, search->hosting_playlist, search->hosting_size,
		search->under_way ? MM_RUNNING_INTERVAL / 100 : MM_INTERVAL);

	record.under_way = search->under_way;
	record.held = held;
	record.players = search->players;
	record.red_players = search->red_players;
	record.most_players = search->most_players;
	memcpy(record.token, search->token, P2P_TOKEN_SIZE);
	for (index = 0; index < search->pool_count; index++)
	{
		struct mm_peer *member = find_peer(search, search->pool[index], search->hosting_playlist, search->hosting_size);

		if (!memcmp(search->pool[index], search->key_hash, P2P_KEY_HASH_SIZE))
		{
			for (other = 0; other < search->party.count - 1; other++)
				pool_add(search, &record, search->party.keys[other], held);
		}
		if (!member)
			continue;
		pool_add(search, &record, member->record.key, held);
		for (other = 0; other < member->record.party - 1; other++)
			pool_add(search, &record, member->record.member_keys[other], held);
	}
	search->next_hosting_record = now + (search->under_way ? MM_RUNNING_INTERVAL : MM_INTERVAL * 100);
	return emit(search, &record, out);
}

static int hand_over(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	int count = join(search, now, out, out_size);

	if (search->state == P2P_MM_FOUND && count < out_size)
	{
		search->most_players = search->players;
		count += hosting_record(search, now, &out[count]);
		count += cancel_except(search, -1, -1, &out[count], out_size - count);
	}
	return count;
}

int mm_step(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size)
{
	int count = 0, looked = 0;

	if (search->state == P2P_MM_OFF)
		return cancel_except(search, -1, -1, out, out_size);
	expire(search, now);
	if (search->state == P2P_MM_HOSTING)
	{
		if (!search->under_way)
			count = hand_over(search, now, out, out_size);
		if (search->state == P2P_MM_HOSTING && (long)(now - search->next_hosting_record) >= 0 && out_size > 0)
			count = hosting_record(search, now, out);
		return count;
	}
	if (search->state == P2P_MM_LOOKING)
	{
		count = pick_game(search, now, out, out_size);
		if (search->state == P2P_MM_LOOKING)
			return count;
		looked = 1;
	}
	if (search->state == P2P_MM_QUEUED)
	{
		if (search->host_now && search->can_host > 0)
			count = host_open(search, 1, out, out_size);
		if (!looked && search->decision.kind == P2P_MM_DECIDE_NONE && !search->party.following)
			count = pick_game(search, now, out, out_size);
		if (search->decision.kind == P2P_MM_DECIDE_NONE)
			count = join(search, now, out, out_size);
		if (search->decision.kind == P2P_MM_DECIDE_NONE)
			count = settle(search, now, out, out_size);
		if (search->decision.kind == P2P_MM_DECIDE_NONE)
			count = marks(search, now, out, out_size);
	}
	if (search->queue_count && !p2p_mm_size_big(search->choice.size) && !search->party.following &&
		(long)(now - search->next_record) >= 0 && count < out_size)
	{
		int interval = mm_interval(search->queue_count);
		struct mm_record record = base_record(search, MM_KIND_SEARCHING, search->queues[search->next_queue][0],
			search->queues[search->next_queue][1], interval);

		search->next_queue = (search->next_queue + 1) % search->queue_count;
		search->next_record = now + interval * 100 / search->queue_count;
		count += emit(search, &record, &out[count]);
	}
	return count;
}

/* ---------- the p2p thread's search: mm is guarded by p2p_lock */

enum
{
	MAXIMUM_QUEUED = 64,
	/* milliseconds of signature checks a pass */
	VERIFY_BUDGET = 2,
};

struct queued
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;
};

static struct
{
	struct mm_search search;
	struct queued queue[MAXIMUM_QUEUED];
	int queue_count;
	struct p2p_mm_decision pending;
	int subscribed[P2P_MM_MAXIMUM_TOPICS][2], subscribed_count;
	int browser_held, browser_was;
	int has_token;
	unsigned char token[P2P_TOKEN_SIZE];
	int notice;
} mm;

static unsigned long monotonic_us(void)
{
	struct timespec time_now;

	clock_gettime(CLOCK_MONOTONIC, &time_now);
	return (unsigned long)time_now.tv_sec * 1000000UL + (unsigned long)time_now.tv_nsec / 1000;
}

static unsigned long elapsed_ms(unsigned long started)
{
	return (monotonic_us() - started) / 1000;
}

static int record_stale(const struct queued *queued)
{
	int index;

	if (queued->size < MM_HEADER_SIZE || (unsigned)(queued->bytes[3] << 8 | queued->bytes[4]) != HALO_PORT_NETWORK_VERSION)
		return 1;
	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		const struct mm_peer *peer = &mm.search.peers[index];

		if (peer->used && peer->playlist == queued->bytes[12] && peer->size == queued->bytes[13] &&
			!memcmp(peer->record.key, queued->bytes + 14, P2P_KEY_SIZE))
		{
			return mm_get_long(queued->bytes + 6) <= peer->record.sequence;
		}
	}
	return 0;
}

static const char *short_hash(const unsigned char *key_hash, char *text)
{
	p2p_hex(key_hash, 4, text);
	return text;
}

static unsigned long search_seconds(void)
{
	return (unsigned long)time(NULL) - mm.search.started;
}

static void publish_out(const struct mm_out *out, int count)
{
	int index;

	for (index = 0; index < count; index++)
		p2p_signal_mm_publish(out[index].topic, out[index].bytes, out[index].size);
}

static int notice_after(int notice, int state)
{
	return state == P2P_MM_LOOKING || state == P2P_MM_QUEUED ? notice : P2P_MM_NOTICE_NONE;
}

int p2p_mm_hosts_big_game(void)
{
	return mm.search.decided == P2P_MM_DECIDE_HOST && p2p_mm_size_big(mm.search.hosting_size) &&
		(mm.search.state == P2P_MM_FOUND || mm.search.state == P2P_MM_HOSTING);
}

void p2p_mm_heard(const unsigned char *payload, int size)
{
	if (mm.search.state == P2P_MM_OFF || mm.queue_count == MAXIMUM_QUEUED || size <= 0 ||
		size > MM_MAXIMUM_RECORD_SIZE)
	{
		return;
	}
	memcpy(mm.queue[mm.queue_count].bytes, payload, (size_t)size);
	mm.queue[mm.queue_count++].size = size;
}

int p2p_mm_verify_pass(unsigned long now)
{
	unsigned long started = monotonic_us();

	for (;;)
	{
		struct queued queued;
		struct mm_record record;
		int good;

		if (!mm.queue_count)
			break;
		queued = mm.queue[0];
		memmove(mm.queue, mm.queue + 1, sizeof(*mm.queue) * (size_t)(--mm.queue_count));
		if (record_stale(&queued))
			continue;
		pthread_mutex_unlock(&p2p_lock);
		good = mm_record_read(queued.bytes, queued.size, &record);
		pthread_mutex_lock(&p2p_lock);
		if (good)
			mm_heard(&mm.search, &record, now);
		if (elapsed_ms(started) >= VERIFY_BUDGET)
			break;
	}
	return mm.queue_count;
}

static void sync_topics(void)
{
	int count = mm.search.state == P2P_MM_OFF ? 0 : mm.search.queue_count;
	char topics[P2P_MM_MAXIMUM_TOPICS][P2P_MM_TOPIC_SIZE];
	int index;

	if (count == mm.subscribed_count && !memcmp(mm.subscribed, mm.search.queues, sizeof(int) * 2 * (size_t)count))
		return;
	{
		char line[P2P_MM_MAXIMUM_TOPICS * (P2P_MM_TOPIC_SIZE + 1)];
		int length = 0;

		line[0] = 0;
		for (index = 0; index < count; index++)
		{
			mm_topic(mm.search.queues[index][0], mm.search.queues[index][1], topics[index], P2P_MM_TOPIC_SIZE);
			length += snprintf(line + length, sizeof(line) - (size_t)length, " %s", topics[index]);
		}
		if (count)
			platform_log("matchmaking: in %d queue%s, %lu s into the search:%s", count, count == 1 ? "" : "s",
				search_seconds(), line);
		else
			platform_log("matchmaking: in no queue");
	}
	memcpy(mm.subscribed, mm.search.queues, sizeof(int) * 2 * (size_t)count);
	mm.subscribed_count = count;
	p2p_signal_mm_topics(topics, count);
}

int p2p_mm_active(void)
{
	return mm.search.state != P2P_MM_OFF;
}

void p2p_mm_update(unsigned long now, int can_host, const unsigned char *token)
{
	struct mm_out out[MM_MAXIMUM_OUT];
	int count;

	mm.search.can_host = can_host;
	if (mm.search.state == P2P_MM_OFF)
	{
		mm.pending.kind = P2P_MM_DECIDE_NONE;
		/* still stepped while joined, so its queue's cancel goes out */
		publish_out(out, mm_step(&mm.search, now, out, MM_MAXIMUM_OUT));
		sync_topics();
		return;
	}
	mm.has_token = token != NULL;
	if (token)
		memcpy(mm.token, token, P2P_TOKEN_SIZE);
	p2p_mm_verify_pass(now);
	/* the pass lets go of p2p_lock: the game's thread may have restarted the search after now */
	count = mm_step(&mm.search, p2p_now(), out, MM_MAXIMUM_OUT);
	publish_out(out, count);
	mm.notice = notice_after(mm.notice, mm.search.state);
	if (mm.search.decision.kind != P2P_MM_DECIDE_NONE)
	{
		const struct p2p_mm_decision *decision = &mm.search.decision;
		char hash[9];

		if (decision->kind == P2P_MM_DECIDE_HOST)
		{
			char pool[MM_MAXIMUM_POOL * 9 + 1];
			int member, length = 0;

			pool[0] = 0;
			for (member = 0; member < mm.search.pool_count; member++)
				length += snprintf(pool + length, sizeof(pool) - (size_t)length, " %s",
					short_hash(mm.search.pool[member], hash));
			platform_log("matchmaking: decided: host %s %s, %lu s into the search (pool of %d:%s)",
				p2p_mm_playlist_names[decision->playlist], p2p_mm_size_names[decision->size], search_seconds(),
				mm.search.pool_count, pool);
		}
		else
		{
			static const unsigned char none[P2P_KEY_HASH_SIZE];
			char host[16] = "a public";

			if (memcmp(mm.search.joining, none, sizeof(none)))
				snprintf(host, sizeof(host), "%s's", short_hash(mm.search.joining, hash));
			platform_log("matchmaking: decided: join %s %s %s game, %lu s into the search", host,
				p2p_mm_playlist_names[decision->playlist], p2p_mm_size_names[decision->size], search_seconds());
		}
		mm.pending = mm.search.decision;
		mm.search.decision.kind = P2P_MM_DECIDE_NONE;
	}
	sync_topics();
}

static void cancel_search(void)
{
	struct mm_out out[MM_MAXIMUM_OUT];

	publish_out(out, mm_cancel(&mm.search, out, MM_MAXIMUM_OUT));
	mm.pending.kind = P2P_MM_DECIDE_NONE;
	mm.queue_count = 0;
}

static void hold_browser(void)
{
	if (!mm.browser_held)
	{
		mm.browser_held = 1;
		mm.browser_was = p2p_lobby_browsing();
	}
}

/* under p2p_lock; restore the result after releasing it (-1: nothing to restore) */
static int take_browser(void)
{
	int was = mm.browser_held ? mm.browser_was : -1;

	mm.browser_held = 0;
	return was;
}

static void browse_for(int size)
{
	if (p2p_mm_size_big(size))
		master_list_refresh();
	p2p_lobby_browse(1);
}

const char *p2p_mm_region(const char *setting)
{
	/* inclusive, in minutes east of UTC */
	static const struct { int from, to; const char *region; } regions[] = {
		{ -10 * 60, -7 * 60, "na-west" },
		{ -6 * 60, -4 * 60, "na-east" },
		{ -1 * 60, 4 * 60, "eu" },
		{ 8 * 60, 14 * 60, "oce" },
	};
	time_t now = time(NULL);
	/* mktime reads gmtime's fields as local standard time: daylight saving
	stays out, so a machine keeps its region all year */
	long offset = (long)difftime(now, mktime(gmtime(&now))) / 60;
	size_t index;

	if (setting[0])
		return strcasecmp(setting, "any") ? setting : "";
	for (index = 0; index < sizeof(regions) / sizeof(regions[0]); index++)
	{
		if (offset >= regions[index].from && offset <= regions[index].to)
			return regions[index].region;
	}
	return "";
}

struct mm_search *p2p_mm_local(void)
{
	return &mm.search;
}

void p2p_mm_search(const struct p2p_mm_choice *choice)
{
	pthread_mutex_lock(&p2p_lock);
	if (p2p_party_is_member())
	{
		pthread_mutex_unlock(&p2p_lock);
		return;
	}
	if (mm.search.state != P2P_MM_OFF)
		cancel_search();
	p2p_lobby_masters_forget();
	mm_start(&mm.search, p2p_signing_seed(), choice, p2p_now(), (unsigned long)time(NULL));
	p2p_party_search_begin(&mm.search);
	mm.notice = P2P_MM_NOTICE_NONE;
	{
		char hash[9];

		platform_log("matchmaking: searching as %s", short_hash(mm.search.key_hash, hash));
	}
	hold_browser();
	pthread_mutex_unlock(&p2p_lock);
	browse_for(choice->size);
}

void p2p_mm_widen_now(void)
{
	pthread_mutex_lock(&p2p_lock);
	if (mm.search.state != P2P_MM_OFF)
		mm_widen_now(&mm.search);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_mm_host_now(void)
{
	pthread_mutex_lock(&p2p_lock);
	if (mm.search.state != P2P_MM_OFF)
		mm_host_now(&mm.search);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_mm_cancel(void)
{
	int was;

	pthread_mutex_lock(&p2p_lock);
	if (mm.search.party.following)
		p2p_party_search_left();
	if (mm.search.state != P2P_MM_OFF)
	{
		cancel_search();
		platform_log("matchmaking: search cancelled");
	}
	mm.search.in_game = 0;
	mm.search.party.following = 0;
	mm.notice = P2P_MM_NOTICE_NONE;
	was = take_browser();
	pthread_mutex_unlock(&p2p_lock);
	if (was >= 0)
		p2p_lobby_browse(was);
}

void p2p_mm_game(int players, int red_players, int most_players, int under_way)
{
	int was = -1;

	pthread_mutex_lock(&p2p_lock);
	if (mm.search.state != P2P_MM_OFF && mm.search.decided == P2P_MM_DECIDE_HOST)
	{
		if (mm.has_token)
			mm_hosted(&mm.search, mm.token, players, red_players, most_players, under_way, p2p_now());
		if (players > 1 || under_way)
			was = take_browser();
	}
	pthread_mutex_unlock(&p2p_lock);
	if (was >= 0)
		p2p_lobby_browse(was);
}

void p2p_mm_joined(int ok)
{
	int was = -1;

	pthread_mutex_lock(&p2p_lock);
	if (mm.search.state == P2P_MM_FOUND && mm.search.decided == P2P_MM_DECIDE_JOIN)
	{
		if (!ok && mm.search.picked_set)
			lobby_mark_failed_unlocked(mm.search.picked);
		mm.search.picked_set = mm.search.picked_set && ok;
		mm_joined(&mm.search, ok, p2p_now());
		platform_log("matchmaking: %s", ok ? "joined the game" : "could not join the game: back in the queue");
		if (!ok)
			mm.notice = P2P_MM_NOTICE_HOST_UNREACHED;
		else
			was = take_browser();
	}
	pthread_mutex_unlock(&p2p_lock);
	if (was >= 0)
		p2p_lobby_browse(was);
}

void p2p_mm_host_gave_up(void)
{
	pthread_mutex_lock(&p2p_lock);
	if (mm.search.state == P2P_MM_FOUND && mm.search.decided == P2P_MM_DECIDE_HOST)
	{
		mm_host_gave_up(&mm.search, p2p_now());
		platform_log("matchmaking: the game to host never had its player: back in the queue");
		mm.notice = P2P_MM_NOTICE_HOST_FAILED;
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_mm_game_lost(void)
{
	int restarted = 0, size = 0;

	pthread_mutex_lock(&p2p_lock);
	/* a member that stayed behind when the leader left has no search to go back to */
	if (p2p_party_is_member() && !mm.search.party.following)
		mm.search.in_game = 0;
	if (mm.search.state != P2P_MM_OFF || mm.search.in_game)
	{
		p2p_lobby_masters_forget();
		mm_game_lost(&mm.search, p2p_now());
		size = mm.search.choice.size;
		platform_log("matchmaking: the game's host is gone: back in the queue, %lu s into the search",
			search_seconds());
		mm.notice = P2P_MM_NOTICE_HOST_LEFT;
		restarted = !mm.search.party.following;
		if (restarted)
			hold_browser();
	}
	pthread_mutex_unlock(&p2p_lock);
	if (restarted)
		browse_for(size);
}

int p2p_mm_take_left_game(void)
{
	int left, restart, size;

	pthread_mutex_lock(&p2p_lock);
	left = mm.search.left_game;
	mm.search.left_game = 0;
	restart = left && !mm.search.party.following;
	size = mm.search.choice.size;
	if (restart)
		hold_browser();
	pthread_mutex_unlock(&p2p_lock);
	if (restart)
		browse_for(size);
	return left;
}

int p2p_mm_take_decision(struct p2p_mm_decision *decision)
{
	int kind, was = -1;

	pthread_mutex_lock(&p2p_lock);
	*decision = mm.pending;
	kind = mm.pending.kind;
	mm.pending.kind = P2P_MM_DECIDE_NONE;
	/* a lone big host still needs the pings of the hosts it may merge with */
	if (kind == P2P_MM_DECIDE_HOST && !p2p_mm_size_big(mm.search.choice.size))
		was = take_browser();
	pthread_mutex_unlock(&p2p_lock);
	if (was >= 0)
		p2p_lobby_browse(was);
	return kind;
}

static int searchers_counted(int playlist, int size)
{
	int index, other, count = mm.search.party.following ? 0 : party_players(&mm.search);

	for (index = 0; index < MM_MAXIMUM_UNITS; index++)
	{
		const struct mm_peer *peer = &mm.search.peers[index];
		int seen = 0;

		if (!peer->used || peer->record.kind != MM_KIND_SEARCHING || !mm_counted(peer) ||
			(playlist >= 0 && (peer->playlist != playlist || peer->size != size)))
		{
			continue;
		}
		for (other = 0; other < index && !seen; other++)
			seen = mm.search.peers[other].used && mm.search.peers[other].record.kind == MM_KIND_SEARCHING &&
				mm_counted(&mm.search.peers[other]) &&
				(playlist < 0 || (mm.search.peers[other].playlist == playlist && mm.search.peers[other].size == size)) &&
				!memcmp(mm.search.peers[other].record.key, peer->record.key, P2P_KEY_SIZE);
		count += seen ? 0 : peer->record.party;
	}
	return count;
}

void p2p_mm_get_status(struct p2p_mm_status *status)
{
	const struct mm_search *search = &mm.search;
	int index;

	memset(status, 0, sizeof(*status));
	pthread_mutex_lock(&p2p_lock);
	status->state = search->state;
	if (search->state != P2P_MM_OFF)
	{
		status->seconds = (int)((unsigned long)time(NULL) - search->started);
		status->mark_seconds = (int)((p2p_now() - search->marks_from) / 1000);
		status->widened = search->widened;
		status->games_checked = search->games_checked;
		status->can_host = search->can_host;
		for (index = 0; index < search->queue_count; index++)
		{
			int count = searchers_counted(search->queues[index][0], search->queues[index][1]);

			if (count > status->count)
			{
				status->count = count;
				status->playlist = search->queues[index][0];
				status->size = search->queues[index][1];
				status->needed = p2p_mm_size_players(status->size);
			}
		}
		if (search->widened)
			status->all_sizes_count = searchers_counted(-1, 0);
		if (search->state == P2P_MM_FOUND || search->state == P2P_MM_HOSTING)
		{
			status->hosts = search->decided == P2P_MM_DECIDE_HOST;
			status->playlist = status->hosts ? search->hosting_playlist : search->decision.playlist;
			status->size = status->hosts ? search->hosting_size : search->decision.size;
			status->players = status->hosts ? search->players : search->joining_players;
			status->most_players = status->hosts ? search->most_players : search->joining_most_players;
			status->ping = status->hosts ? -1 : search->joining_ping;
			memcpy(status->region, search->joining_region, sizeof(status->region));
		}
		status->notice = mm.notice = notice_after(mm.notice, search->state);
	}
	if (p2p_party_mm_notice())
		status->notice = p2p_party_mm_notice();
	pthread_mutex_unlock(&p2p_lock);
}
