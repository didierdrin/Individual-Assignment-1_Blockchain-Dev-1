# Technical Report — Blockchain-Based Library Book Lending Tracker

**Course:** Introduction to Blockchain Development
**Assignment:** Individual Assignment 1
**Student:** Didier Nsengiyumva
**Term:** 2026 September (ALU)

---

## 1. Introduction

Traditional library lending records live in paper logs or in a single
centralised database. Both are *mutable*: a librarian with database access can
silently change a "BORROWED" row to "RETURNED", and a borrower has no
independent way to prove what the log originally said.

This project replaces that mutable log with an **append-only blockchain**.
Every borrow, return or overdue event becomes a block. Each block:

1. stores the transaction data,
2. carries an **ECDSA digital signature** produced by the librarian node's
   private key (authenticity),
3. carries the **SHA-256 hash of the previous block** (immutability of order),
4. carries its **own SHA-256 hash** over all of its fields (integrity).

Because block *N*'s hash depends on block *N−1*'s hash, editing any historical
block invalidates every block after it — tampering becomes immediately
detectable.

---

## 2. Description of the Blockchain Implementation

### 2.1 Block structure

```c
typedef struct {
    int           index;                        /* 0 = genesis           */
    time_t        timestamp;
    char          book_id[20];
    char          book_title[80];
    char          member_id[20];
    char          member_name[50];
    char          action[10];                   /* BORROWED/RETURNED/... */
    char          previous_hash[65];
    unsigned char signature[72];                /* DER-encoded ECDSA     */
    char          hash[65];
} Block;
```

The `book_title` and `member_name` fields are **snapshots** copied from the
registries at transaction time. This means the ledger remains readable and
verifiable even if a book is later renamed or a member record is edited.

### 2.2 Genesis block

The chain begins with a genesis block at `index = 0`:

* `previous_hash` = 64 zeros
* `book_id` = `"GENESIS"`, `member_id` = `"SYSTEM"`, `action` = `"GENESIS"`
* It is signed and hashed exactly like any other block, so validation is
  uniform.

If `chain.dat` does not exist, the program creates the genesis block and
persists it.

### 2.3 Canonical serialisation

To make hashing deterministic, every block is serialised into a single
pipe-delimited string:

```
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash
```

Call this the **payload**. Then:

* `signature = ECDSA_sign(SHA256(payload))`
* `hash      = SHA256(payload + "|" + hex(signature))`

Binding the signature into the hash prevents an attacker from swapping a
valid signature from a different block into this one.

### 2.4 Appending a block

```
new.index         = chain.count
new.timestamp     = time(NULL)
new.previous_hash = chain.blocks[count-1].hash
new.*             = data copied from the registries
new.signature     = sign(payload)
new.hash          = sha256(payload + signature_hex)
chain.blocks[count++] = new
```

### 2.5 Validation algorithm

For each block `i`:

1. **Linkage** — `blocks[i].previous_hash == blocks[i-1].hash`
   (for `i == 0`, it must equal 64 zeros).
2. **Signature** — recompute the payload and run `ECDSA_verify`. A mismatch
   means the data was altered or signed by a different key.
3. **Hash** — recompute the block hash and compare with the stored value.

The first failing block index is reported to the user.

---

## 3. Security Mechanisms

### 3.1 SHA-256

SHA-256 gives us:

* **Pre-image resistance** — you cannot reconstruct a block's contents from its
  hash.
* **Collision resistance** — you cannot craft two different blocks with the
  same hash.
* **Avalanche effect** — changing a single character in `action` from
  `BORROWED` to `RETURNED` produces a completely different 256-bit digest.

### 3.2 ECDSA digital signatures

The system uses **ECDSA over NIST P-256 (secp256r1)**, a standard 256-bit
elliptic curve offering roughly 128 bits of security — equivalent to RSA-3072
with far smaller keys and signatures.

* On first run, a key pair is generated and stored in `keys/node_private.pem`
  and `keys/node_public.pem`.
* Every block is signed with the private key.
* `View Records` and `Validate Chain` verify each signature against the
  public key.

**Threat covered:** an attacker who only has write access to `chain.dat`
cannot forge a valid signature, because they do not have the private key.

### 3.3 Authentication and access control

Authentication is enforced at three levels:

| Level | Mechanism |
|---|---|
| Node identity | ECDSA key pair persisted in `keys/` |
| Member identity | `member_id` must exist in `members.txt` |
| Resource validity | `book_id` must exist in `books.txt` |
| State validity | A book that is already `BORROWED`/`OVERDUE` cannot be borrowed again |

### 3.4 Data validation

* CSV lines are trimmed and split into exactly the expected number of fields;
  malformed lines are skipped with a warning.
* Blank IDs, non-existent IDs and whitespace-padded input are rejected.
* Empty or missing registry files are treated as fatal startup errors.

---

## 4. Data Persistence Approach

Two categories of state are persisted:

**1. Registries (`books.txt`, `members.txt`)** — human-editable CSV, loaded
into arrays of `Book` and `Member` structs at startup. Keeping them as plain
text means library staff can add books and members with a text editor.

**2. The chain (`chain.dat`)** — a binary snapshot written after every
successful transaction:

```
offset 0   : "LBCT1"          (5-byte magic)
offset 5   : int block_count
offset 9   : Block[block_count]  (raw struct records)
```

The magic string lets the loader detect a corrupt or foreign file. The loader
also validates the block count and checks that every record was read
completely; any failure is reported as a fatal error rather than silently
producing a partial chain.

---

## 5. Error Handling Strategy

| Scenario | Behaviour |
|---|---|
| `books.txt` missing | `ERROR: cannot open book registry` → exit code 1 |
| `books.txt` empty | `ERROR: book registry ... is empty` → exit code 1 |
| `members.txt` missing / empty | Same as above |
| Malformed CSV line | Warning printed, line skipped, loading continues |
| Unknown `book_id` / `member_id` | `ERROR: Book or Member not found`, no block created |
| Borrowing an already-borrowed book | `ERROR: ... is already on loan` |
| Returning a book that is not on loan | `ERROR: ... is not currently on loan` |
| Signing failure | Block creation aborts, user is informed |
| Disk write failure | `WARNING: chain could not be written to disk` (in-memory chain stays usable) |
| Corrupt `chain.dat` | Fatal error on startup, no partial load |
| Chain fails validation at startup | Prominent warning, program continues in read-only spirit so the user can investigate |

The guiding principle: **a lending action either succeeds completely and is
persisted, or it fails with a clear message and no block is created.** No
half-written state is possible.

---

## 6. System Design Diagram

```
                    ┌────────────────────────┐
                    │      books.txt         │
                    │  BK001,Title,Author    │
                    └───────────┬────────────┘
                                │  load at startup
                                ▼
   ┌────────────────┐   ┌──────────────────┐   ┌────────────────┐
   │  members.txt   │──▶│  REGISTRY LAYER  │◀──│  User / CLI    │
   │ ALU001,Name,C  │   │ Book[] Member[]  │   │  (stdin)       │
   └────────────────┘   └────────┬─────────┘   └───────┬────────┘
                                 │ lookup              │ command
                                 ▼                     ▼
                        ┌──────────────────────────────────────┐
                        │          TRANSACTION LAYER           │
                        │  borrow() / return() / overdue()     │
                        │  - validate IDs against registry     │
                        │  - check current loan state          │
                        └───────────────┬──────────────────────┘
                                        │ build block
                                        ▼
                        ┌──────────────────────────────────────┐
                        │           CRYPTO LAYER               │
                        │  SHA-256      ──▶ block hash         │
                        │  ECDSA P-256  ──▶ block signature    │
                        │  keys/node_private.pem               │
                        │  keys/node_public.pem                │
                        └───────────────┬──────────────────────┘
                                        │ append
                                        ▼
   ┌─────────────────────────────────────────────────────────────────┐
   │                          CHAIN                                  │
   │                                                                 │
   │  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐      │
   │  │ Block #0 │   │ Block #1 │   │ Block #2 │   │ Block #3 │ ...  │
   │  │ GENESIS  │──▶│ BORROWED │──▶│ RETURNED │──▶│ BORROWED │      │
   │  │ prev:000 │   │ prev:H0  │   │ prev:H1  │   │ prev:H2  │      │
   │  │ hash: H0 │   │ hash: H1 │   │ hash: H2 │   │ hash: H3 │      │
   │  │ sig: S0  │   │ sig: S1  │   │ sig: S2  │   │ sig: S3  │      │
   │  └──────────┘   └──────────┘   └──────────┘   └──────────┘      │
   │        │              │              │              │           │
   │        └──────────────┴──────────────┴──────────────┘           │
   │             each block's previous_hash == predecessor's hash    │
   └────────────────────────────┬────────────────────────────────────┘
                                │ persist after every transaction
                                ▼
                        ┌────────────────┐
                        │   chain.dat    │
                        │ "LBCT1" + N +  │
                        │   Block[N]     │
                        └────────────────┘

   VALIDATION PIPELINE (menu option 4):
   for each block i:
      1. previous_hash[i] == hash[i-1]        (linkage)
      2. ECDSA_verify(payload[i], signature[i]) (authenticity)
      3. SHA256(payload[i] + sig) == hash[i]   (integrity)
   → any failure  ⇒  "CHAIN INVALID at block #i"
```

### Block-linkage detail

```
                 ┌──────────────────────────────────────┐
                 │ Block N-1                            │
                 │  index, timestamp, book, member, …   │
                 │  previous_hash = H(N-2)              │
                 │  signature     = Sig(N-1)            │
                 │  hash          = H(N-1)  ────────────┼──┐
                 └──────────────────────────────────────┘  │
                                                           │ copied into
                 ┌──────────────────────────────────────┐  │
                 │ Block N                              │  │
                 │  index, timestamp, book, member, …   │  │
                 │  previous_hash = H(N-1) ◀────────────┼──┘
                 │  signature     = Sig(N)
                 │  hash          = H(N)
                 └──────────────────────────────────────┘
```

---

## 7. Screenshots of Application Execution

*(Insert screenshots captured from your demo run. Suggested set:)*

1. **Startup** — key generation / key load, registry load, chain load,
   integrity check passed.
2. **Valid borrow** — `BK001` + `ALU001` → `SUCCESS`, block hash printed.
3. **Invalid ID** — `BK999` + `ALU001` → `ERROR: Book or Member not found`.
4. **Double borrow** — `BK001` + `ALU002` → `ERROR: ... is already on loan`.
5. **Return** — `BK001` + `ALU001` → `SUCCESS`.
6. **View records** — full ledger with per-block signature validity.
7. **Validate chain** — `CHAIN VALID: all N block(s) verified.`
8. **Tamper detection** — before/after hashes and
   `CHAIN INVALID - tampering detected at block #1`.

---

## 8. Challenges Encountered and Solutions

**Challenge 1 — Deterministic hashing of a struct containing binary data.**
A raw `memcpy` of a `Block` would include uninitialised padding bytes, so the
hash would change between runs.

*Solution:* Explicitly serialise the block into a pipe-delimited ASCII string
(`block_payload`). The binary signature is hex-encoded before being folded
into the hash.

**Challenge 2 — Storing a variable-length ECDSA signature in a fixed 72-byte
array.** DER-encoded signatures are 70–72 bytes, so the array length alone does
not tell you where the signature ends.

*Solution:* Implemented `der_sig_len()`, which parses the DER header
(`0x30`, optional long-form length byte) to recover the exact length. The
remaining bytes in the array are simply ignored.

**Challenge 3 — Signature/hash circular dependency.**
If the signature covered the hash and the hash covered the signature, neither
could be computed first.

*Solution:* Two-stage sealing. The payload excludes both `signature` and
`hash`. The signature is produced over the payload; the hash is then produced
over the payload *plus* the hex signature. This keeps the signature bound into
the hash without creating a cycle.

**Challenge 4 — Malformed or missing CSV files crashing the loader.**
Early versions used `strtok`, which collapses consecutive delimiters and
silently drops empty fields.

*Solution:* Replaced with a manual `split_csv()` that splits on the first
`N-1` commas and assigns the remainder to the final field, then trims each
field. Lines with the wrong field count are skipped with a warning instead of
aborting the whole load.

**Challenge 5 — Demonstrating tampering without destroying the real ledger.**
Modifying the live chain to prove tamper detection would leave the user with a
broken chain.

*Solution:* `chain_tamper_demo()` deep-copies the chain into a temporary
`Chain`, mutates the copy, validates the copy, prints the before/after hashes,
then frees the copy. The persisted `chain.dat` is never touched.

**Challenge 6 — Making validation failures diagnosable.**
A single boolean "valid/invalid" is unhelpful when debugging.

*Solution:* `chain_validate()` returns the index of the *first* failing block
via an out-parameter, and internally checks three distinct conditions
(linkage, signature, hash) so the failure mode can be reasoned about.

---

## 9. Conclusion

The system demonstrates that a small, well-structured C program can provide
the three properties that make a blockchain useful for record-keeping:

* **Immutability** — SHA-256 chaining makes retroactive edits detectable.
* **Authenticity** — ECDSA signatures prove which node created each record.
* **Auditability** — anyone can re-run `chain_validate()` and independently
  confirm that the ledger has not been altered.

At the same time, the project shows the honest limits of a single-node
implementation: without peer-to-peer replication and a consensus rule, the
node that holds the private key is still a single point of trust. Extending
the design with per-librarian keys, network gossip and a consensus protocol
is the natural next step toward a production-grade system.