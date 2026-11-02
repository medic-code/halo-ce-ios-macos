/* P2P_PARTY.C: MATCHMAKING's party: its link, and the records its machines publish on its topic */

#include "p2p_matchmaking.h"
#include "posix.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);
void platform_clipboard_set(const char *text);

static const char record_label[] = "hceu-party-1";
static const char link_prefix[] = "halo://party/";

enum
{
	PARTY_INTERVAL = 3000,
	PARTY_EXPIRY = 10000,
	/* a record's kind, its key and its sequence */
	PARTY_HEADER_SIZE = 1 + P2P_KEY_SIZE + 4,
	PARTY_MEMBER_RECORD = 1,
	PARTY_LEADER_RECORD,
	PARTY_LINK_RECORD,
	PARTY_FLAG_LEAVING = 1,
	SEALED_SECRET_SIZE = P2P_PARTY_SECRET_SIZE + P2P_SEAL_OVERHEAD,
	MAXIMUM_QUEUED = 32,
	VERIFY_BUDGET = 2,
	LINK_RESEND = 10000,
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
	party->gone_count = 0;
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

/* what the leader and a member (other: the other's Ed25519 key) seal a new link's secret with */
static int pair_key(const struct mm_party *party, const unsigned char *other, unsigned char *key)
{
	unsigned char own[P2P_KEY_SIZE], public_key[P2P_KEY_SIZE], x25519[P2P_KEY_SIZE], shared[P2P_KEY_SIZE];

	if (!p2p_ed25519_to_x25519(other, x25519))
		return 0;
	p2p_ed25519_public(party->seed, public_key, own);
	p2p_x25519(shared, own, x25519);
	p2p_hmac_sha256(shared, P2P_KEY_SIZE, "party-link", 10, key);
	return 1;
}

static void remember_gone(struct mm_party *party, const struct mm_party_member *member, unsigned long sequence)
{
	struct mm_party_gone *gone = &party->gone[party->gone_count++ % P2P_PARTY_MAXIMUM];

	memcpy(gone->hash, member->hash, P2P_KEY_HASH_SIZE);
	gone->sequence = sequence;
}

static void drop_member(struct mm_party *party, struct mm_party_member *member)
{
	memmove(member, member + 1, (size_t)(party->members + --party->count - member) * sizeof(*member));
}

int mm_party_remove(struct mm_party *party, int index, const unsigned char *secret, unsigned long now, unsigned char *bytes)
{
	unsigned char plain[MM_MAXIMUM_RECORD_SIZE];
	struct mm_party_member *removed;
	int size, count_at, count = 0, other, out;

	if (party->role != MM_PARTY_LEADER || index < 1 || index >= party->count)
		return 0;
	removed = &party->members[index];
	size = record_start(party, PARTY_LINK_RECORD, plain);
	memcpy(plain + size, removed->hash, P2P_KEY_HASH_SIZE);
	size += P2P_KEY_HASH_SIZE;
	count_at = size++;
	for (other = 1; other < party->count; other++)
	{
		unsigned char key[P2P_SHA256_SIZE];

		if (other == index || !pair_key(party, party->members[other].key, key))
			continue;
		memcpy(plain + size, party->members[other].hash, P2P_KEY_HASH_SIZE);
		size += P2P_KEY_HASH_SIZE;
		size += p2p_seal(key, secret, P2P_PARTY_SECRET_SIZE, plain + size);
		count++;
	}
	plain[count_at] = (unsigned char)count;
	out = record_finish(party, plain, size, bytes);
	remember_gone(party, removed, removed->sequence);
	drop_member(party, removed);
	set_secret(party, secret);
	for (other = 1; other < party->count; other++)
		party->members[other].heard = now;
	party->sent = now - PARTY_INTERVAL;
	return out;
}

int mm_party_leave(struct mm_party *party, unsigned char *bytes)
{
	int size;

	if (party->role != MM_PARTY_MEMBER)
		return 0;
	size = write_member(party, PARTY_FLAG_LEAVING, bytes);
	clear_party(party);
	return size;
}

int mm_party_step(struct mm_party *party, unsigned long now, unsigned char *bytes)
{
	int index;

	if (party->role == MM_PARTY_NONE)
		return 0;
	if (party->role == MM_PARTY_MEMBER && elapsed(party->members[0].heard, now, PARTY_EXPIRY))
	{
		set_notice(party, P2P_PARTY_NOTICE_ENDED, party->members[0].name);
		clear_party(party);
		return 0;
	}
	if (party->role == MM_PARTY_LEADER)
	{
		for (index = party->count - 1; index > 0; index--)
		{
			if (elapsed(party->members[index].heard, now, PARTY_EXPIRY))
			{
				set_notice(party, P2P_PARTY_NOTICE_LEFT, party->members[index].name);
				drop_member(party, &party->members[index]);
			}
		}
	}
	if (!elapsed(party->sent, now, PARTY_INTERVAL))
		return 0;
	party->sent = now;
	return party->role == MM_PARTY_LEADER ? write_leader(party, bytes) : write_member(party, 0, bytes);
}

static int member_record_stale(struct mm_party *party, const unsigned char *hash, unsigned long sequence)
{
	const struct mm_party_member *member = find_member(party, hash);
	int index;

	if (member)
		return sequence <= member->sequence;
	if (party->role != MM_PARTY_LEADER)
		return 1;
	for (index = 0; index < P2P_PARTY_MAXIMUM && index < party->gone_count; index++)
		if (!memcmp(party->gone[index].hash, hash, P2P_KEY_HASH_SIZE) && sequence <= party->gone[index].sequence)
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
	memcpy(record->seal, party->seal, sizeof(record->seal));
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
	return record->kind == PARTY_LINK_RECORD && body_size > P2P_KEY_HASH_SIZE &&
		body_size == P2P_KEY_HASH_SIZE + 1 + body[P2P_KEY_HASH_SIZE] * (P2P_KEY_HASH_SIZE + SEALED_SECRET_SIZE);
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
	if (record->body[P2P_PARTY_NAME_SIZE] & PARTY_FLAG_LEAVING)
	{
		if (party->role == MM_PARTY_LEADER && member)
		{
			set_notice(party, P2P_PARTY_NOTICE_LEFT, member->name);
			remember_gone(party, member, record->sequence);
			drop_member(party, member);
		}
		return;
	}
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

static void heard_link(struct mm_party *party, const struct mm_party_record *record)
{
	const unsigned char *entry = record->body + P2P_KEY_HASH_SIZE + 1;
	int count = record->body[P2P_KEY_HASH_SIZE], index;

	if (!memcmp(record->body, party->hash, P2P_KEY_HASH_SIZE))
	{
		set_notice(party, P2P_PARTY_NOTICE_REMOVED, party->members[0].name);
		clear_party(party);
		return;
	}
	for (index = 0; index < count; index++, entry += P2P_KEY_HASH_SIZE + SEALED_SECRET_SIZE)
	{
		unsigned char key[P2P_SHA256_SIZE], secret[P2P_PARTY_SECRET_SIZE];

		if (memcmp(entry, party->hash, P2P_KEY_HASH_SIZE) || !pair_key(party, party->members[0].key, key))
			continue;
		if (p2p_open(key, entry + P2P_KEY_HASH_SIZE, SEALED_SECRET_SIZE, secret) == P2P_PARTY_SECRET_SIZE)
			set_secret(party, secret);
		return;
	}
}

void mm_party_heard(struct mm_party *party, const struct mm_party_record *record, unsigned long now)
{
	/* (the lock is released while a signature is checked: a removal in that
	time changes the seal, and a removed member's record must not add them back) */
	if (party->role == MM_PARTY_NONE || memcmp(record->seal, party->seal, sizeof(party->seal)))
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
	else
		heard_link(party, record);
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

/* ---------- this machine's party, under p2p_lock */

static struct
{
	struct mm_party party;
	struct
	{
		unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
		int size;
	} queue[MAXIMUM_QUEUED];
	int queue_count;
	/* what the brokers and Discord were last told */
	char topic[P2P_PARTY_TOPIC_SIZE];
	char offered[P2P_PARTY_LINK_SIZE];
	int offered_count;
	/* the last new link's record, sent again on the old topic for members who missed it */
	char link_topic[P2P_PARTY_TOPIC_SIZE];
	unsigned char link_record[MM_MAXIMUM_RECORD_SIZE];
	int link_size;
	unsigned long link_since, link_sent;
} local;

static void sync_topic(void)
{
	if (strcmp(local.topic, local.party.topic))
	{
		strcpy(local.topic, local.party.topic);
		p2p_signal_party_topic(local.topic);
	}
}

static void sync_offer(void)
{
	const char *link = local.party.role == MM_PARTY_LEADER ? local.party.link : "";

	if (strcmp(local.offered, link) || local.offered_count != local.party.count)
	{
		snprintf(local.offered, sizeof(local.offered), "%s", link);
		local.offered_count = local.party.count;
		p2p_discord_set_party(link, local.party.count, P2P_PARTY_MAXIMUM);
	}
}

int p2p_party_active(void)
{
	return local.party.role != MM_PARTY_NONE;
}

void p2p_party_heard(const unsigned char *payload, int size)
{
	if (local.party.role == MM_PARTY_NONE || local.queue_count == MAXIMUM_QUEUED || size <= 0 ||
		size > MM_MAXIMUM_RECORD_SIZE)
	{
		return;
	}
	memcpy(local.queue[local.queue_count].bytes, payload, (size_t)size);
	local.queue[local.queue_count++].size = size;
}

void p2p_party_update(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	unsigned long started = p2p_now();
	int size;

	while (local.queue_count && !elapsed(started, p2p_now(), VERIFY_BUDGET))
	{
		struct mm_party_record record;
		int read = mm_party_read(&local.party, local.queue[0].bytes, local.queue[0].size, &record), good;

		memmove(local.queue, local.queue + 1, sizeof(*local.queue) * (size_t)(--local.queue_count));
		if (!read)
			continue;
		pthread_mutex_unlock(&p2p_lock);
		good = mm_party_verify(&record);
		pthread_mutex_lock(&p2p_lock);
		if (good)
			mm_party_heard(&local.party, &record, p2p_now());
	}
	size = mm_party_step(&local.party, p2p_now(), bytes);
	if (size)
		p2p_signal_party_publish(bytes, size);
	if (local.link_size && !elapsed(local.link_since, p2p_now(), LINK_RESEND) &&
		elapsed(local.link_sent, p2p_now(), PARTY_INTERVAL))
	{
		local.link_sent = p2p_now();
		p2p_signal_party_publish_on(local.link_topic, local.link_record, local.link_size);
	}
	sync_topic();
	sync_offer();
}

int p2p_party_link_held(const char *text)
{
	return mm_party_link_held(text);
}

int p2p_party_link_received(const char *text)
{
	if (!mm_party_link_held(text))
		return 0;
	if (mm_party_join(&local.party, p2p_signing_seed(), text, p2p_now()))
		platform_log("matchmaking: joined a party");
	else
		platform_log("matchmaking: this machine cannot join that party");
	sync_topic();
	return 1;
}

void p2p_party_set_name(const char *name)
{
	pthread_mutex_lock(&p2p_lock);
	snprintf(local.party.name, sizeof(local.party.name), "%s", name);
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_party_invite(char *link, int size)
{
	unsigned char secret[P2P_PARTY_SECRET_SIZE];
	int made;

	if (size < P2P_PARTY_LINK_SIZE)
		return 0;
	posix_random_bytes(secret, sizeof(secret));
	pthread_mutex_lock(&p2p_lock);
	if (local.party.role == MM_PARTY_NONE)
		mm_party_make(&local.party, p2p_signing_seed(), secret, p2p_now());
	made = local.party.role == MM_PARTY_LEADER;
	if (made)
		strcpy(link, local.party.link);
	sync_topic();
	pthread_mutex_unlock(&p2p_lock);
	if (made)
		platform_clipboard_set(link);
	return made;
}

void p2p_party_remove(int member)
{
	unsigned char secret[P2P_PARTY_SECRET_SIZE], bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	posix_random_bytes(secret, sizeof(secret));
	pthread_mutex_lock(&p2p_lock);
	size = mm_party_remove(&local.party, member, secret, p2p_now(), bytes);
	if (size)
	{
		p2p_signal_party_publish(bytes, size);
		strcpy(local.link_topic, local.topic);
		memcpy(local.link_record, bytes, (size_t)size);
		local.link_size = size;
		local.link_since = local.link_sent = p2p_now();
	}
	sync_topic();
	sync_offer();
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_party_leave(void)
{
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;

	pthread_mutex_lock(&p2p_lock);
	size = mm_party_leave(&local.party, bytes);
	if (size)
		p2p_signal_party_publish(bytes, size);
	sync_topic();
	sync_offer();
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_party_get_status(struct p2p_party_status *status)
{
	pthread_mutex_lock(&p2p_lock);
	mm_party_status(&local.party, status);
	pthread_mutex_unlock(&p2p_lock);
}
