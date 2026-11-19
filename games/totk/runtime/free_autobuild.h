/* Experimental Free Autobuild veneer for base 1.0.0.
   The three supplied writes run from main's executed text. Off replays the
   original instruction; on runs the supplied NOP or MOV W8, WZR. The isolated
   trial starts on until its own switch is saved off. That off choice stays off.
   Production images do not install the veneer. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FA_STUB_WORDS 16
#define FA_FLAG_OFFSET 96u
#define FA_ORIGINAL_SHA256 "8f89130628c3e9996afbecb2b6a646b508cef23375d4bad94b0b24dd1003704a"
#define FA_SITE_BL 0x01714820ull
#define FA_SITE_LDR 0x01717610ull
#define FA_SITE_ADD 0x00d46270ull
#define FA_NOP 0xd503201fu
#define FA_MOV_W8_WZR 0x2a1f03e8u

int free_autobuild_ready(void);
void free_autobuild_set(int enabled);

static inline void fa_sha256(const uint8_t *message, size_t length, uint8_t out[32]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h0=0x6a09e667,h1=0xbb67ae85,h2=0x3c6ef372,h3=0xa54ff53a,h4=0x510e527f,h5=0x9b05688c,h6=0x1f83d9ab,h7=0x5be0cd19;
    size_t blocks = (length + 9 + 63) / 64;
    uint8_t storage[128];
    uint8_t *buf = storage;
    if (blocks * 64 > sizeof storage) return;
    for (size_t i = 0; i < blocks * 64; i++) buf[i] = 0;
    for (size_t i = 0; i < length; i++) buf[i] = message[i];
    buf[length] = 0x80;
    uint64_t bits = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) buf[blocks * 64 - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t block = 0; block < blocks; block++) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = ((uint32_t)buf[block*64+4*i]<<24)|((uint32_t)buf[block*64+4*i+1]<<16)|((uint32_t)buf[block*64+4*i+2]<<8)|buf[block*64+4*i+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = (w[i-15]>>7|w[i-15]<<25) ^ (w[i-15]>>18|w[i-15]<<14) ^ (w[i-15]>>3);
            uint32_t s1 = (w[i-2]>>17|w[i-2]<<15) ^ (w[i-2]>>19|w[i-2]<<13) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,h=h7;
        for (int i = 0; i < 64; i++) {
            uint32_t s1 = (e>>6|e<<26) ^ (e>>11|e<<21) ^ (e>>25|e<<7);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + ch + k[i] + w[i];
            uint32_t s0 = (a>>2|a<<30) ^ (a>>13|a<<19) ^ (a>>22|a<<10);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+s0+maj;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=h;
    }
    uint32_t hs[8] = {h0,h1,h2,h3,h4,h5,h6,h7};
    for (int i = 0; i < 8; i++) { out[4*i]=(uint8_t)(hs[i]>>24); out[4*i+1]=(uint8_t)(hs[i]>>16); out[4*i+2]=(uint8_t)(hs[i]>>8); out[4*i+3]=(uint8_t)hs[i]; }
}

static inline int fa_hex_nibble(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}
/* Index the characters. Do not add an integer to the string literal. */
static inline int fa_parse_sha256(const char *hex, uint8_t out[32]) {
    if (!hex) return 0;
    for (int i = 0; i < 32; i++) {
        int hi = fa_hex_nibble(hex[2 * i]), lo = fa_hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return hex[64] == 0;
}

static inline uint32_t fa_branch(uint64_t from, uint64_t to) {
    int64_t distance = (int64_t)to - (int64_t)from;
    if ((distance & 3) || distance < -(1 << 27) || distance >= (1 << 27)) return 0;
    return 0x14000000u | ((uint32_t)(distance >> 2) & 0x3ffffffu);
}
static inline uint32_t fa_adrp(unsigned rd, uint64_t at, uint64_t target) {
    int64_t pages = (int64_t)(target >> 12) - (int64_t)(at >> 12);
    if (pages < -(1 << 20) || pages >= (1 << 20)) return 0xffffffffu;
    uint32_t imm = (uint32_t)pages & 0x1fffffu;
    return 0x90000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | (rd & 31u);
}
static inline int fa_kind(uint32_t word) {
    if ((word & 0xfc000000u) == 0x94000000u) return 0;                 /* BL */
    if ((word & 0x3b000000u) == 0x39000000u && (word & 0x01400000u) == 0x01400000u) return 1; /* LDR unsigned immediate */
    if ((word & 0x7f000000u) == 0x0b000000u) return 2;                 /* ADD register */
    return -1;
}
static inline uint64_t fa_bl_target(uint32_t word, uint64_t site) {
    int32_t imm = (int32_t)(word & 0x03ffffffu);
    if (imm & 0x02000000) imm -= 0x04000000;
    return site + (uint64_t)((int64_t)imm << 2);
}

/* 16 words. Word 15 is the original instruction and is not executed.
   Returns 0 when the branch or ADRP cannot be encoded. */
static inline int fa_stub(uint32_t out[FA_STUB_WORDS], uint32_t original, uint64_t site, uint64_t stub, uint64_t data) {
    int kind = fa_kind(original);
    uint32_t replacement = kind == 1 ? FA_MOV_W8_WZR : FA_NOP;
    uint32_t flag = fa_adrp(16, stub + 4, data);
    uint32_t back = fa_branch(stub + 24, site + 4);
    if (kind < 0 || flag == 0xffffffffu || !back) return 0;
    for (int i = 0; i < FA_STUB_WORDS; i++) out[i] = FA_NOP;
    out[0] = 0xf81f0ff0u;                                             /* str x16, [sp, #-16]! */
    out[1] = flag;
    out[2] = 0x39400000u | (FA_FLAG_OFFSET << 10) | (16u << 5) | 16u; /* ldrb w16, [x16, #96] */
    out[3] = 0x34000000u | (4u << 5) | 16u;                           /* cbz w16, off path */
    out[4] = 0xf84107f0u;                                             /* ldr x16, [sp], #16 */
    out[5] = replacement;
    out[6] = back;
    out[7] = 0xf84107f0u;
    if (kind == 0) {
        uint32_t page = fa_adrp(30, stub + 32, site + 4);
        uint32_t call = fa_branch(stub + 40, fa_bl_target(original, site));
        if (page == 0xffffffffu || !call) return 0;
        out[8] = page;
        out[9] = 0x91000000u | (((site + 4) & 0xfffu) << 10) | (30u << 5) | 30u;
        out[10] = call;
    } else {
        uint32_t resume = fa_branch(stub + 36, site + 4);
        if (!resume) return 0;
        out[8] = original;
        out[9] = resume;
    }
    out[15] = original;
    return 1;
}
