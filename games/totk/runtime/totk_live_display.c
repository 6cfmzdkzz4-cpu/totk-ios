/* TOTK 1.0.0 nn::ui2d Calculate ownership survey. This opt-in observer uses
   verified writable vtable slots and delegates the original calculation intact. */
#include "totk_live_display.h"
#include "totk_live_reservations.h"
#include <mach/mach.h>
#include "mach_vm_compat.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <CommonCrypto/CommonDigest.h>
#ifndef TOTK_LIVE_DISPLAY_NARROW
#define TOTK_LIVE_DISPLAY_NARROW (!TARGET_OS_IPHONE)
#endif
#if TARGET_OS_IPHONE && TOTK_LIVE_DISPLAY_NARROW && !__has_include("totk_live_layouts.h")
#error "Generate the private verified live-layout header before building the narrow iOS profile"
#endif
#if TOTK_LIVE_DISPLAY_NARROW && __has_include("totk_live_layouts.h")
#include "totk_live_layouts.h"
#define LIVE_METADATA 1
#else
#define LIVE_METADATA 0
#endif

enum { LIVE_CALCULATE=0x40e94, LIVE_ROOT=0x18, LIVE_PANE_BYTES=0xd8,
       LIVE_MAX_PANES=512, LIVE_MAX_DEPTH=48 };
typedef void (*Calculate)(uintptr_t,uintptr_t,uint32_t);
typedef void (*PaneCalculate)(uintptr_t,uintptr_t,uintptr_t,uint32_t);
typedef uint64_t (*Build)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef void (*Destroy)(uintptr_t);
typedef struct {
    uintptr_t pointer;
    unsigned parent,depth;
    const char *kind;
    unsigned char bytes[LIVE_PANE_BYTES];
} LivePane;
static pthread_mutex_t geometry_lock=PTHREAD_MUTEX_INITIALIZER;
static TotkLiveSource active_source={.aspect=4.0/3.0,.width=4,.height=3,.scale=1,.valid=1};
static TotkLiveSource requested_source;
static int requested_fallback;
static _Atomic int paired_enabled;
static _Atomic uint64_t adjusted_panes,missed_panes;
extern void runtime_aspect_publish_live(float,float) __attribute__((weak_import));
extern void mtl_present_geometry_changed(void) __attribute__((weak_import));
int runtime_live_display_enabled(void) {return atomic_load_explicit(&paired_enabled,memory_order_acquire);}
int runtime_live_display_request(double width,double height,double scale,const double insets[4]) {
    return runtime_live_display_request_scene(width,height,scale,insets,0);
}
int runtime_live_display_request_scene(double width,double height,double scale,const double insets[4],unsigned orientation) {
    return runtime_live_display_request_scene_mode(width,height,scale,insets,orientation,0);
}
int runtime_live_display_request_scene_mode(double width,double height,double scale,const double insets[4],unsigned orientation,int fallback) {
    if(orientation>4)return 0;
    if(!isfinite(width) || !isfinite(height) || !isfinite(scale) || width<1 || height<1 || scale<=0 || scale>8 || !insets)return 0;
    double aspect=width/height;
    if(width>16384 || height>16384 || aspect<.05 || aspect>20 || (!fallback && (aspect<.5 || aspect>16.0/9.0)))return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(insets[i]) || insets[i]<0)return 0;
    if(insets[1]+insets[3]>=width || insets[0]+insets[2]>=height)return 0;
    pthread_mutex_lock(&geometry_lock);
    if(requested_source.width!=width || requested_source.height!=height || requested_source.scale!=scale || requested_source.orientation!=(int)orientation || requested_fallback!=(fallback!=0) || memcmp(requested_source.safe_insets,insets,4*sizeof(double))) {
        requested_fallback=fallback!=0;
        requested_source.width=width;requested_source.height=height;requested_source.scale=scale;requested_source.aspect=fallback?4.0/3.0:aspect;requested_source.orientation=(int)orientation;
        memcpy(requested_source.safe_insets,insets,4*sizeof(double));requested_source.generation++;
    }
    pthread_mutex_unlock(&geometry_lock);return 1;
}
TotkLiveSource runtime_live_display_finish_interval(uint64_t frame) {
    pthread_mutex_lock(&geometry_lock);
    TotkLiveSource completed=active_source;completed.frame=frame;
    completed.hud_adjustments=atomic_exchange_explicit(&adjusted_panes,0,memory_order_acq_rel);
    completed.hud_misses=atomic_exchange_explicit(&missed_panes,0,memory_order_acq_rel);
    int enabled=runtime_live_display_enabled();
    if(!enabled) {
        float legacy=runtime_aspect_now();completed.aspect=legacy>0?legacy:16.0/9.0;
        completed.paired=0;completed.valid=1;pthread_mutex_unlock(&geometry_lock);return completed;
    }
    completed.valid=fabs(completed.aspect-4.0/3.0)<1e-7 || !completed.hud_misses;
    completed.paired=completed.requested_generation && !completed.fallback && !completed.hud_misses;
    TotkLiveSource next=requested_source.generation?requested_source:active_source;
    next.requested_generation=requested_source.generation;
    next.fallback=requested_fallback || completed.hud_misses!=0;
    if(next.fallback)next.aspect=4.0/3.0;
    int changed=next.aspect!=active_source.aspect || next.width!=active_source.width || next.height!=active_source.height ||
                next.scale!=active_source.scale || next.orientation!=active_source.orientation || next.fallback!=active_source.fallback ||
                next.requested_generation!=active_source.requested_generation ||
                memcmp(next.safe_insets,active_source.safe_insets,sizeof next.safe_insets);
    if(changed) {
        if(!runtime_aspect_publish_live)runtime_fail("live display: camera publisher unavailable");
        runtime_aspect_publish_live((float)next.aspect,(float)((16.0/9.0)/next.aspect));
        next.generation=active_source.generation+1;next.valid=1;active_source=next;
        if(mtl_present_geometry_changed)mtl_present_geometry_changed();
    }
    pthread_mutex_unlock(&geometry_lock);return completed;
}
static uintptr_t live_base;
static size_t live_size,live_text_end,live_data_start;
static Calculate calculate_original;
static Calculate calculate_eui_original;
static PaneCalculate pane_calculate_original;
static Build build_base_original,build_ex_original,build_eui_original;
static Destroy destroy_original,delete_base_original,delete_ex_original,delete_eui_original;
static FILE *audit_file;
static TotkLiveReservations reservations=TOTK_LIVE_RESERVATIONS_INITIALIZER;
static pthread_mutex_t audit_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned calculate_depth;
static _Thread_local unsigned pane_calculate_depth;
typedef struct {uintptr_t layout,root;char name[96];unsigned qualified;} LiveOwner;
static LiveOwner owners[512];
static pthread_mutex_t owner_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned build_reports;
static _Atomic uint64_t pane_calls;
static pthread_mutex_t type_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {uintptr_t table;const char *kind;} type_cache[128];
static unsigned type_count;
static _Thread_local unsigned querying_type;
uint64_t runtime_live_display_calculate_calls(void) {return atomic_load_explicit(&pane_calls,memory_order_relaxed);}

static int live_read(uintptr_t address,void *out,size_t size) {
    if(address<0x10000 || address>UINTPTR_MAX-size || !size || size>4096)return 0;
    mach_vm_size_t got=0;
    return mach_vm_read_overwrite(mach_task_self(),address,size,(uintptr_t)out,&got)==KERN_SUCCESS && got==size;
}
static uintptr_t live_pointer(const unsigned char *p,size_t offset) {
    uintptr_t value;memcpy(&value,p+offset,sizeof value);return value;
}
static float live_float(const unsigned char *p,size_t offset) {
    float value;memcpy(&value,p+offset,sizeof value);return value;
}
static uint64_t live_hash(uint64_t h,const void *data,size_t size) {
    const unsigned char *p=data;
    for(size_t i=0;i<size;i++){h^=p[i];h*=UINT64_C(1099511628211);}
    return h;
}
/* NN runtime RTTI is separate from C++ type_info. The receiving eui::Pane
   observed at +0x44fcf78 has its own getter but inherits this exact Pane ABI.
   Query that public virtual interface outside host locks, then walk its bounded
   parent chain to one of the independently located SDK type singleton records. */
static const char *live_pane_kind(uintptr_t table,uintptr_t pane) {
    switch(table-live_base) {
        case 0x40e9cc0:return "pan1";
        case 0x40ea1a0:return "pic1";
        case 0x40e9e90:return "txt1";
        case 0x40ea338:return "bnd1";
        case 0x40ea488:return "prt1";
        case 0x40ea5d8:return "wnd1";
        case 0x40ea728:return "ali1";
        case 0x4110288:return "scr1";
        /* Exact receiving eui aliases inherit Pane RTTI. Their Calculate
           slots and capture-space setup are separately guarded at install. */
        case 0x44fbc98:case 0x44fd2a0:case 0x44fc050:case 0x44fbee0:return "pan1";
        default:break;
    }
    pthread_mutex_lock(&type_lock);
    for(unsigned i=0;i<type_count;i++)if(type_cache[i].table==table) {
        const char *result=type_cache[i].kind;pthread_mutex_unlock(&type_lock);return result;
    }
    int full=type_count>=128;pthread_mutex_unlock(&type_lock);
    if(full || querying_type || table<live_base+live_data_start || table>live_base+live_size-24)return NULL;
    unsigned char method[8];
    if(!live_read(table+16,method,sizeof method))return NULL;
    uintptr_t getter=live_pointer(method,0);
    if(getter<live_base || getter>=live_base+live_text_end || (getter&3))return NULL;
    querying_type++;
    uintptr_t type=((uintptr_t(*)(uintptr_t))getter)(pane);
    querying_type--;
    static const struct {uintptr_t singleton;const char *kind;} known[]={
        {0x45f1978,"pan1"},{0x45f2de0,"pic1"},{0x45f2db0,"txt1"},{0x4599140,"bnd1"},
        {0x45f1968,"prt1"},{0x45f2e30,"wnd1"},{0x4599150,"ali1"},{0x4599208,"scr1"}
    };
    const char *result=NULL;
    for(unsigned depth=0;depth<16 && type;depth++) {
        for(unsigned i=0;i<sizeof known/sizeof known[0];i++)
            if(type==live_base+known[i].singleton){result=known[i].kind;break;}
        if(result || type<live_base+live_data_start || type>live_base+live_size-8 || (type&7))break;
        unsigned char parent[8];if(!live_read(type,parent,sizeof parent))break;
        uintptr_t next=live_pointer(parent,0);if(next==type)break;type=next;
    }
    pthread_mutex_lock(&type_lock);
    unsigned index=0;while(index<type_count && type_cache[index].table!=table)index++;
    if(index==type_count && type_count<128) {
        type_cache[type_count].table=table;type_cache[type_count].kind=result;type_count++;
    }
    pthread_mutex_unlock(&type_lock);
    return result;
}
#if LIVE_METADATA
static int live_layout_index(const char *name);
static int live_layout_needs_nested(unsigned layout,unsigned depth);
#endif
static unsigned live_resource_identity(const char *name,uintptr_t resource,uint32_t size) {
#if LIVE_METADATA
    int index=live_layout_index(name);if(index<0)return 0;
    const TotkLiveLayoutSpec *layout=&totk_live_layouts[index];
    if(!layout->resource_qualified || size!=layout->resource_size || size>8*1024*1024)return 0;
    unsigned char bytes[4096],digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_CTX context;
    if(CC_SHA256_Init(&context)!=1)return 0;
    for(uint32_t offset=0;offset<size;) {
        uint32_t count=size-offset;if(count>sizeof bytes)count=sizeof bytes;
        if(resource>UINTPTR_MAX-offset || !live_read(resource+offset,bytes,count))return 0;
        if(CC_SHA256_Update(&context,bytes,count)!=1)return 0;offset+=count;
    }
    if(CC_SHA256_Final(digest,&context)!=1)return 0;
    const char hex[]="0123456789abcdef";char result[65];
    for(unsigned i=0;i<sizeof digest;i++){result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15];}
    result[64]=0;return !strcmp(result,layout->resource_sha256);
#else
    (void)name;(void)resource;(void)size;return 0;
#endif
}
static __attribute__((unused)) int live_owner_identity(uintptr_t layout,uintptr_t root,char name[96]) {
    name[0]=0;int qualified=0;pthread_mutex_lock(&owner_lock);
    for(unsigned i=0;i<512;i++)if(owners[i].layout==layout && owners[i].root==root) {
        memcpy(name,owners[i].name,96);qualified=owners[i].qualified;break;
    }
    pthread_mutex_unlock(&owner_lock);return qualified;
}
static __attribute__((unused)) int live_owner_for_root(uintptr_t root,char name[96]) {
    name[0]=0;int found=0;pthread_mutex_lock(&owner_lock);
    for(unsigned i=0;i<512;i++)if(owners[i].root==root && owners[i].qualified) {
        if(found && strcmp(name,owners[i].name)){found=0;break;}
        memcpy(name,owners[i].name,96);found=1;
    }
    pthread_mutex_unlock(&owner_lock);return found;
}
static void live_forget_owner(uintptr_t layout) {
    pthread_mutex_lock(&owner_lock);
    for(unsigned i=0;i<512;i++)if(owners[i].layout==layout)memset(&owners[i],0,sizeof owners[i]);
    pthread_mutex_unlock(&owner_lock);
}
void runtime_live_display_owner_name(uintptr_t layout,uintptr_t root,char name[96]) {
    name[0]=0;
    pthread_mutex_lock(&owner_lock);
    for(unsigned i=0;i<512;i++)if(owners[i].layout==layout && owners[i].root==root) {
        memcpy(name,owners[i].name,96);break;
    }
    pthread_mutex_unlock(&owner_lock);
}
/* Build's virtual internal entry has eight register arguments; the public
   nine-argument Build already copied its stack option into the build context.
   x3 is the FLYT resource. Own a copied identifier, never its resource pointer. */
static uint64_t live_build(Build original,uintptr_t a0,uintptr_t a1,uintptr_t a2,uintptr_t a3,
                           uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7) {
    live_forget_owner(a0);
    unsigned char header[20],lyt[256];char name[96]={0};unsigned qualified=0;uint32_t source_size=0;
    if(live_read(a3,header,sizeof header) && !memcmp(header,"FLYT\xff\xfe\x14\0",8)) {
        uint32_t version,size;memcpy(&version,header+8,4);memcpy(&size,header+12,4);source_size=size;
        if(version==0x09000000 && size>=48 && live_read(a3+20,lyt,8) && !memcmp(lyt,"lyt1",4)) {
            uint32_t length;memcpy(&length,lyt+4,4);
            if(length>28 && length<=sizeof lyt && length<=size-20 && live_read(a3+20,lyt,length)) {
                unsigned n=0;
                while(n<95 && 28+n<length && lyt[28+n]) {
                    unsigned char c=lyt[28+n];
                    if(!((c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_'))break;
                    name[n++]=(char)c;
                }
                if(!n || 28+n>=length || lyt[28+n])name[0]=0;
            }
        }
    }
    if(name[0])qualified=live_resource_identity(name,a3,source_size);
    uint64_t result=original(a0,a1,a2,a3,a4,a5,a6,a7);
    unsigned char bytes[0x20];
    if(result && name[0] && live_read(a0,bytes,sizeof bytes)) {
        uintptr_t root=live_pointer(bytes,LIVE_ROOT);
        if(root) {
            unsigned inert_component=0;
#if LIVE_METADATA
            int resource_index=live_layout_index(name);unsigned char root_bytes[8];
            if(qualified && resource_index>=0 && live_read(root,root_bytes,8)) {
                const char *kind=live_pane_kind(live_pointer(root_bytes,0),root);
                inert_component=kind && !strcmp(kind,"prt1") && !live_layout_needs_nested((unsigned)resource_index,0);
            }
#endif
            pthread_mutex_lock(&owner_lock);
            unsigned slot=0;while(slot<512 && owners[slot].layout && owners[slot].layout!=a0)slot++;
            if(slot==512 && qualified && !inert_component)for(unsigned i=0;i<512;i++)if(!owners[i].qualified){slot=i;break;}
            if(slot<512 && !inert_component){owners[slot].layout=a0;owners[slot].root=root;memcpy(owners[slot].name,name,sizeof name);owners[slot].qualified=qualified;}
            pthread_mutex_unlock(&owner_lock);
            pthread_mutex_lock(&audit_lock);
            if(audit_file && build_reports<512) {
                build_reports++;
                fprintf(audit_file,"build owner=%#llx root=%#llx name=%s registered=%u resource_qualified=%u inert_component=%u\n",
                    (unsigned long long)a0,(unsigned long long)root,name,slot<512 && !inert_component,qualified,inert_component);
                fflush(audit_file);
            }
            pthread_mutex_unlock(&audit_lock);
        }
    }
    return result;
}
static uint64_t live_build_base(uintptr_t a0,uintptr_t a1,uintptr_t a2,uintptr_t a3,
                                uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7) {
    return live_build(build_base_original,a0,a1,a2,a3,a4,a5,a6,a7);
}
static uint64_t live_build_ex(uintptr_t a0,uintptr_t a1,uintptr_t a2,uintptr_t a3,
                              uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7) {
    return live_build(build_ex_original,a0,a1,a2,a3,a4,a5,a6,a7);
}
static uint64_t live_build_eui(uintptr_t a0,uintptr_t a1,uintptr_t a2,uintptr_t a3,
                              uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7) {
    return live_build(build_eui_original,a0,a1,a2,a3,a4,a5,a6,a7);
}
static void live_delete_eui(uintptr_t layout) {live_forget_owner(layout);delete_eui_original(layout);}
static void live_destroy(uintptr_t layout) {live_forget_owner(layout);destroy_original(layout);}
static void live_delete_base(uintptr_t layout) {live_forget_owner(layout);delete_base_original(layout);}
static void live_delete_ex(uintptr_t layout) {live_forget_owner(layout);delete_ex_original(layout);}
/* The intrusive child node is at Pane+8; +0x20/+0x28 are its child-list
   sentinel. Constructor/name-copy and Calculate recursion establish these
   offsets independently for build 082ce09b06e33a123cb1e2770f5f9147709033db. */
static int live_collect(uintptr_t pointer,uintptr_t parent,unsigned parent_index,unsigned depth,
                        LivePane panes[LIVE_MAX_PANES],unsigned *count,uint64_t *hash) {
    if(depth>LIVE_MAX_DEPTH || *count==LIVE_MAX_PANES || (pointer&7))return 0;
    for(unsigned i=0;i<*count;i++)if(panes[i].pointer==pointer)return 0;
    unsigned index=(*count)++;LivePane *pane=&panes[index];
    pane->pointer=pointer;pane->parent=parent_index;pane->depth=depth;
    if(!live_read(pointer,pane->bytes,sizeof pane->bytes) || live_pointer(pane->bytes,0x18)!=parent)return 0;
    const unsigned char *name=pane->bytes+0xb0;unsigned length=0;
    while(length<25 && name[length]) {
        if(name[length]<0x20 || name[length]>0x7e)return 0;
        length++;
    }
    if(length==25 || !length)return 0;
    uintptr_t table=live_pointer(pane->bytes,0);
    if(table<live_base+0x40e0000 || table>=live_base+0x4590000 || (table&7))return 0;
    for(unsigned off=0x30;off<0x58;off+=4) {
        float value=live_float(pane->bytes,off);
        if(!isfinite(value) || fabsf(value)>1000000)return 0;
    }
    *hash=live_hash(*hash,&depth,sizeof depth);
    *hash=live_hash(*hash,name,length+1);
    const char *kind=live_pane_kind(table,pointer);
    if(!kind)return 0;
    pane->kind=kind;
    *hash=live_hash(*hash,kind,4);
    /* Parts own another layout namespace. Bind this resource only; its child
       panes are surveyed independently rather than aliasing their names. */
    if(!strcmp(kind,"prt1") && depth)return 1;
    uintptr_t sentinel=pointer+0x20,link=live_pointer(pane->bytes,0x28),previous=sentinel;
    while(link!=sentinel) {
        unsigned char node[16];
        if(link<8 || (link&7) || !live_read(link,node,sizeof node) || live_pointer(node,0)!=previous)return 0;
        uintptr_t next=live_pointer(node,8);
        if(!live_collect(link-8,pointer,index,depth+1,panes,count,hash))return 0;
        previous=link;link=next;
    }
    return live_pointer(pane->bytes,0x20)==previous;
}
static __attribute__((noinline)) void live_survey(uintptr_t layout,uintptr_t fallback_root,uint32_t force) {
    if(audit_file && totk_live_has_room(&reservations)) {
        unsigned char owner[0x60]={0};LivePane panes[LIVE_MAX_PANES];unsigned count=0;
        uint64_t hash=UINT64_C(14695981039346656037);
        if(live_read(layout,owner,sizeof owner) || fallback_root) {
            if(!layout)memset(owner,0,sizeof owner);
            uintptr_t root=fallback_root?fallback_root:live_pointer(owner,LIVE_ROOT);
            unsigned char root_header[0x20];
            if(root && live_read(root,root_header,sizeof root_header) && live_collect(root,live_pointer(root_header,0x18),UINT32_MAX,0,panes,&count,&hash)) {
                char name[96];runtime_live_display_owner_name(layout,root,name);
                uint64_t owner_key=live_hash(hash,name,strlen(name)+1);
                if(totk_live_reserve(&reservations,owner_key,0)) {
                    pthread_mutex_lock(&audit_lock);
                    fprintf(audit_file,"layout owner=%#llx root=%#llx table=+%#llx panes=%u signature=%016llx force=%u name=%s\n",
                        (unsigned long long)layout,(unsigned long long)root,
                        (unsigned long long)(live_pointer(owner,0)-live_base),count,(unsigned long long)hash,force,name[0]?name:"unbound");
                    for(unsigned n=0;n<count;n++) {
                        const LivePane *p=&panes[n];
                        fprintf(audit_file,"pane index=%u parent=%u depth=%u type=+%#llx name=%s kind=%s pos=%.6g,%.6g scale=%.6g,%.6g size=%.6g,%.6g flags=%02x origin=%02x rotation=%.6g,%.6g,%.6g\n",
                            n,p->parent,p->depth,(unsigned long long)(live_pointer(p->bytes,0)-live_base),p->bytes+0xb0,p->kind,
                            live_float(p->bytes,0x30),live_float(p->bytes,0x34),live_float(p->bytes,0x48),live_float(p->bytes,0x4c),
                            live_float(p->bytes,0x50),live_float(p->bytes,0x54),p->bytes[0x58],p->bytes[0x5b],
                            live_float(p->bytes,0x3c),live_float(p->bytes,0x40),live_float(p->bytes,0x44));
                    }
                    fflush(audit_file);
                    pthread_mutex_unlock(&audit_lock);
                }
            } else if(root && totk_live_reserve(&reservations,root,1)) {
                    unsigned char bytes[LIVE_PANE_BYTES];
                    if(live_read(root,bytes,sizeof bytes)) {
                        pthread_mutex_lock(&audit_lock);
                        fprintf(audit_file,"rejected owner=%#llx root=%#llx parent=%#llx type=+%#llx name=%.24s collected=%u\n",
                            (unsigned long long)layout,(unsigned long long)root,(unsigned long long)live_pointer(bytes,0x18),
                            (unsigned long long)(live_pointer(bytes,0)-live_base),bytes+0xb0,count);
                    fflush(audit_file);
                        pthread_mutex_unlock(&audit_lock);
                    }
            }
        }
    }
}
static void live_calculate(uintptr_t layout,uintptr_t draw_info,uint32_t force) {
    /* Keep the large bounded read buffer out of recursive guest Calculate
       frames. Nested layouts delegate with only this small host frame. */
    if(!calculate_depth)live_survey(layout,0,force);
    calculate_depth++;
    calculate_original(layout,draw_info,force);
    calculate_depth--;
}
static void live_calculate_eui(uintptr_t layout,uintptr_t draw_info,uint32_t force) {
    if(!calculate_depth)live_survey(layout,0,force);
    calculate_depth++;calculate_eui_original(layout,draw_info,force);calculate_depth--;
}
typedef struct { unsigned count,offset[4];float values[4]; } LiveAdjustment;
typedef struct { uintptr_t pane;unsigned spec;int safe_role;unsigned capture; } LiveTarget;
typedef struct { unsigned count,namespaces;int valid;TotkLiveSource geometry;uintptr_t nongeometric_root,minimap[5],capture_pane,capture_draw,capture_context;char owner_name[96];const char *reason;LiveTarget targets[512]; } LiveScope;
static _Thread_local LiveScope *current_scope;
#if LIVE_METADATA
static int live_layout_index(const char *name) {
    for(unsigned i=0;i<sizeof totk_live_layouts/sizeof totk_live_layouts[0];i++)if(!strcmp(totk_live_layouts[i].name,name))return (int)i;
    return -1;
}
enum { SAFE_NONE,SAFE_TOP_LEFT,SAFE_BOTTOM_RIGHT,SAFE_CENTER,SAFE_WORLD,SAFE_MAP,SAFE_BOTTOM_LEFT,SAFE_BOTTOM_CENTER };
static int live_safe_role(const char *layout,const char *pane) {
    if(!strcmp(layout,"AppMap_00")) {
        if(!strcmp(pane,"RootPane"))return SAFE_MAP;
        return !strcmp(pane,"N_Cut_00") || !strcmp(pane,"N_Cut_01") ||
               !strcmp(pane,"N_Cut_02") || !strcmp(pane,"N_Cut_03")?SAFE_BOTTOM_RIGHT:SAFE_NONE;
    }
    if(strcmp(pane,"RootPane"))return SAFE_NONE;
    /* Source-bound authored guide rows are centered at the bottom; the save
       icon/text/glow composite is bottom-left. Native capture contents retain
       their separate basis while only the outer root receives these offsets. */
    if(!strcmp(layout,"CtrlGuide_00"))return SAFE_BOTTOM_CENTER;
    if(!strcmp(layout,"SaveLoadIcon_00"))return SAFE_BOTTOM_LEFT;
    if(!strcmp(layout,"PlayerStatus_00"))return SAFE_TOP_LEFT;
    if(!strcmp(layout,"EnvironmentInfo_00") || !strcmp(layout,"DoCommand_00"))return SAFE_BOTTOM_RIGHT;
    if(!strcmp(layout,"Location_00") || !strcmp(layout,"Title_00"))return SAFE_CENTER;
    if(!strcmp(layout,"PlayerStatus3D_00") || !strcmp(layout,"EnemyInfo_00") || !strcmp(layout,"BalloonMessage_00") || !strcmp(layout,"CameraPointer_00"))return SAFE_WORLD;
    static const char *const full_frame[]={"AppMenuBG_00","PauseMenuBG_00","ShopMenuParasailBG_00","ShortCutBG_00","TitleMenuBG_00","EventFade_00","EventFadeWipe_00","CameraShutter_00"};
    for(unsigned i=0;i<sizeof full_frame/sizeof full_frame[0];i++)if(!strcmp(layout,full_frame[i]))return SAFE_WORLD;
    return -1;
}
static int live_layout_needs_nested(unsigned layout,unsigned depth) {
    if(depth>16)return 1;
    const TotkLiveLayoutSpec *owner=&totk_live_layouts[layout];
    for(unsigned i=owner->first_pane;i<(unsigned)owner->first_pane+owner->pane_count;i++) {
        const TotkLivePaneSpec *p=&totk_live_panes[i];
        if((i!=owner->first_pane && p->rule_count) || (p->parts_layout!=UINT16_MAX && live_layout_needs_nested(p->parts_layout,depth+1)))return 1;
    }
    return 0;
}
static int live_kind_matches(const LivePane *pane,const char *expected,int alias) {
    if(alias && !strcmp(pane->kind,"prt1"))return 1;
    if(!strcmp(pane->kind,expected))return 1;
    uintptr_t table=live_pointer(pane->bytes,0)-live_base;
    return (!strcmp(expected,"ali1") && table==0x44fbc98) || (!strcmp(expected,"scr1") && table==0x44fd2a0);
}
static int live_capture_kind(uintptr_t table) {
    return table==live_base+0x44fc050 || table==live_base+0x44fbee0;
}
/* Receiving AppMap_00_MiniMap animation frame1, independently decoded from
   the pinned Common archive: four scissor apertures and the outer composite
   switch from full-map2250-sized fields to these exact minimap endpoints.
   N_MiniMap_00 itself is hidden in this ordinary gameplay branch. Transitions
   and full-map frame0 remain unsupported and use the complete prepared pair. */
static int live_minimap_endpoint(unsigned index,const unsigned char *bytes) {
    static const float widths[]={249,315,315,315,288},heights[]={249,492,492,492,288};
    if(index>=5 || !(bytes[0x58]&1) || !bytes[0x59])return 0;
    if(!(fabsf(live_float(bytes,0x50)-widths[index])<=.01f && fabsf(live_float(bytes,0x54)-heights[index])<=.01f))return 0;
    return index==4 || (fabsf(live_float(bytes,0x30)-765)<=.01f && fabsf(live_float(bytes,0x34)+540)<=.01f);
}
static int live_minimap_stage(LiveScope *scope,const LivePane *panes,const unsigned char *visible,unsigned count) {
    static const char *const names[]={"N_Cut_00","N_Cut_01","N_Cut_02","N_Cut_03","P_MapCapture_00"};
    for(unsigned k=0;k<5;k++) {
        const LivePane *match=NULL;
        for(unsigned i=0;i<count;i++)if(!strcmp((const char *)panes[i].bytes+0xb0,names[k])) {
            if(match || !visible[i])return 0;match=&panes[i];
        }
        if(!match || !live_kind_matches(match,k==4?"pic1":"scr1",0) || !live_minimap_endpoint(k,match->bytes))return 0;
        scope->minimap[k]=match->pointer;
    }
    return 1;
}
static int live_minimap_recheck(const LiveScope *scope) {
    if(!scope->minimap[0])return 1;
    for(unsigned i=0;i<5;i++) {
        unsigned char bytes[LIVE_PANE_BYTES];
        if(!live_read(scope->minimap[i],bytes,sizeof bytes) || !live_minimap_endpoint(i,bytes))return 0;
    }
    return 1;
}
static int live_stage_namespace(LiveScope *scope,uintptr_t root,unsigned layout,int nested,unsigned depth,int capture_domain) {
    scope->reason="bounded namespace or pane preflight";
    if(depth>16 || scope->namespaces++>=64)return 0;
    const TotkLiveLayoutSpec *owner=&totk_live_layouts[layout];
    if(!owner->resource_qualified)return 0;
    LivePane *panes=calloc(LIVE_MAX_PANES,sizeof *panes);int *specs=malloc(LIVE_MAX_PANES*sizeof *specs);
    unsigned char *visible=calloc(LIVE_MAX_PANES,1),*island=calloc(LIVE_MAX_PANES,1);unsigned count=0;uint64_t hash=UINT64_C(14695981039346656037);
    unsigned char root_header[0x20];int ok=panes && specs && visible && island && live_read(root,root_header,sizeof root_header) &&
        live_collect(root,live_pointer(root_header,0x18),UINT32_MAX,0,panes,&count,&hash);
    if(!ok)goto done;
    if(!nested && strcmp((const char *)panes[0].bytes+0xb0,"RootPane")){ok=0;goto done;}
    for(unsigned i=0;i<count;i++) {
        LivePane *p=&panes[i];specs[i]=-1;
        visible[i]=(p->bytes[0x58]&1) && p->bytes[0x59] && (!i || visible[p->parent]);
        int capture=live_capture_kind(live_pointer(p->bytes,0));
        island[i]=capture || (!i?capture_domain:island[p->parent]);
        if(visible[i] && (p->bytes[0x58]&0x40) && !capture){scope->reason="absolute UI matrix needs qualification";ok=0;goto done;}
        if(!i)specs[i]=owner->first_pane;
        else if(specs[p->parent]>=0) {
            for(unsigned s=owner->first_pane+1;s<(unsigned)owner->first_pane+owner->pane_count;s++)
                if(totk_live_panes[s].parent==specs[p->parent] && !strcmp(totk_live_panes[s].name,(const char *)p->bytes+0xb0)) {
                    if(specs[i]>=0){ok=0;goto done;}specs[i]=(int)s;
                }
        }
        if(specs[i]>=0 && !live_kind_matches(p,totk_live_panes[specs[i]].kind,nested && !i)){ok=0;goto done;}
        if(visible[i] && capture) {
            /* The derived eui method owns a different projection/modelview.
               Preserve this whole coordinate island, including nested Parts;
               only its outer composite receives the main-scene correction. */
            if(specs[i]<0 || totk_live_panes[specs[i]].rule_count ||
               (!nested && live_safe_role(owner->name,(const char *)p->bytes+0xb0)!=SAFE_NONE) || scope->count==512) {
                scope->reason="capture boundary ownership or transform needs qualification";ok=0;goto done;
            }
            scope->targets[scope->count++]=(LiveTarget){.pane=p->pointer,.spec=(unsigned)specs[i],.capture=1};
        }
    }
    for(unsigned s=owner->first_pane;s<(unsigned)owner->first_pane+owner->pane_count;s++) {
        const TotkLivePaneSpec *spec=&totk_live_panes[s];
        int role=nested?SAFE_NONE:live_safe_role(owner->name,spec->name);
        if(nested && s==owner->first_pane)continue; /* primary Parts owns this aliased root pose */
        if(!spec->rule_count && role==SAFE_NONE)continue;
        int match=-1;
        for(unsigned i=0;i<count;i++)if(specs[i]==(int)s){if(match>=0){ok=0;goto done;}match=(int)i;}
        if(match<0){ok=0;goto done;}
        LivePane *p=&panes[match];
        if(!visible[match])continue;
        if(island[match])continue;
        if(role<0 || (p->bytes[0x58]&0x60) || scope->count==512){scope->reason=role<0?"safe-area role not qualified":"user/global matrix or plan capacity";ok=0;goto done;}
        for(unsigned r=spec->first_rule;r<(unsigned)spec->first_rule+spec->rule_count;r++)
            if(totk_live_rules[r].fill_candidate && !spec->simple_fill){scope->reason="mixed background subtree needs qualification";ok=0;goto done;}
        if(role==SAFE_MAP) {
            if(!live_minimap_stage(scope,panes,visible,count)){scope->reason="full map or transition uses prepared fallback";ok=0;goto done;}
        }
        scope->targets[scope->count++]=(LiveTarget){.pane=p->pointer,.spec=s,.safe_role=role};
    }
    /* An aliased namespace root is already admitted here; recurse only into
       other embedded Parts, never back into this root's own owner record. */
    for(unsigned i=1;i<count;i++)if(visible[i] && !strcmp(panes[i].kind,"prt1")) {
        /* A source-bound capture is an opaque native rendering domain. Its
           component namespaces consume no main-scene transforms at all. The
           capture entry/context must still qualify before this frame can pass. */
        if(island[i])continue;
        char child_name[96];int expected=-1;
        if(specs[i]>=0)expected=totk_live_panes[specs[i]].parts_layout;
        if(expected==UINT16_MAX)expected=-1;
        int bound=live_owner_for_root(panes[i].pointer,child_name);
        int child=bound?live_layout_index(child_name):-1;
        if(expected>=0 && ((child>=0 && child!=expected) || (live_layout_needs_nested((unsigned)expected,0) && child!=expected))){scope->reason="Parts resource identity mismatch";ok=0;goto done;}
        if(child>=0 && live_layout_needs_nested((unsigned)child,0) && !live_stage_namespace(scope,panes[i].pointer,(unsigned)child,1,depth+1,island[i])){ok=0;goto done;}
    }
done:
    free(island);free(visible);free(specs);free(panes);return ok;
}
static void live_admission_note(const LiveScope *scope) {
    const char *name=scope->owner_name[0]?scope->owner_name:"unbound";
    const char *reason=scope->reason?scope->reason:"in-call pose/basis guard";
    uint64_t hash=live_hash(UINT64_C(14695981039346656037),name,strlen(name));
    hash=live_hash(hash,reason,strlen(reason))|UINT64_C(0x8000000000000000);
    if(totk_live_reserve(&reservations,hash,1)) {
        pthread_mutex_lock(&audit_lock);
        fprintf(stderr,"[totk-live] prepared fallback: owner=%s reason=%s\n",name,reason);
        if(scope->capture_pane) {
            unsigned char pane[0x100],context[32],draw[112];
            if(live_read(scope->capture_pane,pane,sizeof pane) && live_read(scope->capture_context,context,sizeof context) &&
               scope->capture_draw<=UINTPTR_MAX-0x80 && live_read(scope->capture_draw+0x10,draw,sizeof draw)) {
                uintptr_t table=live_pointer(pane,0)-live_base;unsigned mode=table==0x44fc050?pane[0xe9]:pane[0xd3];
                fprintf(stderr,"[totk-live] bounded capture guard: pane=%.24s class=+%#llx mode=%u flags=%02x extent=%g,%g ctx_view_matches=%d alpha_propagation=%u global_view_equal=%d view_z=%g,%g,%g,%g\n",
                    pane+0xb0,(unsigned long long)table,mode,pane[0x58],live_float(pane,0x50),live_float(pane,0x54),
                    live_pointer(context,8)==scope->capture_draw+0x50,context[0x1f],!memcmp(pane+0x70,draw+64,48),
                    live_float(draw,96),live_float(draw,100),live_float(draw,104),live_float(draw,108));
            }
        }
        pthread_mutex_unlock(&audit_lock);
    }
}
/* Empty SDK/eui containers do not consume projection geometry. Unknown custom
   DrawSelf classes conservatively count as drawing. Recheck after Calculate. */
static int live_possible_geometry(uintptr_t pointer,unsigned depth,unsigned *count) {
    if(depth>LIVE_MAX_DEPTH || (*count)++>=8192)return 1;
    unsigned char bytes[LIVE_PANE_BYTES];if(!live_read(pointer,bytes,sizeof bytes))return 1;
    if(!(bytes[0x58]&1) || !bytes[0x59])return 0;
    uintptr_t table=live_pointer(bytes,0);const char *kind=live_pane_kind(table,pointer);
    if(!kind)return 1;
    if(!strcmp(kind,"pic1") || !strcmp(kind,"txt1") || !strcmp(kind,"wnd1"))return 1;
    switch(table-live_base) {
        case 0x40e9cc0:case 0x40ea338:case 0x40ea488:case 0x40ea728:case 0x4110288:
        case 0x44fcf78:case 0x44fcbc8:case 0x44fc5f0:case 0x44fcd00:case 0x44fd2a0:case 0x44fbc98:break;
        default:return 1;
    }
    uintptr_t sentinel=pointer+0x20,link=live_pointer(bytes,0x28),previous=sentinel;
    while(link!=sentinel) {
        unsigned char node[16];
        if(link<8 || (link&7) || !live_read(link,node,sizeof node) || live_pointer(node,0)!=previous)return 1;
        uintptr_t next=live_pointer(node,8);
        if(live_possible_geometry(link-8,depth+1,count))return 1;
        previous=link;link=next;
    }
    return live_pointer(bytes,0x20)!=previous;
}
static int live_empty_container(uintptr_t root) {unsigned count=0;return !live_possible_geometry(root,0,&count);}
static void live_scope_begin(LiveScope *scope,uintptr_t pane,uintptr_t draw_info,uintptr_t context) {
    pthread_mutex_lock(&geometry_lock);scope->geometry=active_source;pthread_mutex_unlock(&geometry_lock);
    scope->valid=0;scope->reason="source owner or resource identity not qualified";unsigned char bytes[LIVE_PANE_BYTES],draw[8],owner[0x20],alpha[4];
    if(!live_read(pane,bytes,sizeof bytes))goto failed;
    if(!(bytes[0x58]&1) || !bytes[0x59])return;
    if(context<=UINTPTR_MAX-0x18 && live_read(context+0x18,alpha,4) && live_float(alpha,0)<=0)return;
    if(draw_info>UINTPTR_MAX-0xc0 || !live_read(draw_info+0xc0,draw,8))goto failed;
    uintptr_t layout=live_pointer(draw,0);
    if(!live_read(layout,owner,sizeof owner))goto failed;
    uintptr_t root=live_pointer(owner,LIVE_ROOT);char name[96]={0};
    int qualified=live_owner_identity(layout,root,name);memcpy(scope->owner_name,name,sizeof name);
    int index=live_layout_index(name);
    int role=index>=0?live_safe_role(name,"RootPane"):-1;
    if((index<0 || role<0 || role==SAFE_MAP) && live_empty_container(root)) {scope->nongeometric_root=root;return;}
    if(!qualified || index<0)goto failed;
    if(!live_stage_namespace(scope,root,(unsigned)index,0,0,0))goto failed;
    scope->valid=1;return;
failed:
    atomic_fetch_add(&missed_panes,1);live_admission_note(scope);
}
static int live_safe_delta(const LiveScope *scope,int role,uintptr_t pane,uintptr_t context,float *dx,float *dy) {
    const TotkLiveSource *g=&scope->geometry;double x=0,y=0;
    if(role==SAFE_TOP_LEFT){x=g->safe_insets[1];y=-g->safe_insets[0];}
    else if(role==SAFE_BOTTOM_RIGHT){x=-g->safe_insets[3];y=g->safe_insets[2];}
    else if(role==SAFE_BOTTOM_LEFT){x=g->safe_insets[1];y=g->safe_insets[2];}
    else if(role==SAFE_BOTTOM_CENTER){x=(g->safe_insets[1]-g->safe_insets[3])*.5;y=g->safe_insets[2];}
    else if(role==SAFE_CENTER){x=(g->safe_insets[1]-g->safe_insets[3])*.5;y=(g->safe_insets[2]-g->safe_insets[0])*.5;}
    if(!x && !y){*dx=*dy=0;return 1;}
    if(g->width<=0 || g->height<=0)return 0;
    x*=1920/g->width;y*=1080/g->height;
    unsigned char header[0x20],matrix[48],pointer[8];
    if(!live_read(pane,header,sizeof header))return 0;
    uintptr_t parent=live_pointer(header,0x18),basis;
    if(parent) {if(parent>UINTPTR_MAX-0x70)return 0;basis=parent+0x70;}
    else {if(context>UINTPTR_MAX-8 || !live_read(context+8,pointer,8))return 0;basis=live_pointer(pointer,0);}
    if(!live_read(basis,matrix,sizeof matrix))return 0;
    double a=live_float(matrix,0),b=live_float(matrix,4),c=live_float(matrix,16),d=live_float(matrix,20),det=a*d-b*c;
    if(!isfinite(det) || fabs(det)<1e-7)return 0;
    double lx=(d*x-b*y)/det,ly=(-c*x+a*y)/det;
    if(!isfinite(lx) || !isfinite(ly) || fabs(lx)>1000000 || fabs(ly)>1000000)return 0;
    *dx=(float)lx;*dy=(float)ly;return 1;
}
static int live_native_matrix(const unsigned char *matrix) {
    for(unsigned i=0;i<48;i+=4)if(!isfinite(live_float(matrix,i)))return 0;
    double a=live_float(matrix,0),b=live_float(matrix,4),c=live_float(matrix,8),
           d=live_float(matrix,16),e=live_float(matrix,20),f=live_float(matrix,24),
           g=live_float(matrix,32),h=live_float(matrix,36),j=live_float(matrix,40);
    return fabs(a*(e*j-f*h)-b*(d*j-f*g)+c*(d*h-e*g))>=1e-7;
}
static int live_capture_context(uintptr_t pane,uintptr_t draw_info,uintptr_t context,const unsigned char *bytes,const char **reason) {
    unsigned char basis[8],native[112],capturing;
    *reason="capture context pointer/read guard";
    if(!live_capture_kind(live_pointer(bytes,0)) || draw_info>UINTPTR_MAX-0x80 || context>UINTPTR_MAX-0x20 ||
       !live_read(context+8,basis,8) || !live_read(context+0x1f,&capturing,1) || !live_read(draw_info+0x10,native,sizeof native))return 0;
    *reason="capture context view pointer differs from DrawInfo";if(live_pointer(basis,0)!=draw_info+0x50)return 0;
    *reason="capture context alpha propagation was not suspended";if(capturing)return 0;
    float width=live_float(bytes,0x50),height=live_float(bytes,0x54);
    *reason="capture extent is not finite positive";
    if(!isfinite(width) || !isfinite(height) || width<=0 || height<=0)return 0;
    *reason="capture projection or view contains nonfinite values";
    for(unsigned i=0;i<sizeof native;i+=4)if(!isfinite(live_float(native,i)))return 0;
    *reason="capture projection or view is degenerate";
    if(!live_native_matrix(native+64) ||
       fabsf(live_float(native,0))+fabsf(live_float(native,4))+fabsf(live_float(native,8))<1e-7 ||
       fabsf(live_float(native,16))+fabsf(live_float(native,20))+fabsf(live_float(native,24))<1e-7)return 0;
    *reason="capture does not establish an isolated global basis";
    if(!(bytes[0x58]&0x40))return 0;
    unsigned char mode;unsigned offset=live_pointer(bytes,0)==live_base+0x44fc050?0xe9:0xd3;
    *reason="capture projection-mode read guard";if(pane>UINTPTR_MAX-offset || !live_read(pane+offset,&mode,1))return 0;
    if(mode&1) {
        /* Only perspective branches copy DrawInfo view into GlobalMtx. */
        *reason="capture GlobalMtx differs from current perspective view";
        if(memcmp(bytes+0x70,native+64,48))return 0;
    } else {
        /* Default branches replace DrawInfo projection/view but deliberately
           preserve an existing40 GlobalMtx. Exact mode/code/context guards
           distinguish that native island from an arbitrary absolute matrix. */
        *reason="capture preserved native global is nonfinite or degenerate";
        if(!live_native_matrix(bytes+0x70))return 0;
    }
    return 1;
}
static void live_adjust_begin(uintptr_t pane,uintptr_t draw_info,uintptr_t context,LiveAdjustment *restore) {
    LiveScope *scope=current_scope;if(!scope || !scope->valid)return;
    const LiveTarget *target=NULL;
    for(unsigned i=0;i<scope->count;i++)if(scope->targets[i].pane==pane){target=&scope->targets[i];break;}
    if(!target)return;
    const TotkLivePaneSpec *spec=&totk_live_panes[target->spec];unsigned char bytes[LIVE_PANE_BYTES];
    scope->reason="current target read guard";
    if(!live_read(pane,bytes,sizeof bytes))goto failed;
    if(target->capture) {
        scope->capture_pane=pane;scope->capture_draw=draw_info;scope->capture_context=context;
        if(!live_capture_context(pane,draw_info,context,bytes,&scope->reason))goto failed;
        scope->capture_pane=0;
        return;
    }
    scope->reason="current target acquired user/global matrix";
    if(bytes[0x58]&0x60)goto failed;
    float changed[4];double aspect=scope->geometry.aspect;
    scope->reason="current affine pose is nonfinite or exceeds capacity";
    if(fabs(aspect-4.0/3.0)>1e-7)for(unsigned i=spec->first_rule;i<(unsigned)spec->first_rule+spec->rule_count;i++) {
        const TotkLiveRule *rule=&totk_live_rules[i];float before=live_float(bytes,rule->field);
        double m=rule->multiplier[0]+rule->multiplier[1]*aspect+rule->multiplier[2]/aspect;
        double a=rule->addition[0]+rule->addition[1]*aspect+rule->addition[2]/aspect;
        double after=before*m+a;if(!isfinite(after) || fabs(after)>1000000 || restore->count==4)goto failed;
        unsigned n=restore->count++;restore->offset[n]=rule->field;restore->values[n]=before;changed[n]=(float)after;
    }
    scope->reason="current safe-area parent basis is invalid";
    float dx,dy;if(!live_safe_delta(scope,target->safe_role,pane,context,&dx,&dy))goto failed;
    scope->reason="current safe-area translation is nonfinite or exceeds capacity";
    const unsigned offsets[]={0x30,0x34};const float deltas[]={dx,dy};
    for(unsigned j=0;j<2;j++)if(deltas[j]) {
        unsigned n=0;while(n<restore->count && restore->offset[n]!=offsets[j])n++;
        if(n==restore->count) {
            if(n==4)goto failed;restore->count++;restore->offset[n]=offsets[j];
            restore->values[n]=live_float(bytes,offsets[j]);changed[n]=restore->values[n];
        }
        changed[n]+=deltas[j];if(!isfinite(changed[n]) || fabsf(changed[n])>1000000)goto failed;
    }
    for(unsigned i=0;i<restore->count;i++)memcpy((void *)(pane+restore->offset[i]),&changed[i],4);
    if(restore->count){*(unsigned char *)(pane+0x58)|=0x10;atomic_fetch_add(&adjusted_panes,1);}return;
failed:
    restore->count=0;scope->valid=0;atomic_fetch_add(&missed_panes,1);live_admission_note(scope);
}
#else
static void live_scope_begin(LiveScope *scope,uintptr_t pane,uintptr_t draw,uintptr_t context){(void)scope;(void)pane;(void)draw;(void)context;}
static void live_adjust_begin(uintptr_t pane,uintptr_t draw,uintptr_t context,LiveAdjustment *restore){(void)pane;(void)draw;(void)context;(void)restore;}
#endif
static void live_adjust_end(uintptr_t pane,const LiveAdjustment *restore) {
    if(!restore->count)return;
    for(unsigned i=0;i<restore->count;i++)memcpy((void *)(pane+restore->offset[i]),&restore->values[i],4);
    *(unsigned char *)(pane+0x58)|=0x10;
}
static void live_pane_delegate(uintptr_t pane,uintptr_t draw_info,uintptr_t context,uint32_t force) {
    LiveAdjustment restore={0};live_adjust_begin(pane,draw_info,context,&restore);
    pane_calculate_depth++;
    pane_calculate_original(pane,draw_info,context,restore.count?1:force);
    pane_calculate_depth--;
    live_adjust_end(pane,&restore);
}
static __attribute__((noinline)) void live_pane_outer(uintptr_t pane,uintptr_t draw_info,uintptr_t context,uint32_t force) {
    LiveScope scope={0};LiveScope *previous=current_scope;
    live_scope_begin(&scope,pane,draw_info,context);current_scope=&scope;
    live_pane_delegate(pane,draw_info,context,force);
#if LIVE_METADATA
    if(scope.valid && !live_minimap_recheck(&scope)) {
        scope.reason="minimap endpoint changed during Calculate";
        atomic_fetch_add(&missed_panes,1);live_admission_note(&scope);
    }
    if(scope.nongeometric_root && !live_empty_container(scope.nongeometric_root)) {
        scope.reason="geometry appeared during empty-container Calculate";
        atomic_fetch_add(&missed_panes,1);live_admission_note(&scope);
    }
#endif
    current_scope=previous;
}
static void live_pane_calculate(uintptr_t pane,uintptr_t draw_info,uintptr_t context,uint32_t force) {
    if(!atomic_fetch_add_explicit(&pane_calls,1,memory_order_relaxed))
        fprintf(stderr,"[totk-live] exact Pane Calculate entry reached\n");
    if(!pane_calculate_depth) {
        unsigned char header[0x20];
        if(live_read(pane,header,sizeof header) && !live_pointer(header,0x18)) {
            uintptr_t owner=0;unsigned char bytes[8];
            if(draw_info<=UINTPTR_MAX-0xc0 && live_read(draw_info+0xc0,bytes,sizeof bytes))owner=live_pointer(bytes,0);
            live_survey(owner,pane,force);
        }
        if(runtime_live_display_enabled()) {live_pane_outer(pane,draw_info,context,force);return;}
    }
    live_pane_delegate(pane,draw_info,context,force);
}
static uint32_t live_branch(uint64_t from,uint64_t to) {
    int64_t difference=(int64_t)to-(int64_t)from;
    if((difference&3) || difference<-(INT64_C(1)<<27) || difference>=(INT64_C(1)<<27))
        runtime_fail("live display: signed branch range");
    return 0x14000000u|((uint32_t)(difference>>2)&0x03ffffffu);
}
/* An exact function-entry observer also covers devirtualized/inlined Layout
   callers. The signed trampoline executes the displaced instruction once;
   runtime callback addresses live only on main's private writable data page. */
static uintptr_t live_entry(Module *module,uint64_t site,uint32_t original,uint64_t stub,uint64_t slot,void *callback,int signed_code) {
    uint64_t data=(module->file_size+0x3fff)&~UINT64_C(0x3fff);
    if(stub<module->text_end || stub+32>((module->text_end+0x3fff)&~UINT64_C(0x3fff)) || data+slot+8>module->size)
        runtime_fail("live display: entry veneer bounds");
    int64_t pages=(int64_t)(data>>12)-(int64_t)(stub>>12);
    if(pages<-(1<<20) || pages>=(1<<20) || (slot&7) || slot>32760)runtime_fail("live display: entry data address");
    uint32_t immediate=(uint32_t)pages&0x1fffffu;
    uint32_t code[]={0x90000010u|((immediate&3)<<29)|((immediate>>2)<<5),
                     0xf9400210u|((uint32_t)(slot/8)<<10),0xd61f0200u,0xd503201fu,
                     original,live_branch(stub+20,site+4),0xd503201fu,0xd503201fu};
    uint32_t jump=live_branch(site,stub),have;memcpy(&have,module->base+site,4);
    if(signed_code) {
        if(have!=jump || memcmp(module->base+stub,code,sizeof code))runtime_fail("live display: signed entry veneer mismatch");
    } else {
        if(have!=original)runtime_fail("live display: original entry instruction mismatch");
        for(unsigned i=0;i<32;i++)if(module->base[stub+i])runtime_fail("live display: entry padding already used");
        memcpy(module->base+stub,code,sizeof code);memcpy(module->base+site,&jump,4);
    }
    uintptr_t wrapped=callback?(uintptr_t)runtime_hle_wrap_named(callback,"TOTK1.0.0::Pane::Calculate entry"):0;
    memcpy(module->base+data+slot,&wrapped,8);
    return (uintptr_t)module->base+stub+16;
}

static uint32_t live_adrp(uint64_t site,uint64_t target) {
    int64_t pages=(int64_t)(target>>12)-(int64_t)(site>>12);
    if(pages<-(1<<20) || pages>=(1<<20))runtime_fail("live display: viewport page out of range");
    uint32_t immediate=(uint32_t)pages&0x1fffffu;
    return 0x90000000u|((immediate&3)<<29)|((immediate>>2)<<5);
}
static void live_viewport_install(Module *module,int signed_code) {
    if(!LIVE_METADATA)runtime_fail("live display: prepare the private verified layout metadata header first");
    uint64_t data=(module->file_size+0x3fff)&~UINT64_C(0x3fff);
    static const uint64_t sites[]={0x165b4,0x296b4,0x34b64,0x454c4,0x51204};
    float baseline;memcpy(&baseline,module->base+0x16438,4);
    if(fabsf(baseline-4.0f/3.0f)>1e-6f || !getenv("TOTK_IPAD_ASPECT_PATCH"))
        runtime_fail("live display: requires the verified base4:3 camera and UI preparation");
    for(unsigned i=0;i<sizeof sites/sizeof sites[0];i++) {
        uint32_t original[]={live_adrp(sites[i],0x16438),0xbd443800u};
        uint32_t replacement[]={live_adrp(sites[i],data),0xbd403c00u};
        if(signed_code) {
            if(memcmp(module->base+sites[i],replacement,sizeof replacement))runtime_fail("live display: signed viewport veneer mismatch");
        } else {
            if(memcmp(module->base+sites[i],original,sizeof original))runtime_fail("live display: prepared viewport instruction mismatch");
            memcpy(module->base+sites[i],replacement,sizeof replacement);
        }
    }
    if(!runtime_aspect_publish_live)runtime_fail("live display: camera publisher unavailable");
    runtime_aspect_publish_live(4.0f/3.0f,4.0f/3.0f);
    atomic_store_explicit(&paired_enabled,1,memory_order_release);
}

void runtime_live_display_install(Module *module) {
    const char *path=getenv("TOTK_LIVE_DISPLAY_AUDIT");
    const char *enable=getenv("TOTK_LIVE_DISPLAY");int paired=enable && !strcmp(enable,"1");
    int exporting=getenv("TOTK_EXPORT_IOS")!=NULL;
    if(((!path || !*path) && !paired) || (exporting && !paired))return;
    int signed_code=TARGET_OS_IPHONE;
#if defined(TOTK_TEST_SIGNED_DISPLAY)
    if(!exporting)signed_code=1; /* CPU fixture runs the immutable embedded-code verifier. */
#endif
    if(strcmp(module->name,"main") || module->file_size<0x4590000)runtime_fail("live display: wrong main image");
    static const struct {uint64_t offset;uint32_t words[4];} guards[]={
        {0x40e94,{0xd10183ff,0xa9037bfd,0x9100c3fd,0xf90023f5}},
        {0x747f4c,{0xd10743ff,0xa9177bfd,0x9105c3fd,0xa9186ffc}},
        {0x74f330,{0xd10183ff,0xa9037bfd,0x9100c3fd,0xf90023f5}},
        {0x4770c,{0xf9400c0b,0x9101c169,0xf9400428,0xf100017f}},
        {0xc20fb8,{0xfc1b0fe8,0xa900fbfd,0x910023fd,0xf9000ff9}},
        {0xc20e98,{0xa9bd7bfd,0xa90157f6,0x910003fd,0xa9024ff4}},
        {0x223206c,{0xd102c3ff,0xa9077bfd,0x9101c3fd,0xa9085ff8}},
        {0x115eca8,{0xd102c3ff,0xa9077bfd,0x9101c3fd,0xa9085ff8}},
        {0x115ede0,{0xd10783ff,0x6d192beb,0xf900e3fc,0x6d1a23e9}},
        {0x910de0,{0xfc1a0fec,0x6d00abeb,0xf9001ffc,0x6d01a3e9}},
        {0x2232100,{0x3943a6c8,0x37000228,0xaa1403e0,0x979b7b35}},
        {0x115ed58,{0x39434e68,0x370002c8,0xaa1503e0,0x97dec81f}},
        {0x2232154,{0x9101c2c0,0xaa1703e1,0x52800602,0x9421c840}},
        {0x115edc0,{0x9101c260,0xaa1703e1,0x52800602,0x94651525}},
        {0x6f4d3c,{0x39400028,0x34000c28,0x3902c008,0xaa0103e9}}
    };
    for(unsigned i=0;i<sizeof guards/sizeof guards[0];i++) {
        unsigned skip=signed_code && (guards[i].offset==0xc20fb8 || guards[i].offset==0x747f4c) ? 4 : 0;
        if(memcmp(module->base+guards[i].offset+skip,(const unsigned char *)guards[i].words+skip,sizeof guards[i].words-skip))
            runtime_fail("live display: exact Calculate/Pane ABI guard mismatch");
    }
    static const uint64_t slots[]={0x40e9650,0x40eaf10,0x40e9618,0x40eaed8,0x40e95a0,0x40eae60,0x40e95a8,0x40eae68,0x44fbe88,0x44fbe50,0x44fbdd8,0x44fbde0};
    static const uint64_t functions[]={LIVE_CALCULATE,LIVE_CALCULATE,0x3f6f4,0x6688c,0x3e8b4,0x3e8b4,0x3e8c8,0x66ca0,0x74f330,0x747f4c,0x3e8b4,0x29f4594};
    for(unsigned i=0;i<sizeof slots/sizeof slots[0];i++) {
        uintptr_t value;memcpy(&value,module->base+slots[i],8);
        if(value!=(uintptr_t)module->base+functions[i])runtime_fail("live display: Build/Calculate/destroy vtable guard mismatch");
    }
    static const struct {uint64_t table,calculate;} pane_classes[]={
        {0x44fbc98,0xc20e98},{0x44fd2a0,0xc20fb8},
        {0x44fc050,0x223206c},{0x44fbee0,0x115eca8}
    };
    for(unsigned i=0;i<sizeof pane_classes/sizeof pane_classes[0];i++) {
        uintptr_t value;memcpy(&value,module->base+pane_classes[i].table+0xd0,8);
        if(value!=(uintptr_t)module->base+pane_classes[i].calculate)
            runtime_fail("live display: exact eui alignment/scissor/capture class mismatch");
    }
    if(!exporting && path && *path) {audit_file=fopen(path,"w");if(!audit_file)runtime_fail("live display: cannot open bounded ownership audit");}
    live_base=(uintptr_t)module->base;live_size=module->size;live_text_end=module->text_end;live_data_start=module->data_off;
    calculate_original=(Calculate)(live_base+LIVE_CALCULATE);
    calculate_eui_original=(Calculate)(live_base+0x74f330);
    build_eui_original=(Build)(live_base+0x747f4c);delete_eui_original=(Destroy)(live_base+0x29f4594);
    build_base_original=(Build)(live_base+functions[2]);build_ex_original=(Build)(live_base+functions[3]);
    destroy_original=(Destroy)(live_base+functions[4]);delete_base_original=(Destroy)(live_base+functions[6]);delete_ex_original=(Destroy)(live_base+functions[7]);
    void *callbacks[]={(void *)live_calculate,(void *)live_calculate,(void *)live_build_base,(void *)live_build_ex,
                       (void *)live_destroy,(void *)live_destroy,(void *)live_delete_base,(void *)live_delete_ex,
                       (void *)live_calculate_eui,(void *)live_build_eui,(void *)live_destroy,(void *)live_delete_eui};
    if(!exporting)for(unsigned i=0;i<sizeof slots/sizeof slots[0];i++) {
        uintptr_t wrapped=(uintptr_t)runtime_hle_wrap_named(callbacks[i],"TOTK1.0.0::Layout ownership");
        memcpy(module->base+slots[i],&wrapped,sizeof wrapped);
    }
    uint64_t entry_stub=((module->text_end+15)&~UINT64_C(15))+64;
    pane_calculate_original=(PaneCalculate)live_entry(module,0xc20fb8,0xfc1b0fe8,entry_stub,72,exporting?NULL:(void *)live_pane_calculate,signed_code);
    build_eui_original=(Build)live_entry(module,0x747f4c,0xd10743ff,entry_stub+32,80,exporting?NULL:(void *)live_build_eui,signed_code);
    if(paired)live_viewport_install(module,signed_code);
    fprintf(stderr,"[totk-live] guarded owner/Calculate entries installed; paired=%d\n",paired);
}
