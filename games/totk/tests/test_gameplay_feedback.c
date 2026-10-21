/* CPU-only tests for the read-only gameplay feedback observer.  Including the
   implementation keeps its verified offsets and real Mach safe-read path under
   test without adding production hooks. */
#define TOTK_GAMEPLAY_FEEDBACK_TEST 1
#include "../runtime/gameplay_feedback.c"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static const char pinned_digest[] =
    "5553c4124bfff15094d4def00894ddccc3a48ee9a2cd4cea06ca8f8c48359ccc";

enum {
    FIXTURE_SIZE = 0x4670000,
    MODULE_OFFSET = 0x1000,
    SCENE_OFFSET = 0x2000,
    RESIDENT_TABLE_OFFSET = 0x3000,
    RESIDENT_OFFSET = 0x4000,
    LINKS_OFFSET = 0x5000,
    DATA_OFFSET = 0x6000,
    ACTOR1_OFFSET = 0x7000,
    NAME1_OFFSET = 0x8000,
    COMPONENTS1_OFFSET = 0x9000,
    COMPONENT1_OFFSET = 0xa000,
    PAUSE_MANAGER_OFFSET = 0xc000,
    PAUSE_HASHES_OFFSET = 0xd000,
    PAUSE_INDICES_OFFSET = 0xe000,
    ACTOR2_OFFSET = 0xf000,
    NAME2_OFFSET = 0x10000,
    COMPONENTS2_OFFSET = 0x11000,
    COMPONENT2_OFFSET = 0x12000,
    PHYSICS1_OFFSET = 0x14000,
    PHYSICS_SET1_OFFSET = 0x15000,
    PHYSICS_P1_OFFSET = 0x16000,
    PHYSICS_A1_OFFSET = 0x17000,
    PHYSICS_B1_OFFSET = 0x18000,
    PHYSICS_ENTRY10_OFFSET = 0x19000,
    PHYSICS_ENTRY11_OFFSET = 0x1a000,
    PHYSICS_ENTRY12_OFFSET = 0x1b000,
    PHYSICS2_OFFSET = 0x1d000,
    PHYSICS_SET2_OFFSET = 0x1e000,
    PHYSICS_P2_OFFSET = 0x1f000,
    PHYSICS_A2_OFFSET = 0x20000,
    PHYSICS_B2_OFFSET = 0x21000,
    PHYSICS_ENTRY20_OFFSET = 0x22000,
    PHYSICS_ENTRY21_OFFSET = 0x23000,
    PHYSICS_ENTRY22_OFFSET = 0x24000,
    PAUSE_REQUESTS_OFFSET = 0x25000,
    BSA1_OFFSET = 0x26000,
    BSA2_OFFSET = 0x27000,
    RANDOM_AUDIO_OFFSET = 0x28000,
    EVENT_MANAGER_OFFSET = 0x29000,
    EVENT_ACTIVE1_OFFSET = 0x2a000,
    EVENT_ACTIVE2_OFFSET = 0x2b000,
    EQUIPMENT1_OFFSET = 0x50000,
    EQUIPMENT2_OFFSET = 0x55000,
    MELEE_ACTOR_OFFSET = 0x2e000,
    MELEE_NAME_OFFSET = 0x2f000,
    MELEE_COMPONENTS_OFFSET = 0x30000,
    MELEE_DYNAMIC_OFFSET = 0x31000,
    MELEE_RECORD_OFFSET = 0x32000,
    SHIELD_ACTOR_OFFSET = 0x33000,
    SHIELD_NAME_OFFSET = 0x34000,
    SHIELD_COMPONENTS_OFFSET = 0x35000,
    SHIELD_DYNAMIC_OFFSET = 0x36000,
    SHIELD_RECORD_OFFSET = 0x37000,
    MAIN_ACTION1_OFFSET = 0x38000,
    MAIN_ACTION2_OFFSET = 0x39000,
    SUB_ACTION1_OFFSET = 0x3a000,
    SUB_ACTION2_OFFSET = 0x3b000,
    BOW_ACTOR_OFFSET = 0x5a000,
    BOW_NAME_OFFSET = 0x5b000,
    BOW_COMPONENTS_OFFSET = 0x5c000,
    BOW_DYNAMIC_OFFSET = 0x5d000,
    BOW_RECORD_OFFSET = 0x5e000,
    PAUSE_CAPACITY = 512
};

enum {
    PAUSE_CONTROLLER = 0,
    PAUSE_MENU = 1,
    PAUSE_RUMBLE = 2,
    PAUSE_STOP = 3
};

static const uint32_t pause_hashes_expected[4] = {
    UINT32_C(0x53920242), UINT32_C(0xe57eb13e),
    UINT32_C(0xb7a94c8f), UINT32_C(0x560a37d9)
};

typedef struct {
    unsigned char *base;
    size_t size;
    unsigned char *module;
    unsigned char *scene;
    unsigned char *resident_table;
    unsigned char *resident;
    unsigned char *links;
    unsigned char *data;
    unsigned char *actors[2];
    unsigned char *names[2];
    unsigned char *components[2];
    unsigned char *player_components[2];
    unsigned char *bsa_components[2];
    unsigned char *main_actions[2];
    unsigned char *sub_actions[2];
    unsigned char *physics[2];
    unsigned char *physics_sets[2];
    unsigned char *physics_p[2];
    unsigned char *physics_a[2];
    unsigned char *physics_b[2];
    unsigned char *physics_entries[2][3];
    unsigned char *pause_manager;
    uint32_t *pause_hashes;
    unsigned char *pause_indices;
    unsigned char *pause_requests;
    unsigned char *random_audio;
    unsigned char *event_manager;
    unsigned char *event_active[2];
    unsigned char *equipment_users[2];
    unsigned char *melee_actor;
    unsigned char *melee_name;
    unsigned char *melee_components;
    unsigned char *melee_dynamic;
    unsigned char *melee_record;
    unsigned char *shield_actor;
    unsigned char *shield_name;
    unsigned char *shield_components;
    unsigned char *shield_dynamic;
    unsigned char *shield_record;
    unsigned char *bow_actor;
    unsigned char *bow_name;
    unsigned char *bow_components;
    unsigned char *bow_dynamic;
    unsigned char *bow_record;
    int writable;
} GameplayFixture;

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void fatal_system(const char *operation) {
    fprintf(stderr, "FAIL: %s: %s\n", operation, strerror(errno));
    exit(2);
}

static void store_pointer(unsigned char *bytes, size_t offset, const void *pointer) {
    const uintptr_t value = (uintptr_t)pointer;
    memcpy(bytes + offset, &value, sizeof value);
}

static void store_u32(unsigned char *bytes, size_t offset, uint32_t value) {
    memcpy(bytes + offset, &value, sizeof value);
}

static void fixture_protect(GameplayFixture *fixture, int writable) {
    const int protection = PROT_READ | (writable ? PROT_WRITE : 0);
    if (mprotect(fixture->base, fixture->size, protection) != 0)
        fatal_system("mprotect gameplay fixture");
    fixture->writable = writable;
}

static void fixture_require_writable(const GameplayFixture *fixture) {
    if (!fixture->writable) {
        fprintf(stderr, "FAIL: attempted to mutate a protected gameplay fixture\n");
        exit(2);
    }
}

static void setup_actor(GameplayFixture *fixture, unsigned index) {
    unsigned char *actor = fixture->actors[index];
    unsigned char *name = fixture->names[index];
    unsigned char *components = fixture->components[index];
    unsigned char *player_component = fixture->player_components[index];
    memcpy(name, "Player", 7);
    store_pointer(actor, 0x210, name);
    store_u32(actor, 0x218, 0x76);
    store_pointer(actor, 0x220, components);
    store_pointer(components, PLAYER_COMPONENT_SLOT, player_component);
    store_pointer(components, 0x120, NULL);
    store_pointer(components, 0x158, fixture->bsa_components[index]);
    store_pointer(components, 0x230, fixture->equipment_users[index]);
    store_pointer(components, 0x50, fixture->physics[index]);
    store_pointer(player_component, 0x18, actor);
    store_pointer(fixture->bsa_components[index], 0x18, actor);
    store_pointer(fixture->bsa_components[index], 0x1f0, fixture->main_actions[index]);
    store_pointer(fixture->bsa_components[index], 0x1f8, fixture->sub_actions[index]);
    store_u32(fixture->main_actions[index], 0x28, 0);
    store_u32(fixture->sub_actions[index], 0x28, 0);
    store_pointer(fixture->equipment_users[index], 0x18, actor);
    store_pointer(fixture->physics[index], 0x20, fixture->physics_sets[index]);
    store_pointer(fixture->physics_sets[index], 0x10, fixture->physics_p[index]);
    store_pointer(fixture->physics_p[index], 8, fixture->physics_a[index]);
    store_pointer(fixture->physics_a[index], 0x40, fixture->physics_b[index]);
    for (unsigned selector = 0; selector < 3; selector++)
        store_pointer(fixture->physics_b[index], 0x38 + selector * sizeof(uintptr_t),
                      fixture->physics_entries[index][selector]);
    store_u32(fixture->physics_b[index], 0x50, 0);
    store_u32(fixture->physics_entries[index][0], 8, 0);
    store_u32(fixture->physics_entries[index][1], 8, 0);
    store_u32(fixture->physics_entries[index][2], 8, 0);
}

static void setup_pause_table(GameplayFixture *fixture) {
    store_u32(fixture->pause_manager, 0, 1);
    store_pointer(fixture->pause_manager, 8, fixture->pause_requests);
    store_pointer(fixture->pause_manager, 0x10, fixture->pause_hashes);
    store_pointer(fixture->pause_manager, 0x18, fixture->pause_indices);
    store_u32(fixture->pause_manager, 0x20, 4);
    store_u32(fixture->pause_manager, 0x24, PAUSE_CAPACITY);
    for (unsigned i = 0; i < 4; i++) {
        const unsigned slot = pause_hashes_expected[i] % PAUSE_CAPACITY;
        fixture->pause_hashes[slot] = pause_hashes_expected[i];
        fixture->pause_indices[slot] = (unsigned char)i;
    }
    store_u32(fixture->pause_requests, 0x1c, 1);
}

static GameplayFixture fixture_create(void) {
    GameplayFixture fixture = {0};
    fixture.size = FIXTURE_SIZE;
    fixture.base = mmap(NULL, fixture.size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (fixture.base == MAP_FAILED)
        fatal_system("mmap gameplay fixture");
    fixture.writable = 1;
    fixture.module = fixture.base + MODULE_OFFSET;
    fixture.scene = fixture.base + SCENE_OFFSET;
    fixture.resident_table = fixture.base + RESIDENT_TABLE_OFFSET;
    fixture.resident = fixture.base + RESIDENT_OFFSET;
    fixture.links = fixture.base + LINKS_OFFSET;
    fixture.data = fixture.base + DATA_OFFSET;
    fixture.actors[0] = fixture.base + ACTOR1_OFFSET;
    fixture.names[0] = fixture.base + NAME1_OFFSET;
    fixture.components[0] = fixture.base + COMPONENTS1_OFFSET;
    fixture.player_components[0] = fixture.base + COMPONENT1_OFFSET;
    fixture.pause_manager = fixture.base + PAUSE_MANAGER_OFFSET;
    fixture.pause_hashes = (uint32_t *)(void *)(fixture.base + PAUSE_HASHES_OFFSET);
    fixture.pause_indices = fixture.base + PAUSE_INDICES_OFFSET;
    fixture.actors[1] = fixture.base + ACTOR2_OFFSET;
    fixture.names[1] = fixture.base + NAME2_OFFSET;
    fixture.components[1] = fixture.base + COMPONENTS2_OFFSET;
    fixture.player_components[1] = fixture.base + COMPONENT2_OFFSET;
    fixture.physics[0] = fixture.base + PHYSICS1_OFFSET;
    fixture.physics_sets[0] = fixture.base + PHYSICS_SET1_OFFSET;
    fixture.physics_p[0] = fixture.base + PHYSICS_P1_OFFSET;
    fixture.physics_a[0] = fixture.base + PHYSICS_A1_OFFSET;
    fixture.physics_b[0] = fixture.base + PHYSICS_B1_OFFSET;
    fixture.physics_entries[0][0] = fixture.base + PHYSICS_ENTRY10_OFFSET;
    fixture.physics_entries[0][1] = fixture.base + PHYSICS_ENTRY11_OFFSET;
    fixture.physics_entries[0][2] = fixture.base + PHYSICS_ENTRY12_OFFSET;
    fixture.physics[1] = fixture.base + PHYSICS2_OFFSET;
    fixture.physics_sets[1] = fixture.base + PHYSICS_SET2_OFFSET;
    fixture.physics_p[1] = fixture.base + PHYSICS_P2_OFFSET;
    fixture.physics_a[1] = fixture.base + PHYSICS_A2_OFFSET;
    fixture.physics_b[1] = fixture.base + PHYSICS_B2_OFFSET;
    fixture.physics_entries[1][0] = fixture.base + PHYSICS_ENTRY20_OFFSET;
    fixture.physics_entries[1][1] = fixture.base + PHYSICS_ENTRY21_OFFSET;
    fixture.physics_entries[1][2] = fixture.base + PHYSICS_ENTRY22_OFFSET;
    fixture.pause_requests = fixture.base + PAUSE_REQUESTS_OFFSET;
    fixture.bsa_components[0] = fixture.base + BSA1_OFFSET;
    fixture.bsa_components[1] = fixture.base + BSA2_OFFSET;
    fixture.main_actions[0] = fixture.base + MAIN_ACTION1_OFFSET;
    fixture.main_actions[1] = fixture.base + MAIN_ACTION2_OFFSET;
    fixture.sub_actions[0] = fixture.base + SUB_ACTION1_OFFSET;
    fixture.sub_actions[1] = fixture.base + SUB_ACTION2_OFFSET;
    fixture.random_audio = fixture.base + RANDOM_AUDIO_OFFSET;
    fixture.event_manager = fixture.base + EVENT_MANAGER_OFFSET;
    fixture.event_active[0] = fixture.base + EVENT_ACTIVE1_OFFSET;
    fixture.event_active[1] = fixture.base + EVENT_ACTIVE2_OFFSET;
    fixture.equipment_users[0] = fixture.base + EQUIPMENT1_OFFSET;
    fixture.equipment_users[1] = fixture.base + EQUIPMENT2_OFFSET;
    fixture.melee_actor = fixture.base + MELEE_ACTOR_OFFSET;
    fixture.melee_name = fixture.base + MELEE_NAME_OFFSET;
    fixture.melee_components = fixture.base + MELEE_COMPONENTS_OFFSET;
    fixture.melee_dynamic = fixture.base + MELEE_DYNAMIC_OFFSET;
    fixture.melee_record = fixture.base + MELEE_RECORD_OFFSET;
    fixture.shield_actor = fixture.base + SHIELD_ACTOR_OFFSET;
    fixture.shield_name = fixture.base + SHIELD_NAME_OFFSET;
    fixture.shield_components = fixture.base + SHIELD_COMPONENTS_OFFSET;
    fixture.shield_dynamic = fixture.base + SHIELD_DYNAMIC_OFFSET;
    fixture.shield_record = fixture.base + SHIELD_RECORD_OFFSET;
    fixture.bow_actor = fixture.base + BOW_ACTOR_OFFSET;
    fixture.bow_name = fixture.base + BOW_NAME_OFFSET;
    fixture.bow_components = fixture.base + BOW_COMPONENTS_OFFSET;
    fixture.bow_dynamic = fixture.base + BOW_DYNAMIC_OFFSET;
    fixture.bow_record = fixture.base + BOW_RECORD_OFFSET;
    memset(fixture.random_audio, 0xa5, 128);

    store_pointer(fixture.base, SCENE_SLOT, fixture.module);
    store_pointer(fixture.module, 0x1e8, fixture.scene);
    store_u32(fixture.scene, 0x50, 14);
    store_pointer(fixture.scene, 0x58, fixture.resident_table);
    store_pointer(fixture.resident_table, 13 * sizeof(uintptr_t), fixture.resident);
    store_u32(fixture.resident, 0x20, 1);
    store_pointer(fixture.resident, 0x28, fixture.links);
    store_pointer(fixture.links, 8, fixture.data);
    setup_actor(&fixture, 0);
    setup_actor(&fixture, 1);
    store_pointer(fixture.data, 0x40, fixture.actors[0]);

    store_pointer(fixture.base, PAUSE_SLOT, fixture.pause_manager);
    setup_pause_table(&fixture);
    store_pointer(fixture.base, EVENT_SLOT, fixture.event_manager);
    store_pointer(fixture.event_manager, 0xb0, fixture.event_manager + 0xbc);
    store_u32(fixture.event_manager, 0xb8, 128);
    return fixture;
}

static void fixture_destroy(GameplayFixture *fixture) {
    if (munmap(fixture->base, fixture->size) != 0)
        fatal_system("munmap gameplay fixture");
    *fixture = (GameplayFixture){0};
}

static Module fixture_module(const GameplayFixture *fixture) {
    Module module = {0};
    module.name = "main";
    module.digest = pinned_digest;
    module.size = fixture->size;
    module.base = fixture->base;
    return module;
}

static void fixture_select_actor(GameplayFixture *fixture, int index) {
    fixture_require_writable(fixture);
    store_pointer(fixture->data, 0x40,
                  index >= 0 ? fixture->actors[(unsigned)index] : NULL);
}

static void fixture_set_position(GameplayFixture *fixture, unsigned actor_index,
                                 float x, float y, float z) {
    fixture_require_writable(fixture);
    const float position[3] = {x, y, z};
    memcpy(fixture->actors[actor_index] + 0x2ac, position, sizeof position);
}

static void fixture_set_surf(GameplayFixture *fixture, unsigned actor_index,
                             size_t flag_offset, int enabled) {
    fixture_require_writable(fixture);
    unsigned char *flag = fixture->player_components[actor_index] + flag_offset;
    *flag = (unsigned char)(enabled ? *flag | 8u : *flag & (unsigned char)~8u);
}

static void fixture_set_dynamic_state(unsigned char *component, unsigned state) {
    memset(component + 0x3d0, 0, 8);
    if (state != 2) {
        component[0x3d0] = 1;
        store_u32(component, 0x3d4, state);
    }
}

static void fixture_set_equipment(GameplayFixture *fixture, unsigned actor_index,
                                  unsigned slot, unsigned char *actor,
                                  unsigned char *name_storage,
                                  unsigned char *components,
                                  unsigned char *dynamic_component,
                                  unsigned char *record, const char *name,
                                  uint32_t id, int dynamic_state) {
    fixture_require_writable(fixture);
    unsigned char *link = fixture->equipment_users[actor_index] + 0x20 + slot * 0x18;
    memset(link, 0, 0x18);
    if (dynamic_state < 0)
        return;
    const size_t length = strlen(name);
    if (length >= 64) {
        fprintf(stderr, "FAIL: equipment fixture name exceeds bounded actor name\n");
        exit(2);
    }
    memset(name_storage, 0, 64);
    memcpy(name_storage, name, length + 1);
    store_u32(actor, 0x10, id);
    store_u32(actor, 0x24, 0);
    store_pointer(actor, 0x210, name_storage);
    store_u32(actor, 0x218, 0x42);
    store_pointer(actor, 0x220, components);
    store_pointer(components, 0x208, dynamic_component);
    store_pointer(dynamic_component, 0x18, actor);
    fixture_set_dynamic_state(dynamic_component, (unsigned)dynamic_state);
    store_pointer(record, 0x40, actor);
    store_u32(record, 0x48, id);
    store_pointer(link, 8, record);
    store_u32(link, 0x10, id);
}

static void fixture_set_shield_state(GameplayFixture *fixture,
                                     unsigned actor_index, int state) {
    fixture_set_equipment(fixture, actor_index, 4, fixture->shield_actor,
                          fixture->shield_name, fixture->shield_components,
                          fixture->shield_dynamic, fixture->shield_record,
                          "Weapon_Shield_Test", UINT32_C(0x53484945), state);
}

static void fixture_set_melee_state(GameplayFixture *fixture,
                                    unsigned actor_index, int state) {
    fixture_set_equipment(fixture, actor_index, 0, fixture->melee_actor,
                          fixture->melee_name, fixture->melee_components,
                          fixture->melee_dynamic, fixture->melee_record,
                          "Weapon_Sword_Test", UINT32_C(0x4d454c45), state);
}

static void fixture_set_bow_state(GameplayFixture *fixture,
                                  unsigned actor_index,int state,float charging) {
    fixture_set_equipment(fixture,actor_index,3,fixture->bow_actor,
                          fixture->bow_name,fixture->bow_components,
                          fixture->bow_dynamic,fixture->bow_record,
                          "Weapon_Bow_Test",UINT32_C(0x424f5754),state);
    memcpy(fixture->equipment_users[actor_index]+0x3ef4,&charging,sizeof charging);
}

static void fixture_set_guard_requested(GameplayFixture *fixture,
                                        unsigned actor_index, int enabled) {
    fixture_require_writable(fixture);
    unsigned char *flag = fixture->bsa_components[actor_index] + 0x24;
    *flag = (unsigned char)(enabled ? *flag | 0x20u :
                           *flag & (unsigned char)~0x20u);
}

static void fixture_set_action_verb(GameplayFixture *fixture,unsigned actor_index,
                                    uint32_t direct,uint32_t fallback) {
    fixture_require_writable(fixture);
    store_pointer(fixture->bsa_components[actor_index],0x1f0,
                  fixture->main_actions[actor_index]);
    store_u32(fixture->main_actions[actor_index],0x28,direct);
    store_u32(fixture->main_actions[actor_index],0x4c,fallback);
}

static void fixture_set_sub_action_verb(GameplayFixture *fixture,unsigned actor_index,
                                        uint32_t direct,uint32_t fallback) {
    fixture_require_writable(fixture);
    store_pointer(fixture->bsa_components[actor_index],0x1f8,
                  fixture->sub_actions[actor_index]);
    store_u32(fixture->sub_actions[actor_index],0x28,direct);
    store_u32(fixture->sub_actions[actor_index],0x4c,fallback);
}

static void fixture_set_player_state_flag(GameplayFixture *fixture,unsigned actor_index,
                                          size_t offset,unsigned char mask,int enabled) {
    fixture_require_writable(fixture);
    unsigned char *flag=fixture->player_components[actor_index]+offset;
    *flag=(unsigned char)(enabled?*flag|mask:*flag&(unsigned char)~mask);
}

static void fixture_set_event(GameplayFixture *fixture, int active_index,
                              const char *name) {
    fixture_require_writable(fixture);
    store_pointer(fixture->event_manager, 0x58,
                  active_index >= 0 ? fixture->event_active[(unsigned)active_index] : NULL);
    if (name) {
        const size_t length = strlen(name);
        if (length >= 128) {
            fprintf(stderr, "FAIL: event fixture name exceeds FixedSafeString capacity\n");
            exit(2);
        }
        memset(fixture->event_manager + 0xbc, 0, 128);
        memcpy(fixture->event_manager + 0xbc, name, length + 1);
    }
}

static void fixture_set_ground_state(GameplayFixture *fixture,
                                     unsigned actor_index, unsigned selector,
                                     uint32_t state) {
    fixture_require_writable(fixture);
    store_u32(fixture->physics_b[actor_index], 0x50, selector);
    const unsigned effective_selector = selector < 3 ? selector : 0;
    store_u32(fixture->physics_entries[actor_index][effective_selector], 8, state);
}

static void fixture_clear_pause(GameplayFixture *fixture) {
    fixture_require_writable(fixture);
    memset(fixture->pause_requests, 0, 4 * 0x20);
    store_u32(fixture->pause_manager, 0, 1);
    store_u32(fixture->pause_requests, 0x1c, 1);
}

static void fixture_set_pause_request_count(GameplayFixture *fixture, unsigned count) {
    fixture_require_writable(fixture);
    store_u32(fixture->pause_manager, 0, count);
}

static void fixture_set_pause_request_active(GameplayFixture *fixture,
                                             unsigned request, int active) {
    fixture_require_writable(fixture);
    store_u32(fixture->pause_requests + request * 0x20, 0x1c,
              active ? 1u : 0u);
}

static void fixture_set_pause_request_hash(GameplayFixture *fixture,
                                           unsigned request, uint32_t hash) {
    fixture_require_writable(fixture);
    store_u32(fixture->pause_requests + request * 0x20, 8, hash);
}

static void fixture_set_pause_request(GameplayFixture *fixture, unsigned request,
                                      unsigned index, int paused) {
    fixture_require_writable(fixture);
    uint32_t masks[4];
    unsigned char *row = fixture->pause_requests + request * 0x20;
    memcpy(masks, row + 0x0c, sizeof masks);
    if (paused)
        masks[index >> 5] |= UINT32_C(1) << (index & 31);
    else
        masks[index >> 5] &= ~(UINT32_C(1) << (index & 31));
    memcpy(row + 0x0c, masks, sizeof masks);
}

static void fixture_set_pause(GameplayFixture *fixture, unsigned index, int paused) {
    fixture_set_pause_request(fixture, 0, index, paused);
}

static void fixture_set_pause_index(GameplayFixture *fixture, unsigned target,
                                    unsigned index) {
    fixture_require_writable(fixture);
    const unsigned slot = pause_hashes_expected[target] % PAUSE_CAPACITY;
    fixture->pause_indices[slot] = (unsigned char)index;
}

static void fixture_set_pause_hash(GameplayFixture *fixture, unsigned target,
                                   uint32_t hash) {
    fixture_require_writable(fixture);
    const unsigned slot = pause_hashes_expected[target] % PAUSE_CAPACITY;
    fixture->pause_hashes[slot] = hash;
}

static void fixture_set_pause_table(GameplayFixture *fixture, unsigned capacity,
                                    const unsigned slots[4]) {
    fixture_require_writable(fixture);
    memset(fixture->pause_hashes, 0, PAUSE_CAPACITY * sizeof *fixture->pause_hashes);
    memset(fixture->pause_indices, 0, PAUSE_CAPACITY);
    store_u32(fixture->pause_manager, 0x20, 4);
    store_u32(fixture->pause_manager, 0x24, capacity);
    for (unsigned target = 0; target < 4; target++) {
        fixture->pause_hashes[slots[target]] = pause_hashes_expected[target];
        fixture->pause_indices[slots[target]] = (unsigned char)target;
    }
}

static void observer_reset(void) {
    main_base = 0;
    last_tick = 0;
    previous_ns = 0;
    previous_actor = 0;
    memset(previous_position, 0, sizeof previous_position);
    previous_vertical_speed = 0.0f;
    previous_grounded = -1;
    airborne_since = 0;
    airborne_peak_y = 0.0f;
    skydive_started_ns = 0;
    skydive_peak_y = 0.0f;
    memset(&observed_events, 0, sizeof observed_events);
    atomic_store(&recent_guard_ns, 0);
    atomic_store(&pending_block_ns, 0);
    atomic_store(&last_guard_audio_ns, 0);
    atomic_store(&pending_melee, 0);
    atomic_store(&player_melee_drawn, 0);
    atomic_store(&audio_context_allowed, 0);
    atomic_store(&read_failure_count, 0);
    atomic_store(&observer_active, 0);
    atomic_store(&observer_epoch, 0);
    sampled_epoch = 0;
    published_epoch = 0;
    atomic_store(&audio_context_epoch, 0);
    atomic_store(&audio_context_ns, 0);
    atomic_store(&extra_context_epoch, 0);
    atomic_store(&extra_context_ns, 0);
    atomic_store(&extra_gameplay_allowed, 0);
    atomic_store(&extra_dialogue_allowed, 0);
    atomic_store(&extra_swimming, 0);
    atomic_store(&extra_climbing, 0);
    atomic_store(&extra_climb_dashing, 0);
    atomic_store(&extra_charging, 0);
    atomic_store(&extra_melee_drawn, 0);
    atomic_store(&extra_melee_attacking, 0);
    atomic_store(&extra_action_verb, 0);
    pthread_mutex_lock(&cue_lock);
    clear_cue_epoch_locked();
    memset(cue_category_counts, 0, sizeof cue_category_counts);
    memset(cue_category_matches, 0, sizeof cue_category_matches);
    memset(cue_category_state_rejects, 0, sizeof cue_category_state_rejects);
    cue_queue_drops = 0;
    cue_last_rejected_id = 0;
    cue_last_rejected_action = 0;
    pthread_mutex_unlock(&cue_lock);
    reset_dialogue();
    memset(&published_context, 0, sizeof published_context);
    memset(&published_events, 0, sizeof published_events);
    memset(&published_evidence, 0, sizeof published_evidence);
    memset(&evidence, 0, sizeof evidence);
}

static void observer_initialize(const Module *module) {
    observer_reset();
    runtime_gameplay_feedback_init(module);
    runtime_gameplay_feedback_set_active(1);
}

typedef struct {
    TOTKSemanticContext context;
    TOTKSemanticEvents events;
    RuntimeGameplayFeedbackEvidence evidence;
} Observation;

static Observation observe(uint64_t now_ns) {
    Observation observation = {0};
    runtime_gameplay_feedback_tick(now_ns);
    runtime_gameplay_feedback_read(&observation.context, &observation.events,
                                   &observation.evidence);
    return observation;
}

static Observation read_observation(void) {
    Observation observation = {0};
    runtime_gameplay_feedback_read(&observation.context, &observation.events,
                                   &observation.evidence);
    return observation;
}

static int context_quiet(TOTKSemanticContext context) {
    return !context.valid && !context.paused && !context.shield_ready &&
           !context.npc_interaction_allowed && !context.shield_raised &&
           !context.shield_surfing && !context.bow_drawn && !context.bow_ready &&
           context.speed_mps == 0.0f;
}

static int events_quiet(TOTKSemanticEvents events) {
    return events.npc_interaction_timestamp_ns == 0 &&
           events.landing_timestamp_ns == 0 &&
           events.shield_block_timestamp_ns == 0 &&
           events.melee_swing_timestamp_ns == 0 &&
           events.melee_kind == TOTK_MELEE_UNKNOWN;
}

static uint64_t fixture_digest(const GameplayFixture *fixture) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < fixture->size; i++) {
        hash ^= fixture->base[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void test_image_guard(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 1.0f, 2.0f, 3.0f);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);

    module.name = "subsdk0";
    observer_initialize(&module);
    expect(observe(UINT64_C(1000000000)).evidence.frames == 0,
           "a module with the wrong name does not enable the observer");

    module = fixture_module(&fixture);
    module.digest = "0553c4124bfff15094d4def00894ddccc3a48ee9a2cd4cea06ca8f8c48359ccc";
    observer_initialize(&module);
    expect(observe(UINT64_C(1000000000)).evidence.frames == 0,
           "a module with the wrong digest does not enable the observer");

    module = fixture_module(&fixture);
    module.digest = NULL;
    observer_initialize(&module);
    expect(observe(UINT64_C(1000000000)).evidence.frames == 0,
           "a module without a digest does not enable the observer");

    module = fixture_module(&fixture);
    module.size = PAUSE_SLOT + 7;
    observer_initialize(&module);
    expect(observe(UINT64_C(1000000000)).evidence.frames == 0,
           "an undersized verified image does not enable the observer");

    module = fixture_module(&fixture);
    module.size = PAUSE_SLOT + 8;
    observer_initialize(&module);
    Observation accepted = observe(UINT64_C(1000000000));
    uintptr_t player_attachment = UINTPTR_MAX;
    memcpy(&player_attachment, fixture.components[0] + 0x120,
           sizeof player_attachment);
    expect(accepted.evidence.frames == 1 && accepted.context.valid &&
               player_attachment == 0,
           "the pinned image observes a Player with no AttachmentRef component");
    fixture_destroy(&fixture);
}

static void test_loading_and_missing_actor_clear_context(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 0.0f, 0.0f);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);

    Observation present = observe(UINT64_C(2000000000));
    expect(present.context.valid && present.context.shield_surfing,
           "a resident Player publishes valid context before loading begins");

    fixture_protect(&fixture, 1);
    store_u32(fixture.scene, 0x50, 13);
    fixture_protect(&fixture, 0);
    Observation loading = observe(UINT64_C(2020000000));
    expect(context_quiet(loading.context) && events_quiet(loading.events) &&
               !strcmp(loading.evidence.action, "unavailable"),
           "a loading scene clears previously published semantic context");

    fixture_protect(&fixture, 1);
    store_u32(fixture.scene, 0x50, 14);
    fixture_select_actor(&fixture, -1);
    fixture_protect(&fixture, 0);
    Observation missing = observe(UINT64_C(2040000000));
    expect(context_quiet(missing.context) && previous_actor == 0 && previous_ns == 0,
           "a scene with no resident Player stays quiet and clears the speed baseline");
    fixture_destroy(&fixture);
}

static void test_bad_and_unmapped_pointers_fail_quiet(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 0.0f, 0.0f);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);

    observer_initialize(&module);
    fixture_protect(&fixture, 1);
    store_pointer(fixture.base, SCENE_SLOT, (void *)(uintptr_t)0x1000);
    fixture_protect(&fixture, 0);
    Observation unmapped = observe(UINT64_C(3000000000));
    expect(context_quiet(unmapped.context) && unmapped.evidence.read_failures > 0,
           "an aligned unmapped scene pointer fails through the Mach safe-read path");

    fixture_protect(&fixture, 1);
    store_pointer(fixture.base, SCENE_SLOT, fixture.module + 1);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    Observation misaligned = observe(UINT64_C(3000000000));
    expect(context_quiet(misaligned.context),
           "a misaligned scene-module pointer fails quiet");

    fixture_protect(&fixture, 1);
    store_pointer(fixture.base, SCENE_SLOT, fixture.module);
    store_pointer(fixture.components[0], PLAYER_COMPONENT_SLOT,
                  (void *)(uintptr_t)0x2000);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    Observation bad_component = observe(UINT64_C(3000000000));
    expect(context_quiet(bad_component.context) &&
               bad_component.evidence.read_failures > 0,
           "an unmapped Player component pointer fails quiet without a crash");

    fixture_protect(&fixture, 1);
    store_pointer(fixture.components[0], PLAYER_COMPONENT_SLOT,
                  fixture.player_components[0]);
    store_pointer(fixture.bsa_components[0], 0x18, fixture.actors[1]);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    Observation wrong_bsa_owner = observe(UINT64_C(3000000000));
    expect(context_quiet(wrong_bsa_owner.context) &&
               !strcmp(wrong_bsa_owner.evidence.detail,
                       "BSARef missing or owner mismatch"),
           "a readable BSARef owned by a different actor fails the observation quiet");

    fixture_protect(&fixture, 1);
    store_pointer(fixture.bsa_components[0], 0x18, fixture.actors[0]);
    fixture_protect(&fixture, 0);
    Observation recovered = observe(UINT64_C(3020000000));
    expect(recovered.context.valid && recovered.evidence.player_frames == 1,
           "restoring the exact Player owner lets the next BSA snapshot recover");
    fixture_destroy(&fixture);
}

static void test_pause_targets_and_bounds(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 0.0f, 0.0f);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(4000000000);

    Observation running = observe(now);
    expect(running.context.valid && !running.context.paused &&
               running.context.shield_surfing,
           "matching pause targets report an unpaused controller and game");

    fixture_protect(&fixture, 1);
    store_u32(fixture.pause_manager, 0x38,
              UINT32_C(1) << PAUSE_MENU);
    fixture_protect(&fixture, 0);
    Observation ignored_cache = observe(now += UINT64_C(20000000));
    expect(ignored_cache.context.valid && !ignored_cache.context.paused,
           "cached manager masks are ignored in favor of live pause requests");

    fixture_protect(&fixture, 1);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_set_pause_request_active(&fixture, 0, 0);
    fixture_protect(&fixture, 0);
    Observation inactive_request = observe(now += UINT64_C(20000000));
    expect(inactive_request.context.valid && !inactive_request.context.paused,
           "an inactive pause request contributes no mask bits");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause_request_count(&fixture, 2);
    fixture_set_pause(&fixture, PAUSE_CONTROLLER, 1);
    fixture_set_pause_request_active(&fixture, 1, 1);
    fixture_set_pause_request(&fixture, 1, PAUSE_MENU, 1);
    fixture_protect(&fixture, 0);
    Observation aggregated = observe(now += UINT64_C(20000000));
    expect(aggregated.context.valid && aggregated.context.paused &&
               aggregated.evidence.controller_paused,
           "active pause requests aggregate their four mask words with OR");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);

    fixture_protect(&fixture, 1);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_protect(&fixture, 0);
    Observation menu = observe(now += UINT64_C(20000000));
    expect(menu.context.valid && menu.context.paused,
           "the matched menu pause target marks semantic context paused");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_RUMBLE, 1);
    fixture_protect(&fixture, 0);
    Observation rumble = observe(now += UINT64_C(20000000));
    expect(rumble.context.valid && rumble.context.paused,
           "the matched rumble pause target marks semantic context paused");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_STOP, 1);
    fixture_protect(&fixture, 0);
    Observation stop = observe(now += UINT64_C(20000000));
    expect(stop.context.valid && stop.context.paused,
           "the matched stop target is folded into rumble pause");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_CONTROLLER, 1);
    fixture_protect(&fixture, 0);
    Observation controller = observe(now += UINT64_C(20000000));
    expect(controller.context.valid && !controller.context.paused &&
               !controller.context.shield_surfing,
           "controller pause suppresses the surf bit without inventing menu pause");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause_hash(&fixture, PAUSE_MENU, UINT32_C(0x12345678));
    fixture_protect(&fixture, 0);
    Observation missing_target = observe(now += UINT64_C(20000000));
    expect(missing_target.context.valid && !missing_target.context.paused,
           "an absent pause target is a valid not-paused result");

    fixture_protect(&fixture, 1);
    fixture_set_pause_hash(&fixture, PAUSE_MENU, pause_hashes_expected[PAUSE_MENU]);
    fixture_set_pause_index(&fixture, PAUSE_MENU, 127);
    fixture_set_pause(&fixture, 127, 1);
    fixture_protect(&fixture, 0);
    Observation upper_valid = observe(now += UINT64_C(20000000));
    expect(upper_valid.context.valid && upper_valid.context.paused,
           "pause index 127 is accepted and read from the final mask bit");

    fixture_protect(&fixture, 1);
    fixture_set_pause_index(&fixture, PAUSE_MENU, 128);
    fixture_protect(&fixture, 0);
    Observation out_of_bounds = observe(now += UINT64_C(20000000));
    expect(context_quiet(out_of_bounds.context),
           "pause index 128 is rejected before indexing the masks");

    fixture_protect(&fixture, 1);
    memset(fixture.pause_hashes, 0, PAUSE_CAPACITY * sizeof *fixture.pause_hashes);
    memset(fixture.pause_indices, 0, PAUSE_CAPACITY);
    store_u32(fixture.pause_manager, 0x20, 4);
    store_u32(fixture.pause_manager, 0x24, 8);
    fixture.pause_hashes[2] = pause_hashes_expected[PAUSE_CONTROLLER];
    fixture.pause_indices[2] = PAUSE_CONTROLLER;
    fixture.pause_hashes[7] = pause_hashes_expected[PAUSE_MENU];
    fixture.pause_indices[7] = PAUSE_MENU;
    fixture_protect(&fixture, 0);
    Observation empty_stop = observe(now += UINT64_C(20000000));
    expect(empty_stop.context.valid && !empty_stop.context.paused,
           "pause hash probing stops at an empty slot and reports the target inactive");

    fixture_protect(&fixture, 1);
    const unsigned collision_slots[4] = {2, 3, 0, 1};
    fixture_set_pause_table(&fixture, 4, collision_slots);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    Observation wrapped = observe(now += UINT64_C(20000000));
    expect(wrapped.context.valid && !wrapped.context.paused,
           "colliding pause hashes probe through wraparound across a full table");

    fixture_protect(&fixture, 1);
    for (unsigned slot = 0; slot < 4; slot++) {
        fixture.pause_hashes[slot] = UINT32_C(0x11111110) + slot;
        fixture.pause_indices[slot] = (unsigned char)slot;
    }
    fixture_protect(&fixture, 0);
    Observation full_miss = observe(now += UINT64_C(20000000));
    expect(full_miss.context.valid && !full_miss.context.paused,
           "a full-cycle pause-target miss is valid and reports the target inactive");

    fixture_protect(&fixture, 1);
    fixture_set_pause_request_count(&fixture, 129);
    fixture_protect(&fixture, 0);
    Observation malformed_requests = observe(now + UINT64_C(20000000));
    expect(context_quiet(malformed_requests.context),
           "a pause request snapshot above its 128-row bound fails quiet");
    fixture_destroy(&fixture);
}

static void test_equipment_grounding_and_surf(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_shield_state(&fixture, 0, 2);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(4500000000);

    Observation surfing = observe(now);
    expect(surfing.context.valid && surfing.context.shield_surfing &&
               !surfing.context.shield_ready && !surfing.context.shield_raised &&
               surfing.evidence.shield_attached && surfing.evidence.grounded,
           "a linked equipped shield plus grounded surf bit publishes surf without guard posture");

    fixture_protect(&fixture, 1);
    fixture_set_surf(&fixture, 0, 0xaea, 0);
    fixture_protect(&fixture, 0);
    Observation attached = observe(now += UINT64_C(20000000));
    expect(attached.context.valid && attached.context.shield_ready &&
               !attached.context.shield_raised && !attached.context.shield_surfing,
           "dynamic shield state 2 publishes readiness without inventing guard posture");

    fixture_protect(&fixture, 1);
    fixture_set_guard_requested(&fixture, 0, 1);
    fixture_protect(&fixture, 0);
    Observation resting_guard_bit = observe(now += UINT64_C(20000000));
    expect(resting_guard_bit.context.valid && resting_guard_bit.context.shield_ready &&
               !resting_guard_bit.context.shield_raised,
           "a BSA guard bit cannot raise a shield whose dynamic state is only ready");

    fixture_protect(&fixture, 1);
    fixture_set_shield_state(&fixture, 0, 1);
    fixture_protect(&fixture, 0);
    Observation guarded = observe(now += UINT64_C(20000000));
    expect(guarded.context.valid && guarded.context.shield_ready &&
               guarded.context.shield_raised,
           "dynamic shield state 1 plus the exact BSA guard bit publishes shield raised");

    fixture_protect(&fixture, 1);
    fixture_set_shield_state(&fixture, 0, -1);
    fixture_protect(&fixture, 0);
    Observation guard_without_shield = observe(now += UINT64_C(20000000));
    expect(guard_without_shield.context.valid && !guard_without_shield.context.shield_ready &&
               !guard_without_shield.context.shield_raised,
           "a BSA guard bit without an EquipmentUser shield link cannot publish guard posture");

    fixture_protect(&fixture, 1);
    fixture_set_shield_state(&fixture, 0, 4);
    fixture_set_guard_requested(&fixture, 0, 0);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_set_ground_state(&fixture, 0, 1, 1);
    fixture_protect(&fixture, 0);
    Observation airborne = observe(now += UINT64_C(20000000));
    expect(airborne.context.valid && !airborne.context.shield_surfing &&
               !airborne.evidence.grounded && airborne.evidence.shield_attached,
           "raw equipment state 4 maps to ready while CCT state 1 suppresses surf");

    fixture_protect(&fixture, 1);
    fixture_set_ground_state(&fixture, 0, 2, 2);
    fixture_protect(&fixture, 0);
    Observation free = observe(now += UINT64_C(20000000));
    expect(free.context.valid && !free.context.shield_surfing &&
               !free.evidence.grounded,
           "CCT state 2 is free and suppresses the surf bit");

    fixture_protect(&fixture, 1);
    fixture_set_ground_state(&fixture, 0, 7, 0);
    fixture_protect(&fixture, 0);
    Observation clamped = observe(now += UINT64_C(20000000));
    expect(clamped.context.valid && clamped.context.shield_surfing &&
               clamped.evidence.grounded,
           "a CCT selector at or above three clamps to grounded entry zero");

    fixture_protect(&fixture, 1);
    fixture_set_ground_state(&fixture, 0, 0, 3);
    fixture_protect(&fixture, 0);
    Observation invalid = observe(now + UINT64_C(20000000));
    expect(context_quiet(invalid.context),
           "an unknown CCT state fails the entire observation quiet");
    fixture_destroy(&fixture);
}

static void test_activation_epochs_and_landing_reset(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_reset();
    runtime_gameplay_feedback_init(&module);
    uint64_t now = UINT64_C(6500000000);

    Observation inactive = observe(now);
    expect(context_quiet(inactive.context) && inactive.evidence.frames == 0,
           "initialization defaults inactive and performs no guest observation");
    runtime_gameplay_feedback_set_active(1);
    expect(context_quiet(read_observation().context),
           "activation masks old published context until the new epoch is sampled");
    expect(observe(now).context.valid,
           "the first active tick publishes context for the current epoch");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.3f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.5f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(60000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.9f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(60000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.8f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    Observation landed = observe(now += UINT64_C(40000000));
    expect(landed.events.landing_timestamp_ns == now,
           "the activation fixture establishes a qualifying 160 ms, 0.7 m landing before cancellation");

    runtime_gameplay_feedback_set_active(0);
    expect(context_quiet(read_observation().context),
           "deactivation immediately masks the last published context");
    runtime_gameplay_feedback_set_active(1);
    Observation before_resample = read_observation();
    expect(context_quiet(before_resample.context),
           "reactivation keeps the prior epoch quiet until a fresh tick");
    Observation resampled = observe(now + UINT64_C(20000000));
    expect(resampled.context.valid && resampled.context.speed_mps == 0.0f &&
               events_quiet(resampled.events),
           "a reactivated epoch resets movement and landing state without replay");
    fixture_destroy(&fixture);
}

static void test_landing_lifecycle_and_resets(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_position(&fixture, 1, 50.0f, 20.0f, 50.0f);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    uint64_t now = UINT64_C(7000000000);

    observer_initialize(&module);
    expect(observe(now).context.valid, "a grounded frame establishes the landing baseline");
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.3f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    expect(events_quiet(observe(now += UINT64_C(20000000)).events),
           "ground-to-air transition starts airtime without firing a landing");
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.5f, 0.0f);
    fixture_protect(&fixture, 0);
    expect(events_quiet(observe(now += UINT64_C(60000000)).events),
           "an observed airborne rise updates the hop peak without firing a landing");
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.95f, 0.0f);
    fixture_protect(&fixture, 0);
    expect(events_quiet(observe(now += UINT64_C(60000000)).events),
           "the preceding fast downward airborne sample remains quiet before impact");
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.85f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    Observation landed = observe(now += UINT64_C(40000000));
    expect(landed.events.landing_timestamp_ns == now &&
               landed.evidence.landing_events == 1,
           "a 160 ms hop with 0.65 m observed descent and fast downward speed emits one landing event");
    Observation still_grounded = observe(now + UINT64_C(20000000));
    expect(still_grounded.events.landing_timestamp_ns == landed.events.landing_timestamp_ns &&
               still_grounded.evidence.landing_events == 1,
           "remaining grounded does not repeat the latched landing event");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(8000000000);
    (void)observe(now);
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.4f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.7f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(60000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.6f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    Observation short_airtime = observe(now + UINT64_C(20000000));
    expect(events_quiet(short_airtime.events) && short_airtime.evidence.landing_events == 0,
           "a fast 0.8 m drop remains quiet when observed airtime is below 150 ms");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(8500000000);
    (void)observe(now);
    fixture_protect(&fixture, 1);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.7f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(120000000));
    fixture_protect(&fixture, 1);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    Observation small_ledge = observe(now + UINT64_C(40000000));
    expect(events_quiet(small_ledge.events) &&
               small_ledge.evidence.landing_events == 0,
           "a fast small terrain step stays quiet after 160 ms air when observed descent is below 0.60 m");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(9000000000);
    (void)observe(now);
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.3f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.5f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(60000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.8f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(80000000));
    fixture_protect(&fixture, 1);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_position(&fixture, 0, 0.0f, 9.7f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_protect(&fixture, 0);
    Observation after_pause = observe(now + UINT64_C(20000000));
    expect(events_quiet(after_pause.events) && after_pause.evidence.landing_events == 0,
           "a pause clears a qualifying in-flight peak before touchdown");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 0);
    fixture_select_actor(&fixture, 0);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(10000000000);
    (void)observe(now);
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.3f, 0.0f);
    fixture_set_ground_state(&fixture, 0, 0, 1);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 10.5f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(60000000));
    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 0.0f, 9.8f, 0.0f);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(100000000));
    fixture_protect(&fixture, 1);
    fixture_select_actor(&fixture, 1);
    fixture_set_ground_state(&fixture, 1, 0, 0);
    fixture_protect(&fixture, 0);
    Observation replacement = observe(now + UINT64_C(20000000));
    expect(replacement.context.valid && replacement.context.speed_mps == 0.0f &&
               events_quiet(replacement.events) && replacement.evidence.landing_events == 0,
           "actor replacement resets a qualifying airborne peak before the new actor appears grounded");
    fixture_destroy(&fixture);
}

static void test_surf_flags_speed_and_actor_replacement(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 10.0f, 20.0f, 30.0f);
    fixture_set_position(&fixture, 1, 100.0f, 20.0f, 200.0f);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_set_surf(&fixture, 1, 0xae2, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(5000000000);

    Observation first = observe(now);
    expect(first.context.valid && first.context.shield_surfing &&
               first.context.speed_mps == 0.0f,
           "the verified 0xaea surf flag publishes surf with a zero-speed baseline");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 0, 10.2f, 999.0f, 30.0f);
    fixture_protect(&fixture, 0);
    Observation moved = observe(now += UINT64_C(20000000));
    expect(moved.context.valid && moved.context.shield_surfing &&
               fabsf(moved.context.speed_mps - 10.0f) < 0.001f,
           "horizontal position delta and frame time produce movement speed while Y is ignored");

    fixture_protect(&fixture, 1);
    fixture_set_surf(&fixture, 0, 0xaea, 0);
    fixture_set_surf(&fixture, 0, 0xae2, 1);
    fixture_protect(&fixture, 0);
    Observation alternate_flag = observe(now += UINT64_C(20000000));
    expect(alternate_flag.context.valid && alternate_flag.context.shield_surfing,
           "the verified 0xae2 surf flag independently publishes surf");

    fixture_protect(&fixture, 1);
    fixture_select_actor(&fixture, 1);
    fixture_protect(&fixture, 0);
    Observation replacement = observe(now += UINT64_C(20000000));
    expect(replacement.context.valid && replacement.context.shield_surfing &&
               replacement.context.speed_mps == 0.0f,
           "replacing the Player actor resets the movement baseline");

    fixture_protect(&fixture, 1);
    fixture_set_position(&fixture, 1, 100.0f, 20.0f, 200.4f);
    fixture_protect(&fixture, 0);
    Observation replacement_moved = observe(now += UINT64_C(20000000));
    expect(fabsf(replacement_moved.context.speed_mps - 20.0f) < 0.001f,
           "the replacement actor establishes its own subsequent speed baseline");

    fixture_protect(&fixture, 1);
    fixture_select_actor(&fixture, -1);
    fixture_protect(&fixture, 0);
    Observation absent = observe(now += UINT64_C(20000000));
    expect(context_quiet(absent.context), "actor disappearance clears context");
    fixture_protect(&fixture, 1);
    fixture_select_actor(&fixture, 1);
    fixture_protect(&fixture, 0);
    Observation returned = observe(now + UINT64_C(20000000));
    expect(returned.context.valid && returned.context.speed_mps == 0.0f,
           "a returning actor starts from a new zero-speed baseline");
    fixture_destroy(&fixture);
}

static void test_guard_audio_matching_and_lifecycle(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_shield_state(&fixture, 0, 1);
    fixture_set_guard_requested(&fixture, 0, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(11000000000);

    Observation guarded = observe(now);
    expect(guarded.context.shield_raised &&
               guarded.evidence.guard_cue_fingerprints == 28,
           "guard audio matching uses all 28 generated ADPCM prefixes");
    runtime_gameplay_feedback_audio((uint64_t)(uintptr_t)fixture.random_audio, 128,
                                    now + UINT64_C(5000000));
    Observation random = observe(now + UINT64_C(20000000));
    expect(events_quiet(random.events) && random.evidence.block_events == 0,
           "a random 128-byte audio prefix never queues a block event");

    const uint64_t first_audio = now + UINT64_C(25000000);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 127, first_audio);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128, first_audio);
    expect(events_quiet(read_observation().events),
           "a matching audio callback remains pending until the observer tick handoff");
    Observation first_block = observe(now + UINT64_C(40000000));
    expect(first_block.events.shield_block_timestamp_ns == first_audio &&
               first_block.evidence.block_events == 1,
           "a matching generated guard prefix hands off its exact callback timestamp");

    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[1].prefix, 128,
        first_audio + UINT64_C(119000000));
    const uint64_t second_audio = first_audio + UINT64_C(120000000);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[1].prefix, 128, second_audio);
    Observation second_block = observe(now + UINT64_C(160000000));
    expect(second_block.events.shield_block_timestamp_ns == second_audio &&
               second_block.evidence.block_events == 2,
           "guard audio rejects 119 ms repetition and accepts the 120 ms cooldown boundary");

    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[2].prefix, 128,
        now + UINT64_C(310000000));
    Observation stale = observe(now + UINT64_C(330000000));
    expect(stale.events.shield_block_timestamp_ns == second_audio &&
               stale.evidence.block_events == 2,
           "guard audio at the exact 150 ms context boundary is stale and rejected");

    fixture_protect(&fixture, 1);
    fixture_set_guard_requested(&fixture, 0, 0);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(12000000000);
    expect(!observe(now).context.shield_raised,
           "a fresh epoch without the BSA guard bit has no raised-shield context");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(observe(now + UINT64_C(20000000)).events),
           "a matching cue without guarded context is ignored");

    fixture_protect(&fixture, 1);
    fixture_set_guard_requested(&fixture, 0, 1);
    fixture_set_shield_state(&fixture, 0, -1);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(13000000000);
    (void)observe(now);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(observe(now + UINT64_C(20000000)).events),
           "a matching cue without an attached shield is ignored");

    fixture_protect(&fixture, 1);
    fixture_set_shield_state(&fixture, 0, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(14000000000);
    expect(observe(now).context.paused,
           "the audio pause case begins from a valid paused observation");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(observe(now + UINT64_C(20000000)).events),
           "paused context rejects a matching guard cue");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(15000000000);
    (void)observe(now);
    runtime_gameplay_feedback_set_active(0);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128,
        now + UINT64_C(5000000));
    runtime_gameplay_feedback_set_active(1);
    expect(context_quiet(read_observation().context),
           "audio epoch cancellation immediately masks the old guard context");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)guard_audio_fingerprints[0].prefix, 128,
        now + UINT64_C(10000000));
    Observation new_epoch = observe(now + UINT64_C(20000000));
    expect(new_epoch.context.valid && events_quiet(new_epoch.events),
           "audio callbacks from inactive and unsampled epochs never replay after reactivation");
    fixture_destroy(&fixture);
}

static unsigned melee_fingerprint_index(int kind) {
    for (unsigned i = 0; i < MELEE_AUDIO_FINGERPRINT_COUNT; i++)
        if (melee_audio_fingerprints[i].kind == kind)
            return i;
    fprintf(stderr, "FAIL: generated melee table has no kind %d entry\n", kind);
    exit(2);
}

static void test_melee_audio_matching_and_lifecycle(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_set_melee_state(&fixture, 0, 1);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(18000000000);
    const unsigned blunt = melee_fingerprint_index(TOTK_MELEE_BLUNT);
    const unsigned sharp = melee_fingerprint_index(TOTK_MELEE_SHARP);

    Observation drawn = observe(now);
    expect(drawn.context.valid && drawn.evidence.melee_drawn &&
               drawn.evidence.melee_cue_fingerprints == 44,
           "slot-zero drawn melee state enables all 44 verified player cue prefixes");
    runtime_gameplay_feedback_audio((uint64_t)(uintptr_t)fixture.random_audio, 128,
                                    now + UINT64_C(5000000));
    Observation random = observe(now += UINT64_C(20000000));
    expect(events_quiet(random.events) && random.evidence.melee_events == 0,
           "random audio cannot synthesize a melee swing event");

    const uint64_t blunt_audio = now + UINT64_C(5000000);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[blunt].prefix, 127,
        blunt_audio);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[blunt].prefix, 128,
        blunt_audio);
    expect(events_quiet(read_observation().events),
           "a verified blunt cue stays pending until the observer tick");
    Observation blunt_swing = observe(now += UINT64_C(20000000));
    expect(blunt_swing.events.melee_swing_timestamp_ns == (blunt_audio & ~UINT64_C(3)) &&
               blunt_swing.events.melee_kind == TOTK_MELEE_BLUNT &&
               blunt_swing.evidence.melee_events == 1,
           "the blunt callback hands off its packed timestamp and class exactly once");

    const uint64_t sharp_audio = now + UINT64_C(5000000);
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[sharp].prefix, 128,
        sharp_audio);
    Observation sharp_swing = observe(now += UINT64_C(20000000));
    expect(sharp_swing.events.melee_swing_timestamp_ns == (sharp_audio & ~UINT64_C(3)) &&
               sharp_swing.events.melee_kind == TOTK_MELEE_SHARP &&
               sharp_swing.evidence.melee_events == 2,
           "a newer sharp callback replaces the published melee event with its exact class");

    fixture_protect(&fixture, 1);
    fixture_set_melee_state(&fixture, 0, 2);
    fixture_protect(&fixture, 0);
    Observation sheathed = observe(now += UINT64_C(20000000));
    expect(!sheathed.evidence.melee_drawn && events_quiet(sheathed.events),
           "leaving drawn state immediately clears the latched melee event");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[blunt].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(observe(now + UINT64_C(20000000)).events),
           "a verified melee cue is ignored while the weapon is not drawn");

    fixture_protect(&fixture, 1);
    fixture_set_melee_state(&fixture, 0, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(19000000000);
    expect(observe(now).evidence.melee_drawn,
           "fresh normal context re-enables drawn-melee matching");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[sharp].prefix, 128,
        now + UINT64_C(150000000));
    Observation stale = observe(now + UINT64_C(160000000));
    expect(events_quiet(stale.events) && stale.evidence.melee_events == 0,
           "melee audio at the exact 150 ms context lease boundary is rejected");

    fixture_protect(&fixture, 1);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(20000000000);
    Observation paused = observe(now);
    expect(paused.context.paused && !paused.evidence.melee_drawn,
           "paused context suppresses the drawn-melee audio lease");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[blunt].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(observe(now + UINT64_C(20000000)).events),
           "a verified melee cue during pause is dropped without replay");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(21000000000);
    expect(observe(now).evidence.melee_drawn,
           "the epoch test starts with a verified drawn weapon");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[sharp].prefix, 128,
        now + UINT64_C(5000000));
    expect(events_quiet(read_observation().events),
           "a melee callback is pending before lifecycle cancellation");
    runtime_gameplay_feedback_set_active(0);
    runtime_gameplay_feedback_set_active(1);
    expect(context_quiet(read_observation().context),
           "reactivation masks the old melee context until a fresh epoch sample");
    runtime_gameplay_feedback_audio(
        (uint64_t)(uintptr_t)melee_audio_fingerprints[sharp].prefix, 128,
        now + UINT64_C(10000000));
    Observation reactivated = observe(now + UINT64_C(20000000));
    expect(reactivated.context.valid && reactivated.evidence.melee_drawn &&
               events_quiet(reactivated.events) && reactivated.evidence.melee_events == 0,
           "pending and pre-sample melee callbacks never cross an observer epoch");
    fixture_destroy(&fixture);
}

static void test_npc_event_sampling_and_allpause(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 0.0f, 10.0f, 0.0f);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_CONTROLLER, 1);
    fixture_set_pause(&fixture, PAUSE_RUMBLE, 1);
    fixture_set_event(&fixture, -1, npc_dialogue_flows[0]);
    fixture_protect(&fixture, 0);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    uint64_t now = UINT64_C(16000000000);

    Observation idle = observe(now);
    expect(idle.context.valid && idle.context.paused &&
               idle.evidence.npc_event_ids == NPC_DIALOGUE_FLOW_COUNT &&
               !idle.evidence.event_active,
           "an idle EventMgr sample arms NPC entry while AllPause remains explicit");
    fixture_protect(&fixture, 1);
    fixture_set_event(&fixture, 0, npc_dialogue_flows[0]);
    fixture_protect(&fixture, 0);
    expect(events_quiet(observe(now += UINT64_C(20000000)).events),
           "the first active named event sample establishes a stable candidate");
    Observation accepted = observe(now += UINT64_C(20000000));
    expect(accepted.context.paused && accepted.context.npc_interaction_allowed &&
               accepted.events.npc_interaction_timestamp_ns == now &&
               accepted.evidence.npc_events == 1 && accepted.evidence.event_active &&
               !strcmp(accepted.evidence.event_name, npc_dialogue_flows[0]),
           "two stable supported NPCTalk samples emit one event despite Controller and Rumble AllPause");
    Observation repeated = observe(now += UINT64_C(20000000));
    expect(repeated.events.npc_interaction_timestamp_ns == accepted.events.npc_interaction_timestamp_ns &&
               repeated.evidence.npc_events == 1,
           "a retained active EventMgr entry does not repeat its NPC event");

    fixture_protect(&fixture, 1);
    fixture_set_event(&fixture, -1, NULL);
    fixture_protect(&fixture, 0);
    Observation retained_text = observe(now += UINT64_C(20000000));
    expect(!retained_text.context.npc_interaction_allowed &&
               !retained_text.evidence.event_active && retained_text.evidence.event_name[0] == 0,
           "a null active pointer is idle even when the inline string retains an old NPC name");

    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture, PAUSE_MENU, 1);
    fixture_set_event(&fixture, 1, npc_dialogue_flows[0]);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    Observation after_menu = observe(now + UINT64_C(20000000));
    expect(after_menu.context.npc_interaction_allowed &&
               after_menu.events.npc_interaction_timestamp_ns ==
                   accepted.events.npc_interaction_timestamp_ns &&
               after_menu.evidence.npc_events == 1,
           "a menu pause resets NPC entry and the still-active event does not replay after resume");

    fixture_protect(&fixture, 1);
    fixture_set_event(&fixture, -1, NULL);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    observer_initialize(&module);
    now = UINT64_C(17000000000);
    (void)observe(now);
    fixture_protect(&fixture, 1);
    fixture_set_pause_request_hash(&fixture, 0, UINT32_C(0x0eafe200));
    fixture_set_event(&fixture, 0, npc_dialogue_flows[0]);
    fixture_protect(&fixture, 0);
    Observation hard = observe(now += UINT64_C(20000000));
    expect(hard.context.paused && hard.evidence.hard_paused,
           "an active LoadingPause request hash publishes hard pause");
    (void)observe(now += UINT64_C(20000000));
    fixture_protect(&fixture, 1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture, 0);
    (void)observe(now += UINT64_C(20000000));
    Observation after_hard = observe(now + UINT64_C(20000000));
    expect(after_hard.context.npc_interaction_allowed && events_quiet(after_hard.events) &&
               after_hard.evidence.npc_events == 0,
           "hard loading resets NPC entry and prevents a delayed event for the retained active row");
    fixture_destroy(&fixture);
}

static unsigned extra_fingerprint_index(unsigned category,unsigned ordinal) {
    for(unsigned i=0;i<EXTRA_AUDIO_FINGERPRINT_COUNT;i++){
        if(extra_audio_fingerprints[i].category!=category)continue;
        if(!ordinal--)return i;
    }
    fprintf(stderr,"FAIL: generated extra-audio table has no category %u ordinal\n",category);
    exit(2);
}

static unsigned extra_fingerprint_highest(unsigned category) {
    unsigned found=UINT_MAX;
    for(unsigned i=0;i<EXTRA_AUDIO_FINGERPRINT_COUNT;i++){
        if(category && extra_audio_fingerprints[i].category!=category)continue;
        if(found==UINT_MAX || extra_audio_fingerprints[i].cue_id>
                              extra_audio_fingerprints[found].cue_id)found=i;
    }
    if(found!=UINT_MAX)return found;
    fprintf(stderr,"FAIL: generated extra-audio table has no category %u\n",category);
    exit(2);
}

static unsigned drain_cues(RuntimeHapticCueObservation *out,unsigned capacity,unsigned *epoch) {
    memset(out,0,capacity*sizeof *out);
    return runtime_gameplay_feedback_cues(out,capacity,epoch);
}

static void test_exact_action_state_observation(void) {
    GameplayFixture fixture=fixture_create();
    fixture_set_position(&fixture,0,0.0f,10.0f,0.0f);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture,0);
    Module module=fixture_module(&fixture);observer_initialize(&module);
    uint64_t now=UINT64_C(18000000000);

    Observation none=observe(now);
    expect(none.context.valid && !none.evidence.swimming && !none.evidence.climbing &&
               !none.evidence.climb_dashing && !none.evidence.charging &&
               !none.evidence.melee_attacking && !none.evidence.skydiving &&
               none.evidence.action_verb==0,
           "a valid ActionVerb None snapshot publishes no authored movement or charge state");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x61,0);
    fixture_protect(&fixture,0);
    Observation swim=observe(now+=UINT64_C(20000000));
    expect(swim.context.valid && swim.evidence.swimming && !swim.evidence.climbing &&
               swim.evidence.action_verb==0x61,
           "the exact BSA current-main-action Swim verb publishes swimming");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x11c,0x46);
    fixture_protect(&fixture,0);
    Observation climb=observe(now+=UINT64_C(20000000));
    expect(climb.evidence.climbing && !climb.evidence.swimming &&
               climb.evidence.action_verb==0x46,
           "the game's 0x11c sentinel selects the fallback Climb verb at action +0x4c");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x45,0);
    fixture_protect(&fixture,0);
    Observation climb_dash=observe(now+=UINT64_C(20000000));
    expect(climb_dash.evidence.climb_dashing && !climb_dash.evidence.climbing &&
               climb_dash.evidence.action_verb==0x45,
           "the authored Player DashUp action publishes climb dash without classifying ordinary Jump");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x0d,0);
    fixture_protect(&fixture,0);
    Observation ordinary_jump=observe(now+=UINT64_C(20000000));
    expect(!ordinary_jump.evidence.climb_dashing && ordinary_jump.evidence.action_verb==0x0d,
           "ordinary Jump is distinct from the exact DashUp climb-jump state");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,1,0);
    fixture_set_player_state_flag(&fixture,0,0x1424,0x10,1);
    fixture_set_player_state_flag(&fixture,0,0x1425,0x02,1);
    fixture_protect(&fixture,0);
    Observation query_flags=observe(now+=UINT64_C(20000000));
    expect(query_flags.evidence.swimming && query_flags.evidence.climbing &&
               query_flags.evidence.action_verb==1,
           "the exact CheckPlayerState fallback bits at +0x1424/+0x1425 are honored with a valid action");

    fixture_protect(&fixture,1);
    fixture_set_player_state_flag(&fixture,0,0x1424,0x10,0);
    fixture_set_player_state_flag(&fixture,0,0x1425,0x02,0);
    fixture_set_action_verb(&fixture,0,0x43,0);
    fixture_protect(&fixture,0);
    Observation charge=observe(now+=UINT64_C(20000000));
    expect(charge.evidence.charging && charge.evidence.action_verb==0x43 &&
               !charge.evidence.swimming && !charge.evidence.climbing,
           "the authored Charge scheduler ActionVerb publishes charging without an input timer");

    const unsigned melee_verbs[]={0x81,0x83,0x84,0x87,0x88,0x89,0x8c,0x90,0x94,0x103};
    for(unsigned i=0;i<sizeof melee_verbs/sizeof melee_verbs[0];i++){
        fixture_protect(&fixture,1);
        fixture_set_action_verb(&fixture,0,melee_verbs[i],0);
        fixture_protect(&fixture,0);
        Observation attack=observe(now+=UINT64_C(20000000));
        expect(attack.evidence.melee_attacking &&
                   attack.evidence.action_verb==melee_verbs[i],
               "an exact Player-pack melee attack verb publishes attack state");
    }
    const unsigned excluded_verbs[]={0x43,0x8a,0x8b,0x8d,0x05,0x21};
    for(unsigned i=0;i<sizeof excluded_verbs/sizeof excluded_verbs[0];i++){
        fixture_protect(&fixture,1);
        fixture_set_action_verb(&fixture,0,excluded_verbs[i],0);
        fixture_protect(&fixture,0);
        Observation excluded=observe(now+=UINT64_C(20000000));
        expect(!excluded.evidence.melee_attacking,
               "charge wait, ranged, adjacent, glide, and fall verbs cannot infer a melee attack");
    }

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x13,0);
    fixture_protect(&fixture,0);
    Observation air_dive=observe(now+=UINT64_C(20000000));
    expect(air_dive.evidence.skydiving && air_dive.evidence.action_verb==0x13,
           "the authored Player AirDive action publishes exact skydive mode");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x14,0);
    fixture_protect(&fixture,0);
    Observation guide_dive=observe(now+=UINT64_C(20000000));
    expect(guide_dive.evidence.skydiving && guide_dive.evidence.action_verb==0x14,
           "the authored Player GuideDive action also publishes exact skydive mode");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x21,0);
    fixture_protect(&fixture,0);
    Observation fall=observe(now+=UINT64_C(20000000));
    expect(!fall.evidence.skydiving && fall.evidence.action_verb==0x21,
           "ordinary Fall cannot activate skydive mode");

    fixture_protect(&fixture,1);
    store_pointer(fixture.bsa_components[0],0x1f0,NULL);
    fixture_protect(&fixture,0);
    Observation missing=observe(now+UINT64_C(20000000));
    expect(missing.context.valid && !missing.evidence.swimming && !missing.evidence.climbing &&
               !missing.evidence.climb_dashing && !missing.evidence.charging &&
               !missing.evidence.melee_attacking && !missing.evidence.skydiving &&
               missing.evidence.action_verb==0 &&
               missing.evidence.grounded,
           "a missing current action fails only authored action state quiet and preserves baseline observation");
    fixture_destroy(&fixture);
}

static void test_bow_draw_state_lifecycle(void) {
    GameplayFixture fixture=fixture_create();
    fixture_set_position(&fixture,0,0.0f,10.0f,0.0f);
    fixture_set_position(&fixture,1,2.0f,10.0f,0.0f);
    fixture_clear_pause(&fixture);
    fixture_set_sub_action_verb(&fixture,0,0,0);
    fixture_set_bow_state(&fixture,0,2,0.0f);
    fixture_protect(&fixture,0);
    Module module=fixture_module(&fixture);observer_initialize(&module);
    uint64_t now=UINT64_C(21000000000);

    Observation sheathed_ready=observe(now);
    expect(sheathed_ready.context.valid && sheathed_ready.context.bow_ready &&
               !sheathed_ready.context.bow_drawn && sheathed_ready.evidence.bow_ready,
           "a live sheathed slot-3 bow is ready before any UseBow action or charging query");

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,1,0.0f);
    fixture_protect(&fixture,0);
    Observation drawn_ready=observe(now+=UINT64_C(20000000));
    expect(drawn_ready.context.bow_ready && !drawn_ready.context.bow_drawn,
           "a live drawn slot-3 bow is ready before the active bow action starts");

    fixture_protect(&fixture,1);
    fixture_set_sub_action_verb(&fixture,0,0x3b,0);
    fixture_set_bow_state(&fixture,0,1,0.5f);
    fixture_protect(&fixture,0);
    Observation drawing=observe(now+=UINT64_C(20000000));
    expect(drawing.context.valid && drawing.context.bow_ready && drawing.context.bow_drawn &&
               drawing.evidence.bow_ready && drawing.evidence.bow_drawn &&
               drawing.evidence.sub_action_verb==0x3b,
           "UseBow in the BSA Sub slot plus a drawn slot-3 bow and positive game charging query publishes bow draw");

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,1,4.0f);
    fixture_protect(&fixture,0);
    Observation held=observe(now+=UINT64_C(20000000));
    expect(held.context.bow_ready && held.context.bow_drawn,
           "a persistent positive shipped charging query remains active without treating its value as a draw fraction");

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,1,0.0f);
    fixture_protect(&fixture,0);
    Observation released=observe(now+=UINT64_C(20000000));
    expect(released.context.bow_ready && !released.context.bow_drawn &&
               released.evidence.bow_ready && released.evidence.sub_action_verb==0x3b,
           "release clears active bow draw but leaves the equipped bow ready for a smooth trigger return");

    const float rejected_values[]={-1.0f,NAN,INFINITY};
    for(unsigned i=0;i<sizeof rejected_values/sizeof rejected_values[0];i++){
        fixture_protect(&fixture,1);
        fixture_set_bow_state(&fixture,0,1,rejected_values[i]);
        fixture_protect(&fixture,0);
        Observation rejected=observe(now+=UINT64_C(20000000));
        expect(rejected.context.bow_ready && !rejected.context.bow_drawn,
               "negative and nonfinite charging fields reject active draw without disabling readiness");
    }

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,2,1.0f);
    fixture_protect(&fixture,0);
    Observation sheathed=observe(now+=UINT64_C(20000000));
    expect(sheathed.context.bow_ready && !sheathed.context.bow_drawn,
           "sheathed remains ready but cannot publish active bow draw");

    const int rejected_states[]={3,-1,5};
    for(unsigned i=0;i<sizeof rejected_states/sizeof rejected_states[0];i++){
        fixture_protect(&fixture,1);
        fixture_set_bow_state(&fixture,0,rejected_states[i],1.0f);
        fixture_protect(&fixture,0);
        Observation rejected=observe(now+=UINT64_C(20000000));
        expect(!rejected.context.bow_ready && !rejected.context.bow_drawn,
               "thrown, absent, and malformed bow states cannot publish readiness or active draw");
    }

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,1,1.0f);
    store_pointer(fixture.equipment_users[0],0x18,fixture.actors[1]);
    fixture_protect(&fixture,0);
    Observation wrong_owner=observe(now+=UINT64_C(20000000));
    expect(wrong_owner.context.valid && !wrong_owner.context.bow_ready &&
               !wrong_owner.context.bow_drawn,
           "a wrong EquipmentUser owner fails bow state quiet");

    fixture_protect(&fixture,1);
    store_pointer(fixture.equipment_users[0],0x18,fixture.actors[0]);
    fixture_set_bow_state(&fixture,0,1,1.0f);
    store_u32(fixture.bow_record,0x48,UINT32_C(0x52454359));
    fixture_protect(&fixture,0);
    Observation recycled=observe(now+=UINT64_C(20000000));
    expect(!recycled.context.bow_ready && !recycled.context.bow_drawn,
           "a recycled slot record whose actor ID no longer matches fails quiet");

    fixture_protect(&fixture,1);
    fixture_set_bow_state(&fixture,0,1,1.0f);
    fixture_set_sub_action_verb(&fixture,0,0x2f,0);
    fixture_set_action_verb(&fixture,0,0x3b,0);
    fixture_protect(&fixture,0);
    Observation throwing=observe(now+=UINT64_C(20000000));
    expect(throwing.context.bow_ready && !throwing.context.bow_drawn &&
               throwing.evidence.sub_action_verb==0x2f,
           "a Main UseBow-like or throwing state cannot substitute for exact Sub-slot UseBow");

    fixture_protect(&fixture,1);
    fixture_set_sub_action_verb(&fixture,0,0x11c,0x3b);
    fixture_protect(&fixture,0);
    Observation fallback=observe(now+=UINT64_C(20000000));
    expect(fallback.context.bow_ready && fallback.context.bow_drawn,
           "the exact BSA action sentinel selects Sub fallback UseBow at action +0x4c");

    fixture_protect(&fixture,1);
    fixture_set_pause(&fixture,PAUSE_MENU,1);
    fixture_protect(&fixture,0);
    Observation paused=observe(now+=UINT64_C(20000000));
    expect(paused.context.valid && paused.context.paused && !paused.context.bow_ready &&
               !paused.context.bow_drawn && !paused.evidence.bow_ready &&
               !paused.evidence.bow_drawn,
           "menu pause clears bow readiness and active draw while preserving a valid paused observation");

    fixture_protect(&fixture,1);
    fixture_clear_pause(&fixture);
    fixture_set_pause(&fixture,PAUSE_CONTROLLER,1);
    fixture_protect(&fixture,0);
    Observation controller_paused=observe(now+=UINT64_C(20000000));
    expect(!controller_paused.context.bow_ready && !controller_paused.context.bow_drawn,
           "player-controller pause independently clears bow readiness and active draw");

    fixture_protect(&fixture,1);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture,0);
    runtime_gameplay_feedback_set_active(0);
    expect(context_quiet(read_observation().context),
           "observer deactivation masks a previously published bow state");
    runtime_gameplay_feedback_set_active(1);
    expect(context_quiet(read_observation().context),
           "a fresh epoch cannot reuse the prior bow state before resampling");
    Observation fresh=observe(now+=UINT64_C(20000000));
    expect(fresh.context.bow_ready && fresh.context.bow_drawn,
           "a fresh consistent sample can publish bow draw in the new epoch");

    fixture_protect(&fixture,1);
    fixture_select_actor(&fixture,1);
    fixture_set_sub_action_verb(&fixture,1,0x3b,0);
    fixture_set_bow_state(&fixture,1,-1,1.0f);
    fixture_protect(&fixture,0);
    Observation replacement=observe(now+=UINT64_C(20000000));
    expect(replacement.context.valid && !replacement.context.bow_ready &&
               !replacement.context.bow_drawn,
           "actor replacement cannot retain the previous Player's bow state");

    fixture_protect(&fixture,1);
    fixture_select_actor(&fixture,0);
    store_pointer(fixture.bsa_components[0],0x1f8,NULL);
    fixture_protect(&fixture,0);
    Observation missing_sub=observe(now+UINT64_C(20000000));
    expect(missing_sub.context.valid && missing_sub.context.bow_ready &&
               !missing_sub.context.bow_drawn && missing_sub.evidence.bow_ready &&
               !missing_sub.evidence.bow_drawn && missing_sub.evidence.sub_action_verb==0 &&
               missing_sub.evidence.grounded,
           "an invalid Sub action disables only active draw and preserves independent bow readiness and baseline observation");
    fixture_destroy(&fixture);
}

static void test_skydive_observation_lifecycle(void) {
    GameplayFixture fixture=fixture_create();
    fixture_set_position(&fixture,0,0.0f,100.0f,0.0f);
    fixture_clear_pause(&fixture);
    fixture_set_action_verb(&fixture,0,0x28,0);
    fixture_protect(&fixture,0);
    Module module=fixture_module(&fixture);observer_initialize(&module);
    uint64_t now=UINT64_C(22000000000);
    (void)observe(now);

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x13,0);
    fixture_set_position(&fixture,0,0.0f,99.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation entered=observe(now+=UINT64_C(100000000));
    expect(entered.evidence.skydiving && entered.evidence.skydive_started_ns==now &&
               entered.evidence.skydive_downward_speed_mps==0.0f &&
               entered.evidence.skydive_descent_m==0.0f,
           "AirDive entry starts a new observed interval without counting pre-entry movement");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,97.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation falling=observe(now+=UINT64_C(100000000));
    expect(falling.evidence.skydiving && falling.evidence.skydive_started_ns==entered.evidence.skydive_started_ns &&
               fabsf(falling.evidence.skydive_downward_speed_mps-20.0f)<0.001f &&
               fabsf(falling.evidence.skydive_descent_m-2.0f)<0.001f,
           "continued exact skydive derives positive downward speed and drop from copied positions");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,100.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation rose=observe(now+=UINT64_C(100000000));
    expect(rose.evidence.skydive_downward_speed_mps==0.0f &&
               rose.evidence.skydive_descent_m==0.0f,
           "upward movement is not reported as downward speed and advances the observed high-water mark");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,95.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation from_peak=observe(now+=UINT64_C(100000000));
    expect(fabsf(from_peak.evidence.skydive_downward_speed_mps-50.0f)<0.001f &&
               fabsf(from_peak.evidence.skydive_descent_m-5.0f)<0.001f,
           "skydive descent is measured from the highest position observed during the exact state");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,85.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation dive100=observe(now+=UINT64_C(100000000));
    expect(dive100.evidence.skydive_started_ns==entered.evidence.skydive_started_ns &&
               fabsf(dive100.evidence.skydive_downward_speed_mps-100.0f)<0.001f &&
               fabsf(dive100.evidence.skydive_descent_m-15.0f)<0.001f,
           "100 m/s exact dive motion retains wind continuity beyond the landing observer limit");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,70.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation dive150=observe(now+=UINT64_C(100000000));
    expect(dive150.evidence.skydive_started_ns==entered.evidence.skydive_started_ns &&
               fabsf(dive150.evidence.skydive_downward_speed_mps-150.0f)<0.001f &&
               fabsf(dive150.evidence.skydive_descent_m-30.0f)<0.001f,
           "150 m/s exact dive motion retains its original wind clock and descent");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,50.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation dive200=observe(now+=UINT64_C(100000000));
    expect(dive200.evidence.skydive_started_ns==entered.evidence.skydive_started_ns &&
               fabsf(dive200.evidence.skydive_downward_speed_mps-200.0f)<0.001f &&
               fabsf(dive200.evidence.skydive_descent_m-50.0f)<0.001f,
           "the finite 200 m/s wind-only engineering ceiling remains continuous inclusively");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,29.9f,0.0f);
    fixture_protect(&fixture,0);
    Observation dive201=observe(now+=UINT64_C(100000000));
    expect(dive201.evidence.skydiving && dive201.evidence.skydive_started_ns==now &&
               dive201.evidence.skydive_downward_speed_mps==0.0f &&
               dive201.evidence.skydive_descent_m==0.0f,
           "vertical motion above 200 m/s restarts wind accumulation quiet");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,90.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation discontinuity=observe(now+=UINT64_C(200000000));
    expect(discontinuity.evidence.skydiving && discontinuity.evidence.skydive_started_ns==now &&
               discontinuity.evidence.skydive_downward_speed_mps==0.0f &&
               discontinuity.evidence.skydive_descent_m==0.0f,
           "the exact 200 ms position discontinuity restarts skydive accumulation quiet");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,0,0.0f,88.0f,0.0f);
    fixture_set_pause(&fixture,PAUSE_CONTROLLER,1);
    fixture_protect(&fixture,0);
    Observation controller_pause=observe(now+=UINT64_C(100000000));
    expect(!controller_pause.evidence.skydiving && controller_pause.evidence.skydive_started_ns==0 &&
               controller_pause.evidence.skydive_downward_speed_mps==0.0f &&
               controller_pause.evidence.skydive_descent_m==0.0f,
           "controller pause immediately clears every continuous skydive field");

    fixture_protect(&fixture,1);
    fixture_clear_pause(&fixture);
    fixture_set_position(&fixture,0,0.0f,86.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation resumed=observe(now+=UINT64_C(100000000));
    expect(resumed.evidence.skydiving && resumed.evidence.skydive_started_ns==now &&
               resumed.evidence.skydive_downward_speed_mps==0.0f &&
               resumed.evidence.skydive_descent_m==0.0f,
           "resuming while still AirDive begins a fresh continuous interval");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x21,0);
    fixture_set_position(&fixture,0,0.0f,71.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation ordinary_fall=observe(now+=UINT64_C(100000000));
    expect(!ordinary_fall.evidence.skydiving && ordinary_fall.evidence.skydive_started_ns==0 &&
               ordinary_fall.evidence.skydive_downward_speed_mps==0.0f &&
               ordinary_fall.evidence.skydive_descent_m==0.0f,
           "a high-speed transition to ordinary Fall ends and cannot activate skydive wind");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x14,0);
    fixture_set_position(&fixture,0,0.0f,82.0f,0.0f);
    fixture_protect(&fixture,0);
    Observation guided=observe(now+=UINT64_C(100000000));
    expect(guided.evidence.skydiving && guided.evidence.skydive_started_ns==now,
           "GuideDive independently begins an authoritative skydive interval");

    fixture_protect(&fixture,1);
    fixture_set_position(&fixture,1,0.0f,40.0f,0.0f);
    fixture_set_action_verb(&fixture,1,0x14,0);
    fixture_select_actor(&fixture,1);
    fixture_protect(&fixture,0);
    Observation replacement=observe(now+=UINT64_C(100000000));
    expect(replacement.evidence.skydiving && replacement.evidence.skydive_started_ns==now &&
               replacement.evidence.skydive_downward_speed_mps==0.0f &&
               replacement.evidence.skydive_descent_m==0.0f,
           "Player actor replacement cannot inherit the prior actor's skydive history");

    fixture_protect(&fixture,1);
    store_pointer(fixture.bsa_components[1],0x1f0,NULL);
    fixture_protect(&fixture,0);
    Observation invalid_action=observe(now+UINT64_C(100000000));
    expect(invalid_action.context.valid && !invalid_action.evidence.skydiving &&
               invalid_action.evidence.skydive_started_ns==0,
           "an invalid main-action pointer clears skydive only while baseline context remains valid");
    fixture_destroy(&fixture);
}

typedef struct {
    const unsigned char *prefix;
    uint64_t now_ns;
    _Atomic int start;
} CueProducerArgs;

static void *cue_producer(void *opaque) {
    CueProducerArgs *args=opaque;
    while(!atomic_load(&args->start)){}
    for(unsigned i=0;i<10000;i++)
        runtime_gameplay_feedback_audio_cue((uint64_t)(uintptr_t)args->prefix,128,
                                            args->now_ns,1.0f);
    return NULL;
}

static void test_exact_extra_audio_gates_queue_and_epochs(void) {
    GameplayFixture fixture=fixture_create();
    fixture_set_position(&fixture,0,0.0f,10.0f,0.0f);
    fixture_clear_pause(&fixture);
    fixture_protect(&fixture,0);
    Module module=fixture_module(&fixture);observer_initialize(&module);
    uint64_t now=UINT64_C(19000000000);unsigned epoch=0;
    RuntimeHapticCueObservation rows[32];
    const unsigned start_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_START,0);
    const unsigned highest_i=extra_fingerprint_highest(0);
    expect(extra_audio_fingerprints[start_i].cue_id>255,
           "the generated title cue exercises a cue ID above the removed fixed dedup bound");
    expect(extra_audio_fingerprints[highest_i].cue_id<sizeof cue_last_ns/sizeof cue_last_ns[0],
           "the highest generated cue ID fits the generated-contract dedup table");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[start_i].prefix,127,now,1.0f);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)fixture.random_audio,128,now,1.0f);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[start_i].prefix,128,now,NAN);
    expect(drain_cues(rows,32,&epoch)==0,
           "short, unknown, and non-finite extra-audio observations fail quiet");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[start_i].prefix,128,now,1.0f);
    expect(drain_cues(rows,32,&epoch)==1 && epoch!=0 &&
               rows[0].category==RUNTIME_HAPTIC_CUE_START && rows[0].event.epoch==epoch &&
               rows[0].event.sequence==1,
           "an exact title cue queues in the active verified image before a Player sample exists");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[highest_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    (void)drain_cues(rows,32,&epoch);

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x28,0);
    fixture_protect(&fixture,0);
    Observation wait_context=observe(now);
    expect(wait_context.evidence.cue_matches[extra_audio_fingerprints[highest_i].category]>=1,
           "the highest generated cue reaches bounded fingerprint diagnostics");
    uint64_t water_matches_before=
        wait_context.evidence.cue_matches[RUNTIME_HAPTIC_CUE_WATER_DASH];
    uint64_t water_rejects_before=
        wait_context.evidence.cue_state_rejects[RUNTIME_HAPTIC_CUE_WATER_DASH];
    const unsigned water_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_WATER_DASH,0);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[water_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==0,
           "a matching water cue in exact Player Wait state is rejected");
    Observation rejected=observe(now+=UINT64_C(20000000));
    expect(rejected.evidence.cue_matches[RUNTIME_HAPTIC_CUE_WATER_DASH]==water_matches_before+1 &&
               rejected.evidence.cue_state_rejects[RUNTIME_HAPTIC_CUE_WATER_DASH]==water_rejects_before+1 &&
               rejected.evidence.last_rejected_cue_id==extra_audio_fingerprints[water_i].cue_id &&
               rejected.evidence.last_rejected_action_verb==0x28,
           "matched-but-rejected diagnostics retain category count, exact cue, and observed action");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x61,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation swimming=observe(now);
    expect(swimming.evidence.swimming,"water-dash cue gate begins from the exact Swim state");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[water_i].prefix,128,
        now+UINT64_C(1000000),0.95f);
    expect(drain_cues(rows,32,&epoch)==1 &&
               rows[0].event.cue_id==extra_audio_fingerprints[water_i].cue_id &&
               rows[0].locality==RUNTIME_HAPTIC_CUE_BOTH && rows[0].pitch==0.95f,
           "an exact authored water-dash prefix preserves timestamp, locality, and actual pitch in Swim");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[water_i].prefix,128,
        now+UINT64_C(50000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==0,"per-cue dedup rejects a repeat 49 ms after admission");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[water_i].prefix,128,
        now+UINT64_C(51000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==1,"per-cue dedup accepts the exact 50 ms boundary");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x46,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(70000000);Observation climbing=observe(now);
    const unsigned climb_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_CLIMB,0);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[climb_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(climbing.evidence.climbing && drain_cues(rows,32,&epoch)==1 &&
               rows[0].category==RUNTIME_HAPTIC_CUE_CLIMB,
           "an exact authored climbing contact queues only with real Climbing state");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x43,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation charging=observe(now);
    const unsigned charge_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_CHARGE,0);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.26f);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.20f);
    expect(drain_cues(rows,32,&epoch)==0,
           "the shared Bow_101 1.26 pitch and an unauthored 1.20 pitch cannot pass the Player charge gate");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.12f);
    expect(charging.evidence.charging && drain_cues(rows,32,&epoch)==1 &&
               rows[0].category==RUNTIME_HAPTIC_CUE_CHARGE && rows[0].pitch==1.12f,
           "actual Charge ActionVerb admits the authored charge cue and preserves its stage pitch");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(150000000),1.12f);
    expect(drain_cues(rows,32,&epoch)==0,"an action gate is stale at the exact 150 ms lease boundary");

    fixture_protect(&fixture,1);
    fixture_set_pause(&fixture,PAUSE_MENU,1);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation menu=observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(menu.context.paused && drain_cues(rows,32,&epoch)==0,
           "menu pause rejects a matching gameplay cue even with a Charge verb");

    fixture_protect(&fixture,1);
    fixture_clear_pause(&fixture);
    store_pointer(fixture.bsa_components[0],0x1f0,NULL);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation invalid_action=observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(invalid_action.context.valid && !invalid_action.evidence.charging &&
               drain_cues(rows,32,&epoch)==0,
           "an invalid current-action pointer rejects charge cues without invalidating landing state");

    fixture_protect(&fixture,1);
    fixture_set_melee_state(&fixture,0,1);
    fixture_set_action_verb(&fixture,0,0x81,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation attacking=observe(now);
    const unsigned swing_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_SWING,0);
    const unsigned thrust_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_THRUST,0);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[swing_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[thrust_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(attacking.evidence.melee_drawn && attacking.evidence.melee_attacking &&
               drain_cues(rows,32,&epoch)==2 &&
               rows[0].category==RUNTIME_HAPTIC_CUE_SWING &&
               rows[1].category==RUNTIME_HAPTIC_CUE_THRUST,
           "exact Player swing and thrust cues require drawn melee in an authored attack action");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x28,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[swing_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==0,
           "drawn melee without an exact Player attack action cannot admit a swing cue");
    Observation swing_rejected=observe(now+=UINT64_C(20000000));
    expect(swing_rejected.evidence.cue_matches[RUNTIME_HAPTIC_CUE_SWING]>=2 &&
               swing_rejected.evidence.cue_state_rejects[RUNTIME_HAPTIC_CUE_SWING]>=1 &&
               swing_rejected.evidence.last_rejected_action_verb==0x28,
           "swing match diagnostics identify action-state rejection without synthesizing an event");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x46,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    const unsigned climb_dash_i=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_CLIMB_DASH,0);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[climb_dash_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==0,
           "a shared climb-jump fingerprint is rejected during ordinary Climb");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x45,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);Observation dash_up=observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[climb_dash_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[climb_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(dash_up.evidence.climb_dashing && drain_cues(rows,32,&epoch)==2 &&
               rows[0].category==RUNTIME_HAPTIC_CUE_CLIMB_DASH &&
               rows[1].category==RUNTIME_HAPTIC_CUE_CLIMB,
           "exact DashUp admits its narrow shared cue and ordinary authored climb contacts");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0,0);
    fixture_set_pause(&fixture,PAUSE_RUMBLE,1);
    fixture_set_event(&fixture,-1,npc_dialogue_flows[0]);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    fixture_protect(&fixture,1);
    fixture_set_event(&fixture,0,npc_dialogue_flows[0]);
    fixture_protect(&fixture,0);
    (void)observe(now+=UINT64_C(20000000));
    Observation dialogue=observe(now+=UINT64_C(20000000));
    const unsigned dialogue_i=extra_fingerprint_highest(RUNTIME_HAPTIC_CUE_DIALOGUE);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[dialogue_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(dialogue.context.paused && dialogue.context.npc_interaction_allowed &&
               drain_cues(rows,32,&epoch)==1 && rows[0].category==RUNTIME_HAPTIC_CUE_DIALOGUE &&
               rows[0].event.cue_id==extra_audio_fingerprints[dialogue_i].cue_id,
           "the highest generated dialogue cue ID is bounded and exact NPC dialogue admits it during Rumble pause");
    fixture_protect(&fixture,1);
    fixture_set_pause(&fixture,PAUSE_MENU,1);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[dialogue_i].prefix,128,
        now+UINT64_C(1000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==0,"menu pause rejects even an exact retained NPC dialogue cue");

    fixture_protect(&fixture,1);
    fixture_clear_pause(&fixture);
    fixture_set_action_verb(&fixture,0,0x43,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(1000000),1.05f);
    runtime_gameplay_feedback_set_active(0);
    expect(drain_cues(rows,32,&epoch)==0 && epoch==0,
           "deactivation clears a queued cue and returns inactive epoch zero");
    runtime_gameplay_feedback_set_active(1);
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[charge_i].prefix,128,
        now+UINT64_C(2000000),1.05f);
    expect(drain_cues(rows,32,&epoch)==0,
           "a fresh epoch cannot reuse the previous epoch's Charge observation before a new sample");
    runtime_gameplay_feedback_audio_cue(
        (uint64_t)(uintptr_t)extra_audio_fingerprints[start_i].prefix,128,
        now+UINT64_C(2000000),1.0f);
    expect(drain_cues(rows,32,&epoch)==1 && rows[0].event.sequence==1 &&
               rows[0].event.epoch==epoch,
           "a trusted title cue remains available and restarts sequencing in the fresh epoch");

    fixture_protect(&fixture,1);
    fixture_set_action_verb(&fixture,0,0x46,0);
    fixture_protect(&fixture,0);
    now+=UINT64_C(20000000);(void)observe(now);
    unsigned submitted[17];
    for(unsigned i=0;i<17;i++){
        unsigned index=extra_fingerprint_index(RUNTIME_HAPTIC_CUE_CLIMB,i);
        submitted[i]=extra_audio_fingerprints[index].cue_id;
        runtime_gameplay_feedback_audio_cue(
            (uint64_t)(uintptr_t)extra_audio_fingerprints[index].prefix,128,
            now+UINT64_C(1000000),1.0f);
    }
    Observation diagnostic=observe(now+=UINT64_C(20000000));
    expect(diagnostic.evidence.cue_queue_drops==1 &&
               diagnostic.evidence.cue_events[RUNTIME_HAPTIC_CUE_CLIMB]>=17 &&
               drain_cues(rows,32,&epoch)==16 && rows[0].event.cue_id==submitted[1],
           "the bounded 16-event queue drops its oldest row and reports category and overflow counters");

    now+=UINT64_C(20000000);(void)observe(now);
    CueProducerArgs args={
        .prefix=extra_audio_fingerprints[climb_i].prefix,
        .now_ns=now+UINT64_C(1000000),.start=0
    };
    pthread_t thread;
    expect(pthread_create(&thread,NULL,cue_producer,&args)==0,
           "concurrent cue producer starts");
    atomic_store(&args.start,1);
    runtime_gameplay_feedback_set_active(0);
    expect(pthread_join(thread,NULL)==0,"concurrent cue producer joins");
    expect(drain_cues(rows,32,&epoch)==0 && epoch==0,
           "deactivation serialized with a concurrent producer leaves no stale queued cue");
    fixture_destroy(&fixture);
}

static void test_observer_never_writes_guest_fixture(void) {
    GameplayFixture fixture = fixture_create();
    fixture_set_position(&fixture, 0, 1.0f, 2.0f, 3.0f);
    fixture_set_surf(&fixture, 0, 0xaea, 1);
    fixture_protect(&fixture, 0);
    const uint64_t before = fixture_digest(&fixture);
    Module module = fixture_module(&fixture);
    observer_initialize(&module);
    Observation observation = observe(UINT64_C(6000000000));
    const uint64_t after = fixture_digest(&fixture);
    expect(observation.context.valid && before == after,
           "observer leaves the entire read-only guest mapping byte-for-byte unchanged");
    fixture_destroy(&fixture);
}

int main(void) {
    test_image_guard();
    test_loading_and_missing_actor_clear_context();
    test_bad_and_unmapped_pointers_fail_quiet();
    test_pause_targets_and_bounds();
    test_equipment_grounding_and_surf();
    test_activation_epochs_and_landing_reset();
    test_landing_lifecycle_and_resets();
    test_surf_flags_speed_and_actor_replacement();
    test_guard_audio_matching_and_lifecycle();
    test_melee_audio_matching_and_lifecycle();
    test_npc_event_sampling_and_allpause();
    test_exact_action_state_observation();
    test_bow_draw_state_lifecycle();
    test_skydive_observation_lifecycle();
    test_exact_extra_audio_gates_queue_and_epochs();
    test_observer_never_writes_guest_fixture();
    if (failures)
        return 1;
    puts("PASS: gameplay feedback read-only observer");
    return 0;
}
