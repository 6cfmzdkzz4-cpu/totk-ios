/* Contextual feedback observes the verified 1.0.0 game at present boundaries.
   It never writes guest memory, calls guest functions, or retains dereferenceable actor pointers across frames. */
#include "gameplay_feedback.h"
#include <TargetConditionals.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#if TARGET_OS_IOS || defined(TOTK_GAMEPLAY_FEEDBACK_TEST)
#include <mach/mach.h>
#include "mach_vm_compat.h"

enum {
    SCENE_SLOT=0x4663f08, PAUSE_SLOT=0x4664058, EVENT_SLOT=0x464a988,
    PLAYER_COMPONENT_SLOT=0x3a8, EXTRA_CUE_QUEUE_CAPACITY=16,
    EXTRA_CUE_DEDUP_NS=50000000, EXTRA_CUE_CONTEXT_NS=150000000
};
static uintptr_t main_base;
static uint64_t last_tick, previous_ns;
static uintptr_t previous_actor;
static float previous_position[3];
static float previous_vertical_speed;
static uint64_t skydive_started_ns;
static float skydive_peak_y;
static int previous_grounded=-1;
static uint64_t airborne_since;
static float airborne_peak_y;
static TOTKSemanticEvents observed_events;
static pthread_mutex_t observation_lock=PTHREAD_MUTEX_INITIALIZER;
static TOTKSemanticContext published_context;
static TOTKSemanticEvents published_events;
static RuntimeGameplayFeedbackEvidence published_evidence;
static RuntimeGameplayFeedbackEvidence evidence;
static _Atomic uint64_t recent_guard_ns,pending_block_ns,last_guard_audio_ns;
static _Atomic uint64_t pending_melee;
static _Atomic int player_melee_drawn;
static _Atomic int audio_context_allowed;
static _Atomic uint64_t read_failure_count;
static _Atomic int observer_active;
static _Atomic unsigned observer_epoch;
static unsigned sampled_epoch;
static unsigned published_epoch;
static _Atomic unsigned audio_context_epoch;
static _Atomic uint64_t audio_context_ns;
static _Atomic unsigned extra_context_epoch;
static _Atomic uint64_t extra_context_ns;
static _Atomic int extra_gameplay_allowed,extra_dialogue_allowed;
static _Atomic int extra_swimming,extra_climbing,extra_climb_dashing,extra_charging;
static _Atomic int extra_melee_drawn,extra_melee_attacking;
static _Atomic unsigned extra_action_verb;
static pthread_mutex_t cue_lock=PTHREAD_MUTEX_INITIALIZER;
static RuntimeHapticCueObservation cue_queue[EXTRA_CUE_QUEUE_CAPACITY];
static unsigned cue_queue_head,cue_queue_count;
static uint64_t cue_sequence;
static uint64_t cue_category_counts[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
static uint64_t cue_category_matches[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
static uint64_t cue_category_state_rejects[RUNTIME_HAPTIC_CUE_CATEGORY_COUNT];
static uint64_t cue_queue_drops;
static unsigned cue_last_id;
static float cue_last_pitch;
static unsigned cue_last_rejected_id,cue_last_rejected_action;
static uintptr_t candidate_event,accepted_npc_event;
static char candidate_name[128],accepted_npc_name[128];
static uint64_t candidate_since;
static int npc_entry_armed;
#if __has_include("gameplay_audio_fingerprints.h")
#include "gameplay_audio_fingerprints.h"
#else
static const struct { const char *name; unsigned char prefix[128]; } guard_audio_fingerprints[1];
#define GUARD_AUDIO_FINGERPRINT_COUNT 0
static const struct { const char *name; unsigned char prefix[128]; int kind; } melee_audio_fingerprints[1];
#define MELEE_AUDIO_FINGERPRINT_COUNT 0
#endif
#if __has_include("gameplay_extra_audio.h")
#include "gameplay_extra_audio.h"
#else
static const struct { unsigned cue_id,category,locality;unsigned char prefix[128]; } extra_audio_fingerprints[1];
#define EXTRA_AUDIO_FINGERPRINT_COUNT 0
#endif
// Generated cue IDs are contiguous and every descriptor has at least one exact fingerprint.
// Size from that verified contract so newly generated high IDs cannot be rejected by an old cap.
static uint64_t cue_last_ns[EXTRA_AUDIO_FINGERPRINT_COUNT+1];
#if __has_include("gameplay_dialogue_flows.h")
#include "gameplay_dialogue_flows.h"
#else
static const char *const npc_dialogue_flows[1];
#define NPC_DIALOGUE_FLOW_COUNT 0
#endif

static int read_memory(uintptr_t address,void *out,size_t size) {
    if(!address || !size || size>16384 || address>UINTPTR_MAX-size)return 0;
    mach_vm_size_t got=0;
    int ok=mach_vm_read_overwrite(mach_task_self(),address,size,(uintptr_t)out,&got)==KERN_SUCCESS && got==size;
    if(!ok)atomic_fetch_add_explicit(&read_failure_count,1,memory_order_relaxed);
    return ok;
}
static uintptr_t pointer_at(uintptr_t base,size_t offset) {
    uintptr_t value=0;
    if(!base || base>UINTPTR_MAX-offset || !read_memory(base+offset,&value,sizeof value) || value%8)return 0;
    return value;
}
static uint32_t u32_at(const unsigned char *bytes,size_t offset) { uint32_t v;memcpy(&v,bytes+offset,4);return v; }
static uintptr_t pointer_in(const unsigned char *bytes,size_t offset) { uintptr_t v;memcpy(&v,bytes+offset,8);return v; }
static int position_valid(const float xyz[3]) {
    return isfinite(xyz[0]) && isfinite(xyz[1]) && isfinite(xyz[2]) &&
        fabsf(xyz[0])<=5500 && xyz[1]>=-1500 && xyz[1]<=3500 && fabsf(xyz[2])<=5000;
}
static uintptr_t equipment_actor_from(uintptr_t player,uintptr_t equipment,unsigned slot,unsigned char actor_header[0x228]) {
    // EquipmentUserRef owns ActorBaseLink slots; copy and validate link identities before using a local actor pointer.
    unsigned char link[0x18],repeat[0x18],data[0x10],confirm[0x10];
    if(!equipment || slot>4 || equipment>UINTPTR_MAX-(0x20+4*0x18) ||
       pointer_at(equipment,0x18)!=player ||
       !read_memory(equipment+0x20+slot*0x18,link,sizeof link))return 0;
    uintptr_t record=pointer_in(link,8);uint32_t id=u32_at(link,0x10);
    if(!record || record>UINTPTR_MAX-0x40 || id==UINT32_MAX ||
       !read_memory(record+0x40,data,sizeof data) || u32_at(data,8)!=id)return 0;
    uintptr_t actor=pointer_in(data,0);
    if(!read_memory(actor,actor_header,0x228))return 0;
    uint32_t state=u32_at(actor_header,0x24);
    if(state>=7 || u32_at(actor_header,0x10)!=id || !read_memory(equipment+0x20+slot*0x18,repeat,sizeof repeat) || memcmp(link,repeat,sizeof link) ||
       !read_memory(record+0x40,confirm,sizeof confirm) || memcmp(data,confirm,sizeof data))return 0;
    return actor;
}
static uintptr_t equipment_actor(uintptr_t player,uintptr_t components,unsigned slot,unsigned char actor_header[0x228]) {
    return equipment_actor_from(player,pointer_at(components,0x230),slot,actor_header);
}
static int dynamic_equipment_state(uintptr_t actor,const unsigned char header[0x228]) {
    unsigned count=u32_at(header,0x218);if(count<=0x41 || count>256)return -1;
    uintptr_t component=pointer_at(pointer_in(header,0x220),0x208);unsigned char state[8];
    if(!component || component>UINTPTR_MAX-0x3d0 || pointer_at(component,0x18)!=actor ||
       !read_memory(component+0x3d0,state,sizeof state))return -1;
    if(!(state[0]&1))return 2;
    uint32_t value=u32_at(state,4);if(value==4)value=2;
    return value<=3?(int)value:-1;
}
static int drawn_melee(uintptr_t player,uintptr_t components) {
    unsigned char header[0x228];uintptr_t actor=equipment_actor(player,components,0,header);
    if(!actor || dynamic_equipment_state(actor,header)!=1)return 0;
    char name[64];if(!read_memory(pointer_in(header,0x210),name,sizeof name) || !memchr(name,0,sizeof name))return 0;
    return !strncmp(name,"Weapon_Sword_",13) || !strncmp(name,"Weapon_Lsword_",14) || !strncmp(name,"Weapon_Spear_",13);
}
static int drawn_charging_bow(uintptr_t player,uintptr_t components) {
    // QueryEquipmentHasBowEquipped at main+0x1f7cfec passes DynamicEquipmentSlot::Bow (3).
    // QueryEquipmentUserIsChargingBow at main+0x1f818b0 defines true as EquipmentUser+0x3ef4 > 0.
    // Treat that field only as the shipped boolean query; no draw fraction is inferred or published.
    uintptr_t equipment=pointer_at(components,0x230);unsigned char header[0x228];
    if(!equipment || equipment>UINTPTR_MAX-0x3ef4)return 0;
    uintptr_t bow=equipment_actor_from(player,equipment,3,header);float charging,repeat;
    if(!bow || dynamic_equipment_state(bow,header)!=1 ||
       !read_memory(equipment+0x3ef4,&charging,sizeof charging) ||
       !read_memory(equipment+0x3ef4,&repeat,sizeof repeat) ||
       memcmp(&charging,&repeat,sizeof charging) || pointer_at(components,0x230)!=equipment ||
       pointer_at(equipment,0x18)!=player)return 0;
    return isfinite(charging) && charging>0;
}
static int equipped_bow_state(uintptr_t player,uintptr_t components) {
    // QueryEquipmentHasBowEquipped at main+0x1f7cfec explicitly passes dynamic slot 3.
    unsigned char header[0x228];uintptr_t bow=equipment_actor(player,components,3,header);
    return bow?dynamic_equipment_state(bow,header):-1;
}
static int equipped_shield_state(uintptr_t player,uintptr_t components) {
    // QueryEquipmentHasShieldEquipped at main+0x1f7d26c explicitly passes dynamic slot 4.
    unsigned char header[0x228];uintptr_t shield=equipment_actor(player,components,4,header);
    char name[64];
    if(!shield || !read_memory(pointer_in(header,0x210),name,sizeof name) || !memchr(name,0,sizeof name) ||
       strncmp(name,"Weapon_Shield_",14))return -1;
    return dynamic_equipment_state(shield,header);
}
static int grounded_state(uintptr_t components,int *grounded) {
    // Exact QueryPhysicsCctIsOnGround path and the selector helper at main+0x7e0268.
    uintptr_t physics=pointer_at(components,0x50),set=pointer_at(physics,0x20),p=pointer_at(set,0x10);
    uintptr_t a=pointer_at(p,8),b=pointer_at(a,0x40);unsigned char header[0x54];
    if(!read_memory(b,header,sizeof header))return 0;
    unsigned selector=u32_at(header,0x50);if(selector>=3)selector=0;
    uintptr_t entry=pointer_in(header,0x38+selector*8);uint32_t state;
    if(!entry || entry>UINTPTR_MAX-8 || !read_memory(entry+8,&state,sizeof state) || state>2)return 0;
    *grounded=state==0;return 1;
}
static int action_verb_at(uintptr_t bsa,size_t slot_offset,unsigned *verb) {
    // SelectorEventQueryCheckPlayerState at main+0x12af610 reads Main at +0x1f0 and Sub at +0x1f8.
    uintptr_t action=pointer_at(bsa,slot_offset);unsigned char snapshot[0x50];
    if(!action || !read_memory(action,snapshot,sizeof snapshot) || pointer_at(bsa,slot_offset)!=action)return 0;
    unsigned value=u32_at(snapshot,0x28);
    if(value==0x11c)value=u32_at(snapshot,0x4c);
    *verb=value;return 1;
}
static int main_action_verb(uintptr_t bsa,unsigned *verb) { return action_verb_at(bsa,0x1f0,verb); }
static int melee_attack_verb(unsigned verb) {
    // Every accepted verb is authored by an exact Player scheduler/action module in Player.pack.
    switch(verb){
    case 0x81: // BasicAttack
    case 0x83: // JumpAttack (the same action module also executes FallAttack)
    case 0x84: // RideJumpAttack
    case 0x87: // DodgeAttack
    case 0x88: // ChargeAttack
    case 0x89: // DashAttack
    case 0x8c: // SneakAttack / Player StealthAttack scheduler
    case 0x90: // JustAvoidAttack
    case 0x94: // SpecialAttack01 / Player SpecialAttack scheduler
    case 0x103: // FallAttack
        return 1;
    default:return 0;
    }
}
static uintptr_t player_actor(unsigned char actor_header[0x228]) {
    uintptr_t module=pointer_at(main_base,SCENE_SLOT),scene=pointer_at(module,0x1e8);
    unsigned char scene_header[0x60],resident_header[0x30];
    if(!read_memory(scene,scene_header,sizeof scene_header)){strcpy(evidence.detail,"SceneModule or current scene unavailable");return 0;}
    uint32_t count=u32_at(scene_header,0x50);
    if(count<14 || count>256){snprintf(evidence.detail,sizeof evidence.detail,"Scene component count outside bounds: %u",count);return 0;}
    uintptr_t resident=pointer_at(pointer_in(scene_header,0x58),13*8);
    if(!read_memory(resident,resident_header,sizeof resident_header)){strcpy(evidence.detail,"Resident component unavailable");return 0;}
    count=u32_at(resident_header,0x20);
    if(!count || count>64){snprintf(evidence.detail,sizeof evidence.detail,"Resident link count outside bounds: %u",count);return 0;}
    unsigned char links[64*0x70];
    if(!read_memory(pointer_in(resident_header,0x28),links,count*0x70)){strcpy(evidence.detail,"Resident links unreadable");return 0;}
    for(uint32_t i=0;i<count;i++){
        uintptr_t data=pointer_in(links,i*0x70+8),actor=pointer_at(data,0x40);
        if(!read_memory(actor,actor_header,0x228))continue;
        char name[7];
        if(read_memory(pointer_in(actor_header,0x210),name,sizeof name) && !memcmp(name,"Player",7))return actor;
    }
    snprintf(evidence.detail,sizeof evidence.detail,"No resident Player among %u links",count);return 0;
}
static int paused_target(uint32_t hash,const uint32_t *hashes,const unsigned char *indices,
                         unsigned capacity,const uint32_t masks[4],int *value) {
    // The game's isTargetPaused returns false for a missing target, including a wrapped full-table miss.
    *value=0;
    unsigned start=hash%capacity;
    for(unsigned step=0;step<capacity;step++){
        unsigned i=(start+step)%capacity;
        if(!hashes[i])return 1;
        if(hashes[i]!=hash)continue;
        unsigned index=indices[i];if(index>=128)return 0;
        *value=(masks[index>>5]>>(index&31))&1;return 1;
    }
    return 1;
}
static int pause_state(int *controller,int *menu,int *rumble,int *hard) {
    uintptr_t manager=pointer_at(main_base,PAUSE_SLOT);
    unsigned char header[0x28],confirm[0x28];
    if(!read_memory(manager,header,sizeof header)){strcpy(evidence.detail,"Pause manager unavailable");return 0;}
    unsigned count=u32_at(header,0x20),capacity=u32_at(header,0x24);
    if(!count || count>capacity || capacity>512){snprintf(evidence.detail,sizeof evidence.detail,"Pause target bounds: count=%u capacity=%u",count,capacity);return 0;}
    uint32_t hashes[512],masks[4]={0};unsigned char indices[512];
    unsigned requests=u32_at(header,0);unsigned char rows[128*0x20],repeat[128*0x20];
    if(requests>128 || (requests && !read_memory(pointer_in(header,8),rows,requests*0x20))){snprintf(evidence.detail,sizeof evidence.detail,"Pause requests outside bounds or unreadable: %u",requests);return 0;}
    if(!read_memory(pointer_in(header,0x10),hashes,capacity*4) ||
       !read_memory(pointer_in(header,0x18),indices,capacity) ||
       !read_memory(manager,confirm,sizeof confirm) || memcmp(header,confirm,sizeof header) ||
       (requests && (!read_memory(pointer_in(header,8),repeat,requests*0x20) || memcmp(rows,repeat,requests*0x20)))){strcpy(evidence.detail,"Pause snapshot inconsistent or unreadable");return 0;}
    // main+0x8d3980 derives the current context by OR-ing each active request; cached context storage is not read.
    for(unsigned i=0;i<requests;i++)if(u32_at(rows+i*0x20,0x1c))
        for(unsigned word=0;word<4;word++)masks[word]|=u32_at(rows+i*0x20,0x0c+word*4);
    int stop=0;
    if(!paused_target(0x53920242,hashes,indices,capacity,masks,controller) ||
       !paused_target(0xe57eb13e,hashes,indices,capacity,masks,menu) ||
       !paused_target(0xb7a94c8f,hashes,indices,capacity,masks,rumble) ||
       !paused_target(0x560a37d9,hashes,indices,capacity,masks,&stop)){strcpy(evidence.detail,"Required pause targets absent or invalid");return 0;}
    *rumble|=stop;*hard=0;
    const uint32_t hard_hashes[]={0x0eafe200,0x956925e5,0xec94b0f4,0xb584333e,0xb5b0a658,0x9310c8ed,0x33d6ecce,0x4a02340c};
    for(unsigned i=0;i<requests;i++)if(u32_at(rows+i*0x20,0x1c))
        for(unsigned j=0;j<sizeof hard_hashes/sizeof hard_hashes[0];j++)
            if(u32_at(rows+i*0x20,8)==hard_hashes[j])*hard=1;
    return 1;
}
static int dialogue_name(const char *name) {
    unsigned lo=0,hi=NPC_DIALOGUE_FLOW_COUNT;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;int order=strcmp(name,npc_dialogue_flows[mid]);
        if(!order)return 1;if(order<0)hi=mid;else lo=mid+1;}
    return 0;
}
static void reset_dialogue(void) {
    candidate_event=accepted_npc_event=0;candidate_since=0;npc_entry_armed=0;
    candidate_name[0]=accepted_npc_name[0]=0;
}
static int event_snapshot(char name[128],uintptr_t *event) {
    // EventMgr FixedSafeString<128>: pointer +0xb0, capacity +0xb8, inline bytes +0xbc.
    uintptr_t manager=pointer_at(main_base,EVENT_SLOT);name[0]=0;*event=0;
    if(!manager)return 1;
    unsigned char before[0x68],after[0x68];char first[128],second[128];
    if(!read_memory(manager+0x58,before,sizeof before))return 0;
    uintptr_t active=pointer_in(before,0);if(!active)return 1;
    uintptr_t text=pointer_in(before,0x58);unsigned capacity=u32_at(before,0x60);
    if(text!=manager+0xbc || capacity!=128 || !read_memory(text,first,sizeof first))return 0;
    size_t length=strnlen(first,sizeof first);if(!length || length>=sizeof first)return 0;
    for(size_t i=0;i<length;i++)if((unsigned char)first[i]<32 || (unsigned char)first[i]>126)return 0;
    if(!read_memory(manager+0x58,after,sizeof after) || pointer_in(after,0)!=active ||
       pointer_in(after,0x58)!=text || u32_at(after,0x60)!=capacity ||
       pointer_at(main_base,EVENT_SLOT)!=manager || !read_memory(text,second,length+1) ||
       memcmp(first,second,length+1))return 0;
    memcpy(name,first,length+1);*event=active;return 1;
}
static void clear_cue_epoch_locked(void) {
    cue_queue_head=cue_queue_count=0;cue_sequence=0;
    memset(cue_queue,0,sizeof cue_queue);memset(cue_last_ns,0,sizeof cue_last_ns);
    cue_last_id=0;cue_last_pitch=0;
}
static void publish_extra_context(unsigned epoch,uint64_t now_ns,int gameplay_allowed,
                                  int dialogue_allowed,int swimming,int climbing,
                                  int climb_dashing,int charging,int melee_drawn,
                                  int melee_attacking,unsigned action_verb) {
    pthread_mutex_lock(&cue_lock);
    if(atomic_load(&observer_active) && atomic_load(&observer_epoch)==epoch){
        atomic_store(&extra_gameplay_allowed,gameplay_allowed);
        atomic_store(&extra_dialogue_allowed,dialogue_allowed);
        atomic_store(&extra_swimming,swimming);
        atomic_store(&extra_climbing,climbing);
        atomic_store(&extra_climb_dashing,climb_dashing);
        atomic_store(&extra_charging,charging);
        atomic_store(&extra_melee_drawn,melee_drawn);
        atomic_store(&extra_melee_attacking,melee_attacking);
        atomic_store(&extra_action_verb,action_verb);
        atomic_store(&extra_context_ns,now_ns);
        atomic_store(&extra_context_epoch,epoch);
    }
    pthread_mutex_unlock(&cue_lock);
}
static int extra_cue_gate_locked(unsigned category,unsigned epoch,uint64_t now_ns) {
    if(!atomic_load(&observer_active) || atomic_load(&observer_epoch)!=epoch ||
       atomic_load(&extra_context_epoch)!=epoch)return category==RUNTIME_HAPTIC_CUE_START;
    uint64_t sampled=atomic_load(&extra_context_ns);
    if(!sampled || now_ns<sampled || now_ns-sampled>=EXTRA_CUE_CONTEXT_NS)
        return category==RUNTIME_HAPTIC_CUE_START;
    switch(category){
    case RUNTIME_HAPTIC_CUE_START:return 1;
    case RUNTIME_HAPTIC_CUE_DIALOGUE:return atomic_load(&extra_dialogue_allowed);
    case RUNTIME_HAPTIC_CUE_WATER_DASH:
        return atomic_load(&extra_gameplay_allowed) && atomic_load(&extra_swimming);
    case RUNTIME_HAPTIC_CUE_CLIMB:
        return atomic_load(&extra_gameplay_allowed) &&
            (atomic_load(&extra_climbing) || atomic_load(&extra_climb_dashing));
    case RUNTIME_HAPTIC_CUE_CHARGE:
        return atomic_load(&extra_gameplay_allowed) && atomic_load(&extra_charging);
    case RUNTIME_HAPTIC_CUE_SWING:
    case RUNTIME_HAPTIC_CUE_THRUST:
        return atomic_load(&extra_gameplay_allowed) && atomic_load(&extra_melee_drawn) &&
            atomic_load(&extra_melee_attacking) && !atomic_load(&extra_charging);
    case RUNTIME_HAPTIC_CUE_CLIMB_DASH:
        return atomic_load(&extra_gameplay_allowed) && atomic_load(&extra_climb_dashing);
    default:return 0;
    }
}
static int charge_pitch_valid(float pitch) {
    // Player ChargeLv1/Lv2/Lv3 bind this cue at 1.00/1.05/1.12; Bow_101 uses the same BWAV at 1.26.
    return fabsf(pitch-1.00f)<=0.003f || fabsf(pitch-1.05f)<=0.003f ||
        fabsf(pitch-1.12f)<=0.003f;
}
void runtime_gameplay_feedback_audio_cue(uint64_t address,uint64_t size,uint64_t now_ns,float pitch) {
    const unsigned epoch=atomic_load(&observer_epoch);
    if(!main_base || !atomic_load(&observer_active) || !address || size<128 || !now_ns ||
       !isfinite(pitch) || pitch<=0)return;
    unsigned char prefix[128];
    if(!read_memory(address,prefix,sizeof prefix))return;
    for(unsigned i=0;i<EXTRA_AUDIO_FINGERPRINT_COUNT;i++){
        if(memcmp(prefix,extra_audio_fingerprints[i].prefix,sizeof prefix))continue;
        const unsigned cue_id=extra_audio_fingerprints[i].cue_id;
        const unsigned category=extra_audio_fingerprints[i].category;
        const unsigned locality=extra_audio_fingerprints[i].locality;
        if(!cue_id || cue_id>=sizeof cue_last_ns/sizeof cue_last_ns[0] ||
           category<RUNTIME_HAPTIC_CUE_START || category>=RUNTIME_HAPTIC_CUE_CATEGORY_COUNT ||
           locality<RUNTIME_HAPTIC_CUE_LEFT || locality>RUNTIME_HAPTIC_CUE_BOTH)return;
        pthread_mutex_lock(&cue_lock);
        int current=atomic_load(&observer_active) && atomic_load(&observer_epoch)==epoch;
        if(!current){pthread_mutex_unlock(&cue_lock);return;}
        cue_category_matches[category]++;
        if(category==RUNTIME_HAPTIC_CUE_CHARGE && !charge_pitch_valid(pitch)){
            pthread_mutex_unlock(&cue_lock);return;
        }
        uint64_t previous=cue_last_ns[cue_id];
        int state_allowed=extra_cue_gate_locked(category,epoch,now_ns);
        int accept=state_allowed &&
            (!previous || (now_ns>previous && now_ns-previous>=EXTRA_CUE_DEDUP_NS));
        if(accept){
            cue_last_ns[cue_id]=now_ns;
            RuntimeHapticCueObservation observation={
                .event={.cue_id=cue_id,.epoch=epoch,.sequence=++cue_sequence,.timestamp_ns=now_ns},
                .pitch=pitch,.category=category,.locality=locality
            };
            if(cue_queue_count==EXTRA_CUE_QUEUE_CAPACITY){
                cue_queue_head=(cue_queue_head+1)%EXTRA_CUE_QUEUE_CAPACITY;
                cue_queue_count--;cue_queue_drops++;
            }
            unsigned tail=(cue_queue_head+cue_queue_count)%EXTRA_CUE_QUEUE_CAPACITY;
            cue_queue[tail]=observation;cue_queue_count++;
            cue_category_counts[category]++;cue_last_id=cue_id;cue_last_pitch=pitch;
        }else if(!state_allowed){
            cue_category_state_rejects[category]++;
            cue_last_rejected_id=cue_id;
            cue_last_rejected_action=atomic_load(&extra_action_verb);
        }
        pthread_mutex_unlock(&cue_lock);
        return;
    }
}
unsigned runtime_gameplay_feedback_cues(RuntimeHapticCueObservation *out,unsigned capacity,unsigned *epoch) {
    pthread_mutex_lock(&cue_lock);
    unsigned active_epoch=atomic_load(&observer_active)?atomic_load(&observer_epoch):0;
    if(epoch)*epoch=active_epoch;
    unsigned count=out&&capacity?cue_queue_count<capacity?cue_queue_count:capacity:0;
    for(unsigned i=0;i<count;i++){
        out[i]=cue_queue[cue_queue_head];
        cue_queue_head=(cue_queue_head+1)%EXTRA_CUE_QUEUE_CAPACITY;cue_queue_count--;
    }
    pthread_mutex_unlock(&cue_lock);
    return count;
}
void runtime_gameplay_feedback_audio(uint64_t address,uint64_t size,uint64_t now_ns) {
    // Only an actual, newly played authored guard cue qualifies; raw volume and unrelated rumble do not.
    unsigned epoch=atomic_load(&observer_epoch);
    uint64_t guard=atomic_load(&recent_guard_ns),last=atomic_load(&last_guard_audio_ns);
    uint64_t context_ns=atomic_load(&audio_context_ns);
    if(!atomic_load(&observer_active) || atomic_load(&audio_context_epoch)!=epoch ||
       !atomic_load(&audio_context_allowed) || !context_ns || now_ns<context_ns || now_ns-context_ns>=150000000ull ||
       !now_ns || size<128 || !address)return;
    unsigned count=guard && now_ns>=guard && now_ns-guard<150000000ull &&
        (!last || (now_ns>last && now_ns-last>=120000000ull))?GUARD_AUDIO_FINGERPRINT_COUNT:0;
    if(!count && !atomic_load(&player_melee_drawn))return;
    unsigned char prefix[128];
    if(!read_memory(address,prefix,sizeof prefix))return;
    for(unsigned i=0;i<count;i++){
        if(memcmp(prefix,guard_audio_fingerprints[i].prefix,sizeof prefix))continue;
        if(atomic_load(&observer_active) && atomic_load(&observer_epoch)==epoch &&
           atomic_load(&audio_context_epoch)==epoch && atomic_load(&audio_context_allowed) &&
           atomic_load(&recent_guard_ns)>=guard && atomic_compare_exchange_strong(&last_guard_audio_ns,&last,now_ns))
            atomic_store(&pending_block_ns,now_ns);
        return;
    }
    if(!atomic_load(&player_melee_drawn))return;
    for(unsigned i=0;i<MELEE_AUDIO_FINGERPRINT_COUNT;i++){
        if(memcmp(prefix,melee_audio_fingerprints[i].prefix,sizeof prefix))continue;
        if(atomic_load(&observer_active) && atomic_load(&observer_epoch)==epoch &&
           atomic_load(&audio_context_epoch)==epoch && atomic_load(&audio_context_allowed) && atomic_load(&player_melee_drawn)){
            uint64_t packed=(now_ns&~UINT64_C(3))|(unsigned)melee_audio_fingerprints[i].kind;
            uint64_t previous=atomic_load(&pending_melee);
            while((previous&~UINT64_C(3))<(packed&~UINT64_C(3)) &&
                  !atomic_compare_exchange_weak(&pending_melee,&previous,packed)){}
        }
        return;
    }
}
void runtime_gameplay_feedback_set_active(int active) {
    pthread_mutex_lock(&cue_lock);
    atomic_store(&observer_active,active!=0);atomic_fetch_add(&observer_epoch,1);
    atomic_store(&extra_context_epoch,0);atomic_store(&extra_context_ns,0);
    atomic_store(&extra_gameplay_allowed,0);atomic_store(&extra_dialogue_allowed,0);
    atomic_store(&extra_swimming,0);atomic_store(&extra_climbing,0);atomic_store(&extra_climb_dashing,0);
    atomic_store(&extra_charging,0);atomic_store(&extra_melee_drawn,0);atomic_store(&extra_melee_attacking,0);
    atomic_store(&extra_action_verb,0);clear_cue_epoch_locked();
    pthread_mutex_unlock(&cue_lock);
    atomic_store(&audio_context_allowed,0);atomic_store(&recent_guard_ns,0);atomic_store(&pending_block_ns,0);
    atomic_store(&pending_melee,0);atomic_store(&player_melee_drawn,0);
    atomic_store(&audio_context_ns,0);
}
void runtime_gameplay_feedback_init(const Module *module) {
    // The loader has already verified the embedded text/data digests. This locks every observation offset to that image.
    main_base=0;last_tick=previous_ns=0;previous_actor=0;skydive_started_ns=0;skydive_peak_y=0;
    previous_grounded=-1;airborne_since=0;airborne_peak_y=0;previous_vertical_speed=0;
    reset_dialogue();
    observed_events=(TOTKSemanticEvents){0};
    atomic_store(&recent_guard_ns,0);atomic_store(&pending_block_ns,0);
    atomic_store(&last_guard_audio_ns,0);atomic_store(&audio_context_allowed,0);
    atomic_store(&pending_melee,0);atomic_store(&player_melee_drawn,0);
    atomic_store(&audio_context_ns,0);
    atomic_store(&read_failure_count,0);
    atomic_store(&observer_active,0);atomic_store(&observer_epoch,0);sampled_epoch=published_epoch=0;
    atomic_store(&audio_context_epoch,0);
    pthread_mutex_lock(&cue_lock);
    atomic_store(&extra_context_epoch,0);atomic_store(&extra_context_ns,0);
    atomic_store(&extra_gameplay_allowed,0);atomic_store(&extra_dialogue_allowed,0);
    atomic_store(&extra_swimming,0);atomic_store(&extra_climbing,0);atomic_store(&extra_climb_dashing,0);
    atomic_store(&extra_charging,0);atomic_store(&extra_melee_drawn,0);atomic_store(&extra_melee_attacking,0);
    atomic_store(&extra_action_verb,0);clear_cue_epoch_locked();
    memset(cue_category_counts,0,sizeof cue_category_counts);
    memset(cue_category_matches,0,sizeof cue_category_matches);
    memset(cue_category_state_rejects,0,sizeof cue_category_state_rejects);
    cue_queue_drops=0;cue_last_rejected_id=cue_last_rejected_action=0;
    pthread_mutex_unlock(&cue_lock);
    memset(&evidence,0,sizeof evidence);
    evidence.guard_cue_fingerprints=GUARD_AUDIO_FINGERPRINT_COUNT;
    evidence.melee_cue_fingerprints=MELEE_AUDIO_FINGERPRINT_COUNT;
    evidence.npc_event_ids=NPC_DIALOGUE_FLOW_COUNT;
    pthread_mutex_lock(&observation_lock);
    published_context=(TOTKSemanticContext){0};published_events=(TOTKSemanticEvents){0};
    published_evidence=(RuntimeGameplayFeedbackEvidence){0};
    pthread_mutex_unlock(&observation_lock);
    if(!module || !module->name || !module->base || strcmp(module->name,"main") || !module->digest ||
       strcmp(module->digest,"5553c4124bfff15094d4def00894ddccc3a48ee9a2cd4cea06ca8f8c48359ccc") ||
       module->size<PAUSE_SLOT+8)return;
    main_base=(uintptr_t)module->base;
    fprintf(stderr,"[gameplay-feedback] read-only 1.0.0 observer enabled for verified main image\n");
}
void runtime_gameplay_feedback_tick(uint64_t now_ns) {
    if(!main_base || !atomic_load(&observer_active) || !now_ns)return;
    unsigned epoch=atomic_load(&observer_epoch);
    if(epoch!=sampled_epoch){
        sampled_epoch=epoch;last_tick=previous_ns=0;previous_actor=0;previous_grounded=-1;airborne_since=0;airborne_peak_y=0;
        skydive_started_ns=0;skydive_peak_y=0;
        observed_events=(TOTKSemanticEvents){0};
        reset_dialogue();
    }
    if(last_tick && (now_ns<=last_tick || now_ns-last_tick<15000000ull))return;
    last_tick=now_ns;evidence.frames++;
    TOTKSemanticContext context={.timestamp_ns=now_ns};
    evidence.detail[0]=0;
    unsigned char actor_header[0x228],pc[0x1430],bsa[0x28];float position[3];
    uintptr_t actor=player_actor(actor_header);
    unsigned components=actor?u32_at(actor_header,0x218):0;
    uintptr_t component_array=components>0x75 && components<=256?pointer_in(actor_header,0x220):0;
    uintptr_t component=pointer_at(component_array,PLAYER_COMPONENT_SLOT);
    uintptr_t bsa_component=pointer_at(component_array,0x158);
    int controller_paused=1,menu_paused=1,rumble_paused=1,hard_paused=1;
    int grounded=0;
    int action_valid=0,sub_action_valid=0,swimming=0,climbing=0,climb_dashing=0,charging=0;
    int melee_attacking=0,skydiving=0;
    unsigned action_verb=0,sub_action_verb=0;
    int good=actor!=0;
    if(good && !component_array){snprintf(evidence.detail,sizeof evidence.detail,"Actor component count outside bounds: %u",components);good=0;}
    if(good && (!read_memory(actor+0x2ac,position,sizeof position) || !position_valid(position))){strcpy(evidence.detail,"Player position unreadable or outside world bounds");good=0;}
    if(good && (!read_memory(component,pc,sizeof pc) || pointer_in(pc,0x18)!=actor)){strcpy(evidence.detail,"PlayerComponent missing or owner mismatch");good=0;}
    if(good && (!read_memory(bsa_component,bsa,sizeof bsa) || pointer_in(bsa,0x18)!=actor)){
        strcpy(evidence.detail,"BSARef missing or owner mismatch");good=0;
    }
    if(good){
        action_valid=main_action_verb(bsa_component,&action_verb);
        sub_action_valid=action_verb_at(bsa_component,0x1f8,&sub_action_verb);
    }
    if(action_valid){
        // These exactly mirror CheckPlayerState Swim/Climbing plus the authored Charge scheduler verb.
        swimming=action_verb==0x61 || (pc[0x1424]&0x10)!=0;
        climbing=action_verb==0x46 || (pc[0x1425]&0x02)!=0;
        climb_dashing=action_verb==0x45;
        charging=action_verb==0x43;
        melee_attacking=melee_attack_verb(action_verb);
        skydiving=action_verb==0x13 || action_verb==0x14;
    }
    if(good && !grounded_state(component_array,&grounded)){strcpy(evidence.detail,"CCT grounded state unavailable");good=0;}
    if(good && !pause_state(&controller_paused,&menu_paused,&rumble_paused,&hard_paused))good=0;
    if(!good){
        previous_actor=0;previous_ns=0;previous_grounded=-1;airborne_since=0;airborne_peak_y=0;
        skydive_started_ns=0;skydive_peak_y=0;
        atomic_store(&audio_context_allowed,0);atomic_store(&recent_guard_ns,0);
        atomic_store(&pending_block_ns,0);atomic_store(&pending_melee,0);atomic_store(&player_melee_drawn,0);
        atomic_store(&audio_context_ns,0);
        evidence.melee_drawn=evidence.bow_drawn=evidence.bow_ready=0;
        evidence.swimming=evidence.climbing=evidence.climb_dashing=evidence.charging=0;
        evidence.melee_attacking=evidence.skydiving=0;evidence.action_verb=evidence.sub_action_verb=0;
        evidence.skydive_started_ns=0;evidence.skydive_downward_speed_mps=0;
        evidence.skydive_descent_m=0;
        evidence.grounded=evidence.shield_attached=0;
        evidence.controller_paused=evidence.menu_paused=evidence.rumble_paused=1;
        evidence.hard_paused=1;evidence.event_active=0;evidence.event_name[0]=0;reset_dialogue();
        strcpy(evidence.action,"unavailable");
        if(!evidence.detail[0])strcpy(evidence.detail,"No consistent player context; semantic output is quiet");
    }else{
        evidence.player_frames++;context.valid=1;context.paused=menu_paused || rumble_paused || hard_paused;
        evidence.swimming=swimming;evidence.climbing=climbing;evidence.climb_dashing=climb_dashing;
        evidence.charging=charging;evidence.melee_attacking=melee_attacking;
        evidence.action_verb=action_valid?action_verb:0;
        evidence.sub_action_verb=sub_action_valid?sub_action_verb:0;
        float vertical_speed=0;
        int continuous=previous_actor==actor && previous_ns && now_ns>previous_ns && now_ns-previous_ns<200000000ull;
        int wind_continuous=continuous;
        if(continuous){
            double dt=(now_ns-previous_ns)*1e-9;
            double speed=hypot((double)position[0]-previous_position[0],(double)position[2]-previous_position[2])/dt;
            if(isfinite(speed) && speed<80)context.speed_mps=(float)speed;else continuous=wind_continuous=0;
            vertical_speed=(float)((position[1]-previous_position[1])/dt);
            // No dive terminal speed was established from the inspected pack/code. This higher wind-only
            // ceiling rejects malformed samples without imposing the landing observer's existing 80 m/s limit.
            if(!isfinite(vertical_speed) || fabsf(vertical_speed)>200)wind_continuous=0;
            if(!isfinite(vertical_speed) || fabsf(vertical_speed)>80)continuous=0;
        }
        if(!continuous || controller_paused || context.paused){previous_grounded=-1;airborne_since=0;airborne_peak_y=0;}
        if(!skydiving || !wind_continuous || controller_paused || context.paused){
            skydive_started_ns=0;skydive_peak_y=0;
        }
        if(skydiving && !controller_paused && !context.paused){
            int entering=!skydive_started_ns;
            if(entering){skydive_started_ns=now_ns;skydive_peak_y=position[1];}
            else skydive_peak_y=fmaxf(skydive_peak_y,position[1]);
            evidence.skydiving=1;evidence.skydive_started_ns=skydive_started_ns;
            // The transition frame includes movement observed before exact skydive state; do not count it.
            evidence.skydive_downward_speed_mps=entering?0:fmaxf(0,-vertical_speed);
            evidence.skydive_descent_m=fmaxf(0,skydive_peak_y-position[1]);
        }else{
            evidence.skydiving=0;evidence.skydive_started_ns=0;
            evidence.skydive_downward_speed_mps=0;evidence.skydive_descent_m=0;
        }
        if(!continuous){
            atomic_store(&recent_guard_ns,0);atomic_store(&pending_block_ns,0);atomic_store(&pending_melee,0);
            observed_events.melee_swing_timestamp_ns=0;observed_events.melee_kind=0;reset_dialogue();
        }
        if(!controller_paused && !context.paused){
            if(!grounded && previous_grounded==1){
                airborne_since=now_ns;airborne_peak_y=fmaxf(previous_position[1],position[1]);
            }
            if(!grounded && airborne_since)airborne_peak_y=fmaxf(airborne_peak_y,position[1]);
            // Small terrain steps are quiet: require substantial observed descent as well as airtime and impact speed.
            if(grounded && previous_grounded==0 && airborne_since && now_ns-airborne_since>=150000000ull &&
               airborne_peak_y-position[1]>=0.60f && previous_vertical_speed< -2.0f){
                observed_events.landing_timestamp_ns=now_ns;evidence.landing_events++;
            }
            previous_grounded=grounded;
            if(grounded){airborne_since=0;airborne_peak_y=0;}
        }
        previous_actor=actor;previous_ns=now_ns;memcpy(previous_position,position,sizeof position);
        previous_vertical_speed=vertical_speed;
        int surfing=((pc[0xaea]|pc[0xae2])&8)!=0;
        context.shield_surfing=!controller_paused && grounded && surfing;
        int shield_state=equipped_shield_state(actor,component_array);
        evidence.shield_attached=shield_state==1 || shield_state==2;
        context.shield_ready=!controller_paused && !context.paused && !surfing && evidence.shield_attached;
        context.shield_raised=context.shield_ready && shield_state==1 && (bsa[0x24]&0x20)!=0;
        atomic_store(&audio_context_epoch,epoch);
        atomic_store(&audio_context_ns,now_ns);
        atomic_store(&audio_context_allowed,!controller_paused && !context.paused);
        evidence.melee_drawn=!controller_paused && !context.paused && drawn_melee(actor,component_array);
        int bow_state=equipped_bow_state(actor,component_array);
        context.bow_ready=!controller_paused && !context.paused && (bow_state==1 || bow_state==2);
        context.bow_drawn=!controller_paused && !context.paused && sub_action_valid &&
            sub_action_verb==0x3b && drawn_charging_bow(actor,component_array);
        evidence.bow_drawn=context.bow_drawn;evidence.bow_ready=context.bow_ready;
        atomic_store(&player_melee_drawn,evidence.melee_drawn);
        if(!evidence.melee_drawn){observed_events.melee_swing_timestamp_ns=0;observed_events.melee_kind=0;}
        if(context.shield_raised)atomic_store(&recent_guard_ns,now_ns);
        uint64_t block=atomic_exchange(&pending_block_ns,0);
        if(block && block<=now_ns && now_ns-block<100000000ull && !controller_paused && !context.paused){
            observed_events.shield_block_timestamp_ns=block;evidence.block_events++;
        }else if(block>now_ns && !controller_paused && !context.paused){
            uint64_t empty=0;atomic_compare_exchange_strong(&pending_block_ns,&empty,block);
        }
        uint64_t melee=atomic_exchange(&pending_melee,0),swing=melee&~UINT64_C(3);
        if(swing && swing<=now_ns && now_ns-swing<350000000ull && evidence.melee_drawn){
            observed_events.melee_swing_timestamp_ns=swing;observed_events.melee_kind=(int)(melee&3);evidence.melee_events++;
        }else if(swing>now_ns && evidence.melee_drawn){uint64_t empty=0;atomic_compare_exchange_strong(&pending_melee,&empty,melee);}
        evidence.grounded=grounded;evidence.controller_paused=controller_paused;
        evidence.menu_paused=menu_paused;evidence.rumble_paused=rumble_paused;
        evidence.hard_paused=hard_paused;
        uintptr_t active_event=0;
        int event_ok=event_snapshot(evidence.event_name,&active_event);
        evidence.event_active=active_event!=0;
        if(!event_ok || menu_paused || hard_paused){reset_dialogue();}
        else if(!active_event){reset_dialogue();npc_entry_armed=1;}
        else{
            if(candidate_event!=active_event || strcmp(candidate_name,evidence.event_name)){
                candidate_event=active_event;strcpy(candidate_name,evidence.event_name);candidate_since=now_ns;
            }else if(now_ns-candidate_since>=15000000ull){
                int npc=dialogue_name(candidate_name);
                context.npc_interaction_allowed=npc;
                if(npc){
                    if(accepted_npc_event!=active_event || strcmp(accepted_npc_name,candidate_name)){
                        if(npc_entry_armed){observed_events.npc_interaction_timestamp_ns=now_ns;evidence.npc_events++;}
                        accepted_npc_event=active_event;strcpy(accepted_npc_name,candidate_name);npc_entry_armed=1;
                    }
                }else{accepted_npc_event=0;accepted_npc_name[0]=0;npc_entry_armed=1;}
            }
        }
        strcpy(evidence.action,context.shield_surfing?"ShieldSurfing":context.shield_raised?"GuardRequested":context.shield_ready?"ShieldAttached":"Player");
        snprintf(evidence.detail,sizeof evidence.detail,"controllerPaused=%d menuPaused=%d rumblePaused=%d",
                 controller_paused,menu_paused,rumble_paused);
    }
    publish_extra_context(epoch,now_ns,good && !controller_paused && !context.paused,
                          good && !menu_paused && !hard_paused && context.npc_interaction_allowed,
                          swimming,climbing,climb_dashing,charging,evidence.melee_drawn,
                          melee_attacking,action_valid?action_verb:0);
    pthread_mutex_lock(&cue_lock);
    memcpy(evidence.cue_events,cue_category_counts,sizeof evidence.cue_events);
    memcpy(evidence.cue_matches,cue_category_matches,sizeof evidence.cue_matches);
    memcpy(evidence.cue_state_rejects,cue_category_state_rejects,sizeof evidence.cue_state_rejects);
    evidence.cue_queue_drops=cue_queue_drops;evidence.last_cue_id=cue_last_id;
    evidence.last_cue_pitch=cue_last_pitch;
    evidence.last_rejected_cue_id=cue_last_rejected_id;
    evidence.last_rejected_action_verb=cue_last_rejected_action;
    pthread_mutex_unlock(&cue_lock);
    evidence.read_failures=atomic_load_explicit(&read_failure_count,memory_order_relaxed);
    pthread_mutex_lock(&observation_lock);
    published_context=context;published_events=observed_events;published_evidence=evidence;published_epoch=epoch;
    pthread_mutex_unlock(&observation_lock);
}
void runtime_gameplay_feedback_read(TOTKSemanticContext *context,TOTKSemanticEvents *events,
                                   RuntimeGameplayFeedbackEvidence *out_evidence) {
    pthread_mutex_lock(&observation_lock);
    if(context)*context=published_context;
    if(context && (!atomic_load(&observer_active) || published_epoch!=atomic_load(&observer_epoch)))
        *context=(TOTKSemanticContext){0};
    if(events)*events=published_events;
    if(out_evidence)*out_evidence=published_evidence;
    pthread_mutex_unlock(&observation_lock);
}
#else
void runtime_gameplay_feedback_init(const Module *module) { (void)module; }
void runtime_gameplay_feedback_set_active(int active) { (void)active; }
void runtime_gameplay_feedback_tick(uint64_t now_ns) { (void)now_ns; }
void runtime_gameplay_feedback_audio(uint64_t address,uint64_t size,uint64_t now_ns) { (void)address;(void)size;(void)now_ns; }
void runtime_gameplay_feedback_audio_cue(uint64_t address,uint64_t size,uint64_t now_ns,float pitch) {
    (void)address;(void)size;(void)now_ns;(void)pitch;
}
unsigned runtime_gameplay_feedback_cues(RuntimeHapticCueObservation *out,unsigned capacity,unsigned *epoch) {
    (void)out;(void)capacity;if(epoch)*epoch=0;return 0;
}
void runtime_gameplay_feedback_read(TOTKSemanticContext *context,TOTKSemanticEvents *events,
                                   RuntimeGameplayFeedbackEvidence *evidence) {
    if(context)*context=(TOTKSemanticContext){0};
    if(events)*events=(TOTKSemanticEvents){0};
    if(evidence)*evidence=(RuntimeGameplayFeedbackEvidence){0};
}
#endif
