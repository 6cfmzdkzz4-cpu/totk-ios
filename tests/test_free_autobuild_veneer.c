/* The veneer executes either the original instruction or the supplied write. */
#include "free_autobuild.h"
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); fails++; }
}

static uint64_t branch_target(uint32_t word, uint64_t from) {
    int32_t imm = (int32_t)(word & 0x03ffffffu);
    if (imm & 0x02000000) imm -= 0x04000000;
    return from + (uint64_t)((int64_t)imm << 2);
}

int main(void) {
    uint32_t stub[FA_STUB_WORDS];
    uint64_t site = FA_SITE_BL, at = 0x1800000, data = 0x3000000;
    uint32_t bl = 0x94000004u;
    expect(fa_kind(bl) == 0, "BL class");
    expect(fa_bl_target(bl, site) == site + 16, "BL target");
    expect(fa_stub(stub, bl, site, at, data) == 1, "BL veneer encodes");
    expect(stub[0] == 0xf81f0ff0u && stub[4] == 0xf84107f0u && stub[7] == 0xf84107f0u, "x16 is restored");
    expect(stub[5] == FA_NOP, "BL on path is NOP");
    expect(branch_target(stub[6], at + 24) == site + 4, "on path returns after the site");
    expect(branch_target(stub[10], at + 40) == site + 16, "off path calls the original target");
    expect(stub[15] == bl, "original BL is retained and not executed");
    uint32_t ldr = 0xb9400288u, add = 0x8b010000u;
    expect(fa_kind(ldr) == 1 && fa_kind(add) == 2, "LDR and ADD classes");
    expect(fa_stub(stub, ldr, FA_SITE_LDR, at, data) == 1, "LDR veneer encodes");
    expect(stub[5] == FA_MOV_W8_WZR && stub[8] == ldr, "LDR on path is MOV and off path is the original load");
    expect(fa_stub(stub, add, FA_SITE_ADD, at, data) == 1, "ADD veneer encodes");
    expect(stub[5] == FA_NOP && stub[8] == add, "ADD on path is NOP and off path is the original add");
    expect(fa_stub(stub, FA_NOP, site, at, data) == 0, "an unexpected instruction is refused");
    expect(strcmp(FA_ORIGINAL_SHA256, "8f89130628c3e9996afbecb2b6a646b508cef23375d4bad94b0b24dd1003704a") == 0,
           "pinned original hash");
    uint8_t parsed[32];
    expect(fa_parse_sha256(FA_ORIGINAL_SHA256, parsed) == 1, "parse pin");
    expect(parsed[0] == 0x8f && parsed[31] == 0x4a, "pin endpoints");
    expect(fa_parse_sha256("zz", parsed) == 0, "bad hex refused");
    uint8_t digest[32];
    fa_sha256((const uint8_t *)"abc", 3, digest);
    expect(memcmp(digest, "\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad", 32) == 0,
           "sha256 abc");
    if (fails) return 1;
    printf("Free Autobuild veneer checks passed\n");
    return 0;
}
