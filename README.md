# Library Blockchain Tracker (LBT)

A blockchain-based library book lending tracker written in **C** with
**OpenSSL** (SHA-256 + ECDSA P-256). Every borrow/return event is stored in
an append-only, cryptographically-linked, digitally-signed block, so lending
records cannot be quietly altered.

---

## 1. Features

| Requirement | Implementation |
|---|---|
| Blockchain data structure | `Block` / `Chain` in `include/blockchain.h` |
| Book & member registries loaded from file | `src/registry.c` (`books.txt`, `members.txt`) |
| SHA-256 block linking | `sha256_hex()` in `src/crypto_utils.c`; `previous_hash` field |
| Digital signatures | ECDSA over NIST P-256 (`ECDSA_sign` / `ECDSA_verify`) |
| Chain validation / tamper detection | `chain_validate()` + menu option 5 |
| CLI interface | `src/main.c` |
| Data persistence | Binary snapshot `chain.dat` (`chain_save` / `chain_load`) |
| Error handling | Registry, ID, state and I/O checks throughout |

---

## 2. Dependencies

* **gcc** (or clang) with C11 support
* **OpenSSL development headers** (`libssl-dev` / `openssl-devel`)

```bash
# Debian / Ubuntu / Kali
sudo apt update && sudo apt install build-essential libssl-dev

# Fedora / RHEL
sudo dnf install gcc openssl-devel

# macOS (Homebrew)
brew install openssl
# then: export CFLAGS="-I$(brew --prefix openssl)/include"
#       export LDFLAGS="-L$(brew --prefix openssl)/lib"
```

---

## 3. Build & Run

```bash
git clone <your-repo-url>
cd library-blockchain

make          # builds ./lbt
./lbt         # or: make run
```

To start from a clean slate (fresh genesis block + fresh key pair):

```bash
make reset
./lbt
```

---

## 4. Files

```
library-blockchain/
├── Makefile
├── README.md
├── books.txt              # book registry  (book_id,title,author)
├── members.txt            # member registry (member_id,full_name,course_code)
├── include/
│   ├── blockchain.h
│   ├── crypto_utils.h
│   └── registry.h
├── src/
│   ├── main.c             # CLI
│   ├── blockchain.c       # blocks, chain, validation, persistence
│   ├── crypto_utils.c     # SHA-256 + ECDSA helpers
│   └── registry.c         # books.txt / members.txt loading
├── keys/                  # generated on first run (git-ignore this)
│   ├── node_private.pem
│   └── node_public.pem
└── chain.dat              # generated on first run (git-ignore this)
```

Add to `.gitignore`:

```
lbt
*.o
keys/
chain.dat
```

---

## 5. Usage Walkthrough

```
Choice > 1
Book ID   : BK001
Member ID : ALU001
SUCCESS: John Doe (ALU001) borrowed "Things Fall Apart".
         Block #1 created.
         Hash      : 3f9a... 
         Signature : VALID (ECDSA P-256)
```

Invalid ID handling:

```
Choice > 1
Book ID   : BK999
Member ID : ALU001
ERROR: Book or Member not found
```

Double-borrow protection:

```
Choice > 1
Book ID   : BK001
Member ID : ALU002
ERROR: "Things Fall Apart" (BK001) is already on loan.
```

Return:

```
Choice > 2
Book ID   : BK001
Member ID : ALU001
SUCCESS: "Things Fall Apart" returned by John Doe.
```

Validate:

```
Choice > 4
CHAIN VALID: all 5 block(s) verified.
```

Tamper demo:

```
Choice > 5
BEFORE  block #1 : action="BORROWED"  book="Things Fall Apart"
AFTER   block #1 : action="RETURNED"  book="Things Fall Apart"
RESULT: CHAIN INVALID - tampering detected at block #1
```

---

## 6. Design Notes

### Block hash payload

The signing payload is the pipe-delimited concatenation of every block field
**except** `signature` and `hash`:

```
index|timestamp|book_id|book_title|member_id|member_name|action|previous_hash
```

The block `hash` is then `SHA256(payload + "|" + hex(signature))`, which
binds the signature into the hash as well.

### Key management

On first run the program generates a P-256 key pair and writes it to
`keys/node_private.pem` and `keys/node_public.pem`. The key pair models a
**librarian node's identity**. In a multi-node deployment each librarian would
hold their own key pair and the chain would store the corresponding public key
per block.

### Persistence

`chain.dat` is a simple binary snapshot:

```
"LBCT1"          5 bytes  magic
<int>            block count
<Block>[count]   raw block records
```

It is rewritten after every successful transaction.

### Error handling strategy

| Layer | Strategy |
|---|---|
| Startup | Missing/empty `books.txt` / `members.txt` → fatal error, exit 1 |
| Corruption | Bad magic / truncated `chain.dat` → fatal error, exit 1 |
| Input | Unknown IDs, blank input, double borrow, return-without-borrow → user-facing `ERROR:` message, no block created |
| Crypto | Signing failure aborts block creation; verification failure marks the chain invalid |
| I/O | Save failure emits a `WARNING` but keeps the in-memory chain usable |

---

## 7. Limitations & Possible Extensions

* The chain is a single-node ledger. A real deployment needs peer-to-peer
  gossip and a consensus rule (e.g. PoW or PBFT).
* `chain.dat` is a binary snapshot rather than an append-only WAL.
* No proof-of-work / mining difficulty is implemented; integrity relies on
  hashing + signatures rather than consensus.
* Possible extensions: per-librarian keys, Merkle trees for O(log n) proofs,
  a REST API, and overdue-date computation from the block timestamps.