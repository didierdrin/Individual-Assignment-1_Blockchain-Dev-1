/* ============================================================
 *  File: src/main.c
 *  Library Blockchain Tracker - CLI
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_utils.h"
#include "registry.h"
#include "blockchain.h"

#define BOOKS_FILE   "books.txt"
#define MEMBERS_FILE "members.txt"
#define CHAIN_FILE   "chain.dat"
#define KEY_DIR      "keys"

static Chain g_chain;

/* ------------------------------------------------------------ */
static int read_line(const char *prompt, char *buf, size_t n)
{
    size_t len;

    printf("%s", prompt);
    fflush(stdout);

    if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; return 0; }

    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 1;
}

/* ------------------------------------------------------------ */
static int init_chain(void)
{
    int r;

    chain_init_struct(&g_chain);
    r = chain_load(&g_chain, CHAIN_FILE);

    if (r == 1) {
        if (chain_genesis(&g_chain) != 0) {
            fprintf(stderr, "FATAL: could not create genesis block\n");
            return -1;
        }
        if (chain_save(&g_chain, CHAIN_FILE) != 0) {
            fprintf(stderr, "FATAL: could not persist new chain\n");
            return -1;
        }
        printf("[chain]    created new chain with genesis block -> %s\n",
               CHAIN_FILE);
    } else if (r != 0) {
        fprintf(stderr, "FATAL: chain file '%s' could not be read\n", CHAIN_FILE);
        return -1;
    } else {
        printf("[chain]    loaded %d block(s) from %s\n",
               g_chain.count, CHAIN_FILE);
    }
    return 0;
}

static void persist_or_warn(void)
{
    if (chain_save(&g_chain, CHAIN_FILE) != 0)
        printf("WARNING: chain could not be written to disk.\n");
}

/* ============================================================
 *  Actions
 * ============================================================ */
static void do_borrow(void)
{
    char bid[64], mid[64];
    const Book   *b;
    const Member *m;

    printf("\n--- BORROW BOOK ---\n");
    if (!read_line("Book ID   : ", bid, sizeof bid)) return;
    if (!read_line("Member ID : ", mid, sizeof mid)) return;

    b = registry_find_book(bid);
    m = registry_find_member(mid);

    if (!b || !m) {
        printf("ERROR: Book or Member not found\n");
        return;
    }
    if (chain_book_on_loan(&g_chain, b->book_id)) {
        printf("ERROR: \"%s\" (%s) is already on loan.\n", b->title, b->book_id);
        return;
    }

    if (chain_add(&g_chain, b->book_id, b->title,
                            m->member_id, m->full_name, "BORROWED") != 0) {
        printf("ERROR: could not record the lending event.\n");
        return;
    }
    persist_or_warn();

    {
        const Block *nb = &g_chain.blocks[g_chain.count - 1];
        printf("\nSUCCESS: %s (%s) borrowed \"%s\".\n",
               m->full_name, m->member_id, b->title);
        printf("         Block #%d created.\n", nb->index);
        printf("         Hash      : %.32s...\n", nb->hash);
        printf("         Signature : VALID (ECDSA P-256)\n");
    }
}

static void do_return(void)
{
    char bid[64], mid[64];
    const Book   *b;
    const Member *m;
    const Block  *last;

    printf("\n--- RETURN BOOK ---\n");
    if (!read_line("Book ID   : ", bid, sizeof bid)) return;
    if (!read_line("Member ID : ", mid, sizeof mid)) return;

    b = registry_find_book(bid);
    m = registry_find_member(mid);

    if (!b || !m) {
        printf("ERROR: Book or Member not found\n");
        return;
    }

    last = chain_last_for_book(&g_chain, b->book_id);
    if (!last || (strcmp(last->action, "BORROWED") != 0 &&
                  strcmp(last->action, "OVERDUE")  != 0)) {
        printf("ERROR: \"%s\" (%s) is not currently on loan "
               "(never borrowed, or already returned).\n", b->title, b->book_id);
        return;
    }

    if (strcmp(last->member_id, m->member_id) != 0)
        printf("NOTE: this book was borrowed by %s (%s).\n",
               last->member_name, last->member_id);

    if (chain_add(&g_chain, b->book_id, b->title,
                            m->member_id, m->full_name, "RETURNED") != 0) {
        printf("ERROR: could not record the return event.\n");
        return;
    }
    persist_or_warn();

    {
        const Block *nb = &g_chain.blocks[g_chain.count - 1];
        printf("\nSUCCESS: \"%s\" returned by %s.\n", b->title, m->full_name);
        printf("         Block #%d created.\n", nb->index);
        printf("         Hash      : %.32s...\n", nb->hash);
    }
}

static void do_overdue(void)
{
    char bid[64], mid[64];
    const Book   *b;
    const Member *m;
    const Block  *last;

    printf("\n--- MARK BOOK OVERDUE ---\n");
    if (!read_line("Book ID   : ", bid, sizeof bid)) return;
    if (!read_line("Member ID : ", mid, sizeof mid)) return;

    b = registry_find_book(bid);
    m = registry_find_member(mid);

    if (!b || !m) { printf("ERROR: Book or Member not found\n"); return; }

    last = chain_last_for_book(&g_chain, b->book_id);
    if (!last || strcmp(last->action, "BORROWED") != 0) {
        printf("ERROR: \"%s\" is not currently on loan.\n", b->title);
        return;
    }
    if (chain_add(&g_chain, b->book_id, b->title,
                            m->member_id, m->full_name, "OVERDUE") != 0) {
        printf("ERROR: could not record the overdue event.\n");
        return;
    }
    persist_or_warn();
    printf("\nSUCCESS: \"%s\" flagged OVERDUE.\n", b->title);
}

static void do_validate(void)
{
    int bad = -1;

    printf("\n--- CHAIN VALIDATION ---\n");
    if (chain_validate(&g_chain, &bad)) {
        printf("CHAIN VALID: all %d block(s) verified.\n", g_chain.count);
        printf("  - every block hash matches its contents\n");
        printf("  - every previous_hash links to its predecessor\n");
        printf("  - every ECDSA signature verifies\n");
    } else {
        printf("CHAIN INVALID: tampering detected at block #%d\n", bad);
        printf("  -> the ledger cannot be trusted.\n");
    }
}

static void do_audit_book(void)
{
    char bid[64];
    const Book *b;

    if (!read_line("\nBook ID: ", bid, sizeof bid)) return;

    b = registry_find_book(bid);
    if (!b) { printf("ERROR: Book or Member not found\n"); return; }

    printf("\nHistory for \"%s\" (%s):\n", b->title, b->book_id);
    int found = 0;
    for (int i = 0; i < g_chain.count; i++) {
        const Block *blk = &g_chain.blocks[i];
        char tbuf[64];
        struct tm *tm_info;

        if (strcmp(blk->book_id, b->book_id) != 0) continue;

        tm_info = localtime(&blk->timestamp);
        strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", tm_info);

        printf("  #%-3d %-9s %-12s %s\n",
               blk->index, blk->action, blk->member_id, tbuf);
        found = 1;
    }
    if (!found) printf("  (no lending events recorded)\n");

    printf("  Current status: %s\n",
           chain_book_on_loan(&g_chain, b->book_id) ? "ON LOAN" : "AVAILABLE");
}

/* ============================================================
 *  Menu
 * ============================================================ */
static void print_menu(void)
{
    printf("\n==================================================\n");
    printf("  LIBRARY BLOCKCHAIN TRACKER\n");
    printf("==================================================\n");
    printf("  1. Borrow a book\n");
    printf("  2. Return a book\n");
    printf("  3. View lending records (full ledger)\n");
    printf("  4. Validate chain integrity\n");
    printf("  5. Tamper-detection demonstration\n");
    printf("  6. Show book registry\n");
    printf("  7. Show member registry\n");
    printf("  8. Audit a single book's history\n");
    printf("  9. Mark a book overdue\n");
    printf("  0. Exit\n");
    printf("--------------------------------------------------\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\n##################################################\n");
    printf("#  Blockchain-Based Library Book Lending Tracker #\n");
    printf("#  SHA-256 chain  +  ECDSA (P-256) signatures    #\n");
    printf("##################################################\n\n");

    /* ---- 1. cryptography ---------------------------------- */
    if (crypto_init(KEY_DIR) != 0) {
        fprintf(stderr, "FATAL: could not initialise cryptography.\n");
        return 1;
    }

    /* ---- 2. registries ------------------------------------ */
    if (registry_load_books(BOOKS_FILE) != 0) {
        fprintf(stderr, "FATAL: book registry unavailable.\n");
        crypto_cleanup();
        return 1;
    }
    if (registry_load_members(MEMBERS_FILE) != 0) {
        fprintf(stderr, "FATAL: member registry unavailable.\n");
        crypto_cleanup();
        return 1;
    }

    /* ---- 3. chain ----------------------------------------- */
    if (init_chain() != 0) {
        crypto_cleanup();
        return 1;
    }

    /* ---- 4. startup integrity check ----------------------- */
    {
        int bad = -1;
        if (!chain_validate(&g_chain, &bad)) {
            printf("\n*** WARNING: stored chain failed validation at block #%d ***\n",
                   bad);
            printf("*** Run option 4/5 to investigate, or delete %s to reset. ***\n",
                   CHAIN_FILE);
        } else {
            printf("[chain]    integrity check passed.\n");
        }
    }

    /* ---- 5. interaction loop ------------------------------ */
    for (;;) {
        char line[32];
        int  choice;

        print_menu();
        if (!read_line("Choice > ", line, sizeof line)) {
            printf("\nEnd of input - exiting.\n");
            break;
        }
        if (line[0] == '\0') continue;

        choice = atoi(line);

        switch (choice) {
            case 1: do_borrow();       break;
            case 2: do_return();       break;
            case 3: chain_print(&g_chain); break;
            case 4: do_validate();     break;
            case 5: chain_tamper_demo(&g_chain); break;
            case 6: registry_print_books();   break;
            case 7: registry_print_members(); break;
            case 8: do_audit_book();   break;
            case 9: do_overdue();      break;
            case 0:
                printf("\nSaving chain and shutting down...\n");
                persist_or_warn();
                chain_free(&g_chain);
                crypto_cleanup();
                printf("Goodbye.\n");
                return 0;
            default:
                printf("Invalid option. Please choose 0-9.\n");
        }
    }

    persist_or_warn();
    chain_free(&g_chain);
    crypto_cleanup();
    return 0;
}