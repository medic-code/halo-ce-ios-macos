/* P2P_PARTY.C: MATCHMAKING's party: its link, and the records its machines publish on its topic */

#include "p2p_matchmaking.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char record_label[] = "hceu-party-1";
static const char link_prefix[] = "halo://party/";

enum
{
	PARTY_INTERVAL = 3000,
	/* a record's kind, its key and its sequence */
	PARTY_HEADER_SIZE = 1 + P2P_KEY_SIZE + 4,
	PARTY_MEMBER_RECORD = 1,
	PARTY_LEADER_RECORD,
};

static int elapsed(unsigned long since, unsigned long now, unsigned long time)
{
	/* (unsigned, as the clock wraps) */
	return (unsigned int)(now - since) >= (unsigned int)time;
}

static void put_long(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

static unsigned long get_long(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

static void hash_of(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

static struct mm_party_member *find_member(struct mm_party *party, const unsigned char *hash)
{
	int index;

	for (index = 0; index < party->count; index++)
		if (!memcmp(party->members[index].hash, hash, P2P_KEY_HASH_SIZE))
			return &party->members[index];
	return NULL;
}

static void set_secret(struct mm_party *party, const unsigned char *secret)
{
	char secret_text[2 * P2P_PARTY_SECRET_SIZE + 1], leader_text[2 * P2P_KEY_SIZE + 1];

	/* (not the topic's hash, which every watcher of the topic reads) */
	p2p_hmac_sha256(secret, P2P_PARTY_SECRET_SIZE, "seal", 4, party->seal);
	mm_party_topic(secret, party->topic, sizeof(party->topic));
	p2p_hex(secret, P2P_PARTY_SECRET_SIZE, secret_text);
	p2p_hex(party->members[0].key, P2P_KEY_SIZE, leader_text);
	snprintf(party->link, sizeof(party->link), "%s%s%s", link_prefix, secret_text, leader_text);
}

static void clear_party(struct mm_party *party)
{
	party->role = MM_PARTY_NONE;
	party->count = 0;
	party->topic[0] = 0;
	party->link[0] = 0;
}

static void set_notice(struct mm_party *party, int notice, const char *name)
{
	party->notice = notice;
	snprintf(party->notice_name, sizeof(party->notice_name), "%s", name);
}

static void add_member(struct mm_party *party, const unsigned char *key)
{
	struct mm_party_member *member = &party->members[party->count++];

	memset(member, 0, sizeof(*member));
	memcpy(member->key, key, P2P_KEY_SIZE);
	hash_of(key, member->hash);
}

/* (the sequence stays, so a machine's records rise from one party to the next) */
static void begin_party(struct mm_party *party, const unsigned char *seed, int role, const unsigned char *leader,
	unsigned long now)
{
	memcpy(party->seed, seed, P2P_SEED_SIZE);
	p2p_ed25519_public(seed, party->key, NULL);
	hash_of(party->key, party->hash);
	party->role = role;
	party->count = 0;
	add_member(party, leader);
	party->members[0].heard = now;
	party->sent = now - PARTY_INTERVAL;
	set_notice(party, P2P_PARTY_NOTICE_NONE, "");
}

void mm_party_make(struct mm_party *party, const unsigned char *seed, const unsigned char *secret, unsigned long now)
{
	unsigned char key[P2P_KEY_SIZE];

	p2p_ed25519_public(seed, key, NULL);
	begin_party(party, seed, MM_PARTY_LEADER, key, now);
	set_secret(party, secret);
}

/* secret, then the leader's key */
static int link_read(const char *text, unsigned char *bytes)
{
	const size_t length = sizeof(link_prefix) - 1;
	int index;

	for (; *text; text++)
	{
		size_t matched;

		for (matched = 0; matched < length && text[matched] && tolower((unsigned char)text[matched]) ==
			link_prefix[matched]; matched++)
			;
		if (matched == length)
			break;
	}
	if (!*text)
		return 0;
	text += length;
	for (index = 0; index < P2P_PARTY_SECRET_SIZE + P2P_KEY_SIZE; index++)
	{
		unsigned value;

		if (!isxdigit((unsigned char)text[0]) || !isxdigit((unsigned char)text[1]) || sscanf(text, "%2x", &value) != 1)
			return 0;
		bytes[index] = (unsigned char)value;
		text += 2;
	}
	return !isxdigit((unsigned char)*text);
}

int mm_party_link_held(const char *text)
{
	unsigned char bytes[P2P_PARTY_SECRET_SIZE + P2P_KEY_SIZE];

	return link_read(text, bytes);
}

int mm_party_join(struct mm_party *party, const unsigned char *seed, const char *text, unsigned long now)
{
	unsigned char bytes[P2P_PARTY_SECRET_SIZE + P2P_KEY_SIZE], key[P2P_KEY_SIZE], x25519[P2P_KEY_SIZE];
	const unsigned char *leader = bytes + P2P_PARTY_SECRET_SIZE;

	p2p_ed25519_public(seed, key, NULL);
	if (!link_read(text, bytes) || !memcmp(leader, key, P2P_KEY_SIZE) || !p2p_ed25519_to_x25519(leader, x25519) ||
		party->role == MM_PARTY_MEMBER || party->count > 1)
	{
		return 0;
	}
	begin_party(party, seed, MM_PARTY_MEMBER, leader, now);
	add_member(party, key);
	set_secret(party, bytes);
	return 1;
}

static int record_start(struct mm_party *party, int kind, unsigned char *plain)
{
	plain[0] = (unsigned char)kind;
	memcpy(plain + 1, party->key, P2P_KEY_SIZE);
	put_long(plain + 1 + P2P_KEY_SIZE, ++party->sequence);
	return PARTY_HEADER_SIZE;
}

static int put_name(const char *name, unsigned char *bytes)
{
	memset(bytes, 0, P2P_PARTY_NAME_SIZE);
	memcpy(bytes, name, strlen(name));
	return P2P_PARTY_NAME_SIZE;
}

static void signed_message(const unsigned char *plain, int size, unsigned char *message)
{
	memcpy(message, record_label, sizeof(record_label) - 1);
	memcpy(message + sizeof(record_label) - 1, plain, (size_t)size);
}

static int record_finish(const struct mm_party *party, unsigned char *plain, int size, unsigned char *bytes)
{
	unsigned char message[sizeof(record_label) + MM_MAXIMUM_RECORD_SIZE];

	signed_message(plain, size, message);
	p2p_ed25519_sign(party->seed, party->key, message, (int)sizeof(record_label) - 1 + size, plain + size);
	return p2p_seal(party->seal, plain, size + P2P_SIGNATURE_SIZE, bytes);
}

static int write_member(struct mm_party *party, int flags, unsigned char *bytes)
{
	unsigned char plain[MM_MAXIMUM_RECORD_SIZE];
	int size = record_start(party, PARTY_MEMBER_RECORD, plain);

	size += put_name(party->name, plain + size);
	plain[size++] = (unsigned char)flags;
	return record_finish(party, plain, size, bytes);
}

static int write_leader(struct mm_party *party, unsigned char *bytes)
{
	unsigned char plain[MM_MAXIMUM_RECORD_SIZE];
	int size = record_start(party, PARTY_LEADER_RECORD, plain);
	int index;

	size += put_name(party->name, plain + size);
	plain[size++] = (unsigned char)(party->count - 1);
	for (index = 1; index < party->count; index++)
	{
		memcpy(plain + size, party->members[index].hash, P2P_KEY_HASH_SIZE);
		size += P2P_KEY_HASH_SIZE;
	}
	return record_finish(party, plain, size, bytes);
}

int mm_party_step(struct mm_party *party, unsigned long now, unsigned char *bytes)
{
	if (party->role == MM_PARTY_NONE)
		return 0;
	if (!elapsed(party->sent, now, PARTY_INTERVAL))
		return 0;
	party->sent = now;
	return party->role == MM_PARTY_LEADER ? write_leader(party, bytes) : write_member(party, 0, bytes);
}

static int member_record_stale(struct mm_party *party, const unsigned char *hash, unsigned long sequence)
{
	const struct mm_party_member *member = find_member(party, hash);

	if (member)
		return sequence <= member->sequence;
	if (party->role != MM_PARTY_LEADER)
		return 1;
	return 0;
}

int mm_party_read(struct mm_party *party, const unsigned char *payload, int size, struct mm_party_record *record)
{
	const unsigned char *body = record->plain + PARTY_HEADER_SIZE;
	unsigned char hash[P2P_KEY_HASH_SIZE];
	int body_size;

	if (party->role == MM_PARTY_NONE || size > MM_MAXIMUM_RECORD_SIZE)
		return 0;
	size = p2p_open(party->seal, payload, size, record->plain);
	if (size < PARTY_HEADER_SIZE + P2P_SIGNATURE_SIZE)
		return 0;
	record->size = size - P2P_SIGNATURE_SIZE;
	body_size = record->size - PARTY_HEADER_SIZE;
	record->kind = record->plain[0];
	record->key = record->plain + 1;
	record->sequence = get_long(record->plain + 1 + P2P_KEY_SIZE);
	record->body = body;
	if (!memcmp(record->key, party->key, P2P_KEY_SIZE))
		return 0;
	if (record->kind == PARTY_MEMBER_RECORD)
	{
		hash_of(record->key, hash);
		return body_size == P2P_PARTY_NAME_SIZE + 1 && !member_record_stale(party, hash, record->sequence);
	}
	if (party->role != MM_PARTY_MEMBER || memcmp(record->key, party->members[0].key, P2P_KEY_SIZE) ||
		record->sequence <= party->members[0].sequence)
	{
		return 0;
	}
	if (record->kind == PARTY_LEADER_RECORD)
		return body_size > P2P_PARTY_NAME_SIZE && body[P2P_PARTY_NAME_SIZE] < P2P_PARTY_MAXIMUM &&
			body_size == P2P_PARTY_NAME_SIZE + 1 + body[P2P_PARTY_NAME_SIZE] * P2P_KEY_HASH_SIZE;
	return 0;
}

int mm_party_verify(const struct mm_party_record *record)
{
	unsigned char message[sizeof(record_label) + MM_MAXIMUM_RECORD_SIZE];

	signed_message(record->plain, record->size, message);
	return p2p_ed25519_verify(record->key, message, (int)sizeof(record_label) - 1 + record->size,
		record->plain + record->size);
}

static void get_name(const unsigned char *bytes, char *name)
{
	memcpy(name, bytes, P2P_PARTY_NAME_SIZE);
	name[P2P_PARTY_NAME_SIZE] = 0;
}

static void heard_member(struct mm_party *party, const struct mm_party_record *record, unsigned long now)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];
	struct mm_party_member *member;

	hash_of(record->key, hash);
	member = find_member(party, hash);
	if (!member)
	{
		if (party->role != MM_PARTY_LEADER || party->count == P2P_PARTY_MAXIMUM)
			return;
		add_member(party, record->key);
		member = &party->members[party->count - 1];
	}
	get_name(record->body, member->name);
	member->sequence = record->sequence;
	member->heard = now;
}

static void heard_leader(struct mm_party *party, const struct mm_party_record *record)
{
	struct mm_party_member old[P2P_PARTY_MAXIMUM];
	const unsigned char *hashes = record->body + P2P_PARTY_NAME_SIZE + 1;
	int old_count = party->count, count = record->body[P2P_PARTY_NAME_SIZE], index, listed;

	memcpy(old, party->members, sizeof(old));
	party->count = 1;
	for (index = 0; index < count; index++)
	{
		const unsigned char *hash = hashes + index * P2P_KEY_HASH_SIZE;
		struct mm_party_member *member = &party->members[party->count];
		int was;

		memset(member, 0, sizeof(*member));
		memcpy(member->hash, hash, P2P_KEY_HASH_SIZE);
		for (was = 1; was < old_count; was++)
			if (!memcmp(old[was].hash, hash, P2P_KEY_HASH_SIZE))
				*member = old[was];
		party->count++;
	}
	for (index = 1; index < old_count; index++)
	{
		for (listed = 1; listed < party->count; listed++)
			if (!memcmp(party->members[listed].hash, old[index].hash, P2P_KEY_HASH_SIZE))
				break;
		if (listed == party->count && memcmp(old[index].hash, party->hash, P2P_KEY_HASH_SIZE))
			set_notice(party, P2P_PARTY_NOTICE_LEFT, old[index].name);
	}
}

void mm_party_heard(struct mm_party *party, const struct mm_party_record *record, unsigned long now)
{
	if (party->role == MM_PARTY_NONE)
		return;
	if (record->kind == PARTY_MEMBER_RECORD)
	{
		heard_member(party, record, now);
		return;
	}
	party->members[0].sequence = record->sequence;
	party->members[0].heard = now;
	if (record->kind == PARTY_LEADER_RECORD)
	{
		get_name(record->body, party->members[0].name);
		heard_leader(party, record);
	}
}

void mm_party_status(const struct mm_party *party, struct p2p_party_status *status)
{
	int index;

	memset(status, 0, sizeof(*status));
	status->count = party->count;
	for (index = 0; index < party->count; index++)
	{
		const struct mm_party_member *member = &party->members[index];
		struct p2p_party_member *shown = &status->members[index];

		shown->you = !memcmp(member->hash, party->hash, P2P_KEY_HASH_SIZE);
		shown->leader = !index;
		memcpy(shown->name, shown->you ? party->name : member->name, sizeof(shown->name));
	}
	status->notice = party->notice;
	memcpy(status->notice_name, party->notice_name, sizeof(status->notice_name));
}
