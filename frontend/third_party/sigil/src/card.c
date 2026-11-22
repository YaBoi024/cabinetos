// SPDX-License-Identifier: MPL-2.0
#include "card.h"
#include "card_dreamcast.h"
#include "card_gamecube.h"
#include "card_ps1.h"
#include "card_ps2.h"
#include "card_saturn.h"
#include "card_segacd.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/* Each reader checks its own magic or layout. The ones with a fixed magic go
 * first; the Dreamcast VMU, which is recognised by its layout alone, goes last. */
static const sigil_card_reader READERS[] = {
    sigil_ps1_card_list_io,
    sigil_ps2_card_list_io,
    sigil_gamecube_card_list_io,
    sigil_saturn_card_list_io,
    sigil_segacd_card_list_io,
    sigil_dreamcast_card_list_io,
};

sigil_card_listing *sigil_card_listing_new(int format, size_t capacity) {
    sigil_card_listing *listing = (sigil_card_listing *)calloc(1, sizeof(*listing));
    if (!listing) return NULL;
    if (capacity > 0) {
        listing->entries = (sigil_card_entry *)calloc(capacity, sizeof(sigil_card_entry));
        listing->corrupt_entries = (sigil_card_entry *)calloc(capacity, sizeof(sigil_card_entry));
        if (!listing->entries || !listing->corrupt_entries) {
            sigil_card_listing_free(listing);
            return NULL;
        }
    }
    listing->struct_version = SIGIL_CARD_LISTING_V1;
    listing->format = format;
    return listing;
}

void sigil_card_listing_corrupt(sigil_card_listing *listing, const char *name, const char *owner_id,
                                uint32_t first_block) {
    listing->corrupt_count++;
    if (!name || !listing->corrupt_entries) return;
    sigil_card_entry *e = &listing->corrupt_entries[listing->corrupt_entry_count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->owner_id, sizeof(e->owner_id), "%s", owner_id ? owner_id : "");
    e->first_block = first_block;
}

void sigil_card_sony_owner(const char *name, char out[SIGIL_CARD_OWNER_MAX]) {
    out[0] = '\0';
    if (strlen(name) < 12 || name[0] != 'B') return;
    if (name[1] != 'A' && name[1] != 'E' && name[1] != 'I') return;
    const char *code = name + 2;
    for (int i = 0; i < 4; i++) {
        if (!isupper((unsigned char)code[i])) return;
    }
    for (int i = 5; i < 10; i++) {
        if (!isdigit((unsigned char)code[i])) return;
    }
    memcpy(out, code, 10);
    out[4] = '-';
    out[10] = '\0';
}

int sigil_card_list(const sigil_io *io, sigil_card_listing **out) {
    if (!io || !io->read || !out) return SIGIL_ERR_INVALID_ARG;
    *out = NULL;
    for (size_t i = 0; i < sizeof(READERS) / sizeof(READERS[0]); i++) {
        int rc = READERS[i](io, out);
        if (rc != SIGIL_ERR_UNSUPPORTED_FORMAT) return rc;
    }
    return SIGIL_ERR_UNSUPPORTED_FORMAT;
}

void sigil_card_listing_free(sigil_card_listing *listing) {
    if (!listing) return;
    free(listing->entries);
    free(listing->corrupt_entries);
    free(listing);
}

int sigil_card_repair_note(sigil_card_repair_report *report, const char *name, const char *owner_id,
                           uint32_t first_block, int damage, bool repaired) {
    sigil_card_repair_entry *grown = (sigil_card_repair_entry *)realloc(
        report->entries, (report->entry_count + 1) * sizeof(*grown));
    if (!grown) return SIGIL_ERR_OOM;
    report->entries = grown;
    sigil_card_repair_entry *e = &grown[report->entry_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
    snprintf(e->owner_id, sizeof(e->owner_id), "%s", owner_id ? owner_id : "");
    e->first_block = first_block;
    e->damage = damage;
    e->repaired = repaired ? 1 : 0;
    return SIGIL_OK;
}

/* A format sigil can't repair: each save the listing leaves out is
 * unrecoverable, one the card doesn't name with an empty name. */
static int report_listing(const sigil_card_listing *listing, sigil_card_repair_report *report) {
    int rc = SIGIL_OK;
    for (size_t i = 0; rc == SIGIL_OK && i < listing->corrupt_entry_count; i++) {
        const sigil_card_entry *e = &listing->corrupt_entries[i];
        rc = sigil_card_repair_note(report, e->name, e->owner_id, e->first_block, SIGIL_CARD_DAMAGE_CHAIN, false);
    }
    for (size_t i = listing->corrupt_entry_count; rc == SIGIL_OK && i < listing->corrupt_count; i++) {
        rc = sigil_card_repair_note(report, NULL, NULL, 0, SIGIL_CARD_DAMAGE_CHAIN, false);
    }
    return rc;
}

/* Repairs `image` in place with `repair`; on a fix the report takes the image. */
static int repair_image(uint8_t *image, size_t size, sigil_card_repair_report *report,
                        int (*repair)(uint8_t *, size_t, sigil_card_repair_report *, bool *)) {
    bool changed = false;
    int rc = repair(image, size, report, &changed);
    if (rc == SIGIL_OK && changed) {
        report->data = image;
        report->len = size;
        return SIGIL_OK;
    }
    free(image);
    return rc;
}

static int repair_vmu(uint8_t *image, size_t size, sigil_card_repair_report *report, bool *changed) {
    (void)size;
    return sigil_dreamcast_card_repair(image, report, changed);
}

static int repair_gamecube(uint8_t *image, size_t size, sigil_card_repair_report *report, bool *changed) {
    return sigil_gamecube_card_repair(image, size, report, changed);
}

int sigil_card_repair(const sigil_io *io, sigil_card_repair_report **out) {
    if (!io || !io->read || !out) return SIGIL_ERR_INVALID_ARG;
    *out = NULL;
    sigil_card_listing *listing = NULL;
    int rc = sigil_card_list(io, &listing);
    if (rc != SIGIL_OK) return rc;
    sigil_card_repair_report *report = (sigil_card_repair_report *)calloc(1, sizeof(*report));
    if (!report) {
        sigil_card_listing_free(listing);
        return SIGIL_ERR_OOM;
    }
    report->struct_version = SIGIL_CARD_REPAIR_V1;
    report->format = listing->format;

    if (listing->format == SIGIL_CARD_FORMAT_DREAMCAST_VMU) {
        uint8_t *image = (uint8_t *)malloc(VMU_CARD_SIZE);
        rc = image ? sigil_dreamcast_card_load(io, image) : SIGIL_ERR_OOM;
        if (rc == SIGIL_OK) rc = repair_image(image, VMU_CARD_SIZE, report, repair_vmu);
        else free(image);
    } else if (listing->format == SIGIL_CARD_FORMAT_GAMECUBE_RAW) {
        uint8_t *image = NULL;
        size_t size = 0;
        rc = sigil_gamecube_card_load(io, &image, &size);
        if (rc == SIGIL_OK) rc = repair_image(image, size, report, repair_gamecube);
    } else {
        rc = report_listing(listing, report);
    }
    sigil_card_listing_free(listing);
    if (rc != SIGIL_OK) {
        sigil_card_repair_report_free(report);
        return rc;
    }
    *out = report;
    return SIGIL_OK;
}

void sigil_card_repair_report_free(sigil_card_repair_report *report) {
    if (!report) return;
    free(report->data);
    free(report->entries);
    free(report);
}
