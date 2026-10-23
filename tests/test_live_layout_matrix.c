/* Execute the exact private TOTK Pane matrix code without a game, GPU or saves.
   The Python driver verifies both input hashes before this fixture maps them. */
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <dlfcn.h>
#include <execinfo.h>
#include "totk_live_display.h"
#ifdef FIXTURE_PAIRED
#include "aspect_patch.h"
#include "totk_live_layouts.h"
#else
float runtime_aspect_now(void) {return 4.0f/3.0f;}
#endif

typedef struct { unsigned char bytes[0x400]; } Pane;
typedef float Matrix[12];
typedef void (*Calculate)(void *,void *,void *,uint32_t);
typedef void (*Local)(void *,void *);
static const Matrix identity={1,0,0,0,0,1,0,0,0,0,1,0};
static unsigned checks;
static uintptr_t fixture_image_base;
static void fixture_trap(int signal_number,siginfo_t *info,void *raw) {
    (void)info;ucontext_t *context=raw;
    dprintf(2,"fixture trap signal%d pc=+%#llx lr=+%#llx x0=%#llx x1=%#llx x2=%#llx\n",signal_number,
        (unsigned long long)(context->uc_mcontext->__ss.__pc-fixture_image_base),
        (unsigned long long)(context->uc_mcontext->__ss.__lr-fixture_image_base),
        (unsigned long long)context->uc_mcontext->__ss.__x[0],(unsigned long long)context->uc_mcontext->__ss.__x[1],
        (unsigned long long)context->uc_mcontext->__ss.__x[2]);
    Dl_info symbol;if(dladdr((void *)context->uc_mcontext->__ss.__pc,&symbol))dprintf(2,"native symbol %s\n",symbol.dli_sname?symbol.dli_sname:"unknown");
    void *frames[16];int count=backtrace(frames,16);backtrace_symbols_fd(frames,count,2);_exit(100+signal_number);
}
static int expected_install_rejection;
_Noreturn void runtime_fail(const char *message) {
    if(expected_install_rejection && strstr(message,"signed"))_exit(77);
    fprintf(stderr,"%s\n",message);abort();
}
static pthread_key_t hle_key;
static uintptr_t host_low,host_high;
static _Atomic unsigned hle_notes;
static unsigned geometry_notifications;
void mtl_present_geometry_changed(void) {geometry_notifications++;}
void runtime_thread_note_hle(unsigned index) {(void)index;atomic_fetch_add(&hle_notes,1);}
uint32_t runtime_thread_current_handle(void) {return 0xc001;}
uint64_t runtime_now_ns(void) {struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+(unsigned)t.tv_nsec;}
extern int fixture_call_sentinels(Calculate,void *,void *,void *,uint32_t);
extern int fixture_call_on_guest_stack(void *,Calculate,void *,void *,void *,uint32_t);
static void check_host_stack(void) {
    uintptr_t sp;__asm__ volatile("mov %0, sp":"=r"(sp));
    assert(sp>=host_low && sp<host_high && pthread_getspecific(hle_key)==NULL);
}
static uint64_t nested_probe(uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    check_host_stack();assert(a==0x111 && b==0x222 && c==0x333 && d==0x444);
    return a+3*b+5*c+7*d;
}
static double argument_probe(uint64_t a,uint64_t b,uint64_t c,uint64_t d,double x,double y) {
    check_host_stack();assert(x==1.25 && y==-2.5);
    uint64_t (*nested)(uint64_t,uint64_t,uint64_t,uint64_t)=runtime_hle_wrap_named((void *)nested_probe,"fixture nested four arguments");
    return x+2*y+(double)nested(a,b,c,d);
}

static void put_pointer(void *base,size_t offset,const void *pointer) {memcpy((char *)base+offset,&pointer,8);}
static void put_float(void *base,size_t offset,float value) {memcpy((char *)base+offset,&value,4);}
static void multiply(Matrix out,const Matrix left,const Matrix right) {
    Matrix value={0};
    for(unsigned r=0;r<3;r++)for(unsigned c=0;c<4;c++) {
        for(unsigned k=0;k<3;k++)value[r*4+c]+=left[r*4+k]*right[k*4+c];
        if(c==3)value[r*4+c]+=left[r*4+3];
    }
    memcpy(out,value,sizeof value);
}
static void equal(const Matrix a,const Matrix b) {
    for(unsigned i=0;i<12;i++){
        int ok=isfinite(a[i]) && isfinite(b[i]) && fabsf(a[i]-b[i])<0.0004f*(1+fabsf(b[i]));
        if(!ok)fprintf(stderr,"matrix component %u: got %.9g expected %.9g\n",i,a[i],b[i]);
        assert(ok);checks++;
    }
}
static void pose(Matrix value,float angle,float x,float y,float sx,float sy) {
    memcpy(value,identity,sizeof(Matrix));
    value[0]=cosf(angle)*sx;value[1]=-sinf(angle)*sy;value[3]=x;
    value[4]=sinf(angle)*sx;value[5]=cosf(angle)*sy;value[7]=y;
}
static void pane_init(Pane *pane,void *table,Pane *parent,const Matrix local) {
    memset(pane,0,sizeof *pane);put_pointer(pane,0,table);put_pointer(pane,0x18,parent);
    put_pointer(pane,0x20,pane->bytes+0x20);put_pointer(pane,0x28,pane->bytes+0x20);
    put_pointer(pane,0xa0,local);put_float(pane,0x48,1);put_float(pane,0x4c,1);
    put_float(pane,0x50,200);put_float(pane,0x54,100);
    pane->bytes[0x58]=0x31;pane->bytes[0x59]=255;
}
static void attach(Pane *parent,Pane *child) {
    put_pointer(parent,0x20,child->bytes+8);put_pointer(parent,0x28,child->bytes+8);
    put_pointer(child,8,parent->bytes+0x20);put_pointer(child,0x10,parent->bytes+0x20);
}
static void raw_reference(Matrix out,float rx,float ry,float rz,float x,float y,float z,float sx,float sy) {
    const float radians=3.14159265358979323846f/180;
    rx*=radians;ry*=radians;rz*=radians;
    Matrix mx={1,0,0,0,0,cosf(rx),-sinf(rx),0,0,sinf(rx),cosf(rx),0};
    Matrix my={cosf(ry),0,sinf(ry),0,0,1,0,0,-sinf(ry),0,cosf(ry),0};
    Matrix mz={cosf(rz),-sinf(rz),0,0,sinf(rz),cosf(rz),0,0,0,0,1,0};
    Matrix scale={sx,0,0,0,0,sy,0,0,0,0,1,0},tmp,rotation;
    multiply(tmp,my,mx);multiply(rotation,mz,tmp);multiply(out,rotation,scale);
    out[3]=x;out[7]=y;out[11]=z;
}
static void restore_user(Pane *pane,const void *matrix,unsigned char old_flags) {
    put_pointer(pane,0xa0,matrix);
    pane->bytes[0x58]=(unsigned char)((pane->bytes[0x58]&~0x20u)|(old_flags&0x20u)|0x10u);
}
/* This body adapter isolates the receiving eui Build veneer/callback ABI. It
   deliberately does not emulate the SDK builder, allocator or asset loader. */
typedef uint64_t (*BuildEntry)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static Pane owner_root;
static Pane *adapter_root=&owner_root;
static uintptr_t owner_address;
static unsigned build_adapter_calls,delete_adapter_calls;
static uint64_t build_adapter(uintptr_t a0,uintptr_t a1,uintptr_t a2,uintptr_t a3,
                              uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7) {
    check_host_stack();
    assert(a0==owner_address && a1==0x1111222233334444 && a2==0x5555666677778888);
    assert(!memcmp((void *)a3,"FLYT",4) && a4==0x9999aaaabbbbcccc && a5==0xddddeeeeffff0001 && a6==0x2222333344445555);
    assert(a7==0x6666777788889990 || a7==0x6666777788889991);
    put_pointer((void *)a0,0x18,adapter_root);build_adapter_calls++;
    return a7&1;
}
static void delete_adapter(uintptr_t layout) {
    check_host_stack();assert(layout==owner_address);delete_adapter_calls++;
}
static void fixture_tail_jump(unsigned char *where,uintptr_t destination) {
    uint32_t words[]={0x58000050,0xd61f0200};
    memcpy(where,words,sizeof words);memcpy(where+8,&destination,8);
}
static unsigned char *read_file(const char *name,size_t *length) {
    int fd=open(name,O_RDONLY);assert(fd>=0);struct stat st;assert(!fstat(fd,&st));
    *length=(size_t)st.st_size;unsigned char *bytes=malloc(*length);assert(bytes);
    size_t done=0;while(done<*length){ssize_t n=read(fd,bytes+done,*length-done);assert(n>0);done+=(size_t)n;}
    close(fd);return bytes;
}
#ifdef FIXTURE_PAIRED
static Pane *activating_container,*appearing_picture;
static Pane *mode_change_container,*mode_change_aperture;
static Calculate common_calculate;
static Matrix capture_view;
static float capture_projection[16];
static unsigned capture_projection_calls;
static unsigned capture_default_calls;
static float capture_expected_width=320,capture_expected_height=180;
static unsigned char *capture_context;
static int capture_bad_basis;
/* The real derived capture methods run intact. Only their projection-service
   dependency is supplied here, since the CPU fixture has no camera allocator,
   renderer or GPU. This makes its separate coordinate domain observable. */
static void fixture_capture_projection(void *draw,float width,float height,float fov) {
    check_host_stack();assert(width==capture_expected_width && height==capture_expected_height && fov==45);
    memcpy((char *)draw+0x10,capture_projection,sizeof capture_projection);
    memcpy((char *)draw+0x50,capture_view,sizeof capture_view);capture_projection_calls++;
    if(capture_bad_basis)put_pointer(capture_context,8,identity);
}
static void fixture_capture_default(void *draw,float width,float height) {
    capture_default_calls++;fixture_capture_projection(draw,width,height,45);
}
static void fixture_append(Pane *parent,Pane *child) {
    uintptr_t last;memcpy(&last,parent->bytes+0x20,8);
    put_pointer(child,0x18,parent);put_pointer(child,8,(void *)last);put_pointer(child,0x10,parent->bytes+0x20);
    put_pointer((void *)last,8,child->bytes+8);put_pointer(parent,0x20,child->bytes+8);
}
static void fixture_activate(void *pane,void *draw,void *context,uint32_t force) {
    if(pane==activating_container && appearing_picture)appearing_picture->bytes[0x58]|=1;
    if(pane==mode_change_container && mode_change_aperture)put_float(mode_change_aperture,0x50,2250);
    common_calculate(pane,draw,context,force);
}
static unsigned fixture_layout(const char *name) {
    for(unsigned i=0;i<sizeof totk_live_layouts/sizeof totk_live_layouts[0];i++)if(!strcmp(totk_live_layouts[i].name,name))return i;
    assert(0);return 0;
}
static unsigned fixture_child(unsigned layout,const char *name) {
    const TotkLiveLayoutSpec *owner=&totk_live_layouts[layout];
    for(unsigned i=1;i<owner->pane_count;i++)if(!strcmp(totk_live_panes[owner->first_pane+i].name,name))return i;
    assert(0);return 0;
}
static Pane *fixture_tree(Pane *root,unsigned char *image,unsigned layout) {
    const TotkLiveLayoutSpec *owner=&totk_live_layouts[layout];
    Pane *children=calloc(owner->pane_count,sizeof *children);assert(children);
    for(unsigned i=1;i<owner->pane_count;i++) {
        const TotkLivePaneSpec *spec=&totk_live_panes[owner->first_pane+i];unsigned parent_index=spec->parent-owner->first_pane;
        Pane *parent=parent_index?&children[parent_index]:root;uint64_t table=0x40e9cc0;
        if(!strcmp(spec->kind,"pic1"))table=0x40ea1a0;
        else if(!strcmp(spec->kind,"txt1"))table=0x40e9e90;
        else if(!strcmp(spec->kind,"prt1"))table=0x40ea488;
        else if(!strcmp(spec->kind,"bnd1"))table=0x40ea338;
        else if(!strcmp(spec->kind,"wnd1"))table=0x40ea5d8;
        else if(!strcmp(spec->kind,"ali1"))table=0x44fbc98;
        else if(!strcmp(spec->kind,"scr1"))table=0x44fd2a0;
        pane_init(&children[i],image+table,parent,identity);children[i].bytes[0x58]=0x10;
        strcpy((char *)children[i].bytes+0xb0,spec->name);
        uintptr_t sentinel=(uintptr_t)parent+0x20,last;memcpy(&last,parent->bytes+0x20,8);
        put_pointer(&children[i],8,(void *)last);put_pointer(&children[i],0x10,(void *)sentinel);
        put_pointer((void *)last,8,children[i].bytes+8);put_pointer(parent,0x20,children[i].bytes+8);
    }
    return children;
}
/* Populate synthetic Pane poses directly from the hash-bound receiving FLYT
   section stream; do not restate overlay positions in the fixture builder. */
static void fixture_authored(Pane *root,Pane *children,unsigned layout,const unsigned char *data,size_t size) {
    const TotkLiveLayoutSpec *owner=&totk_live_layouts[layout];unsigned index=0;
    for(size_t at=20;at<size;) {
        uint32_t length;assert(size-at>=8);memcpy(&length,data+at+4,4);assert(length>=8 && length<=size-at);
        static const char *const kinds[]={"pan1","pic1","txt1","bnd1","prt1","wnd1","ali1","scr1"};int pane=0;
        for(unsigned k=0;k<sizeof kinds/sizeof kinds[0];k++)if(!memcmp(data+at,kinds[k],4))pane=1;
        if(pane) {
            assert(length>=84 && index<owner->pane_count);const TotkLivePaneSpec *spec=&totk_live_panes[owner->first_pane+index];
            char name[25];memcpy(name,data+at+12,24);name[24]=0;assert(!strcmp(name,spec->name));
            Pane *target=index?&children[index]:root;index++;
            memcpy(target->bytes+0x30,data+at+44,12);memcpy(target->bytes+0x3c,data+at+56,12);
            memcpy(target->bytes+0x48,data+at+68,8);memcpy(target->bytes+0x50,data+at+76,8);target->bytes[0x5b]=data[at+9];
        }
        at+=length;
    }
    assert(index==owner->pane_count);
}
static void fixture_visible_path(Pane *node,Pane *root) {
    for(unsigned depth=0;;depth++) {
        assert(depth<48);node->bytes[0x58]=0x11;
        if(node==root)break;uintptr_t parent;memcpy(&parent,node->bytes+0x18,8);assert(parent);node=(Pane *)parent;
    }
}
#endif
int main(int argc,char **argv) {
    assert(argc==15);struct sigaction trap={.sa_sigaction=fixture_trap,.sa_flags=SA_SIGINFO};
    sigemptyset(&trap.sa_mask);assert(!sigaction(SIGTRAP,&trap,NULL));
    size_t size,elf_size;unsigned char *raw=read_file(argv[1],&size),*elf=read_file(argv[2],&elf_size);
    size_t mapped=(size+0x1000000+0x3fff)&~(size_t)0x3fff;
    unsigned char *image=mmap(NULL,mapped,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(image!=MAP_FAILED);fixture_image_base=(uintptr_t)image;memcpy(image,raw,size);free(raw);
    size_t begin=(size_t)strtoull(argv[3],NULL,0)+4096,bytes=(size_t)strtoull(argv[4],NULL,0);
    assert(begin<=elf_size && bytes<=elf_size-begin && bytes%24==0);
    for(size_t at=begin;at<begin+bytes;at+=24) {
        uint64_t where,kind,value;memcpy(&where,elf+at,8);memcpy(&kind,elf+at+8,8);memcpy(&value,elf+at+16,8);
        if(kind!=1027)continue;
        assert(where<=mapped-8 && value<mapped);uintptr_t pointer=(uintptr_t)image+value;
        memcpy(image+where,&pointer,8);
    }
    free(elf);
    size_t sdk_size,binding_size;unsigned char *sdk=read_file(argv[6],&sdk_size),*bindings=read_file(argv[7],&binding_size);
    assert(binding_size==4*16);
    for(size_t at=0;at<binding_size;at+=16) {
        uint64_t where,value;memcpy(&where,bindings+at,8);memcpy(&value,bindings+at+8,8);
        assert(where<size-8 && value<sdk_size-4);put_pointer(image,where,sdk+value);
    }
    free(bindings);
    static const uint32_t calculate_guard[]={0xfc1b0fe8,0xa900fbfd,0x910023fd,0xf9000ff9};
    static const uint32_t local_guard[]={0xf9400c0b,0x9101c169,0xf9400428,0xf100017f};
    assert(!memcmp(image+0xc20fb8,calculate_guard,sizeof calculate_guard));
    assert(!memcmp(image+0x4770c,local_guard,sizeof local_guard));
    size_t host_bytes=1024*1024,guard=0x4000;
    unsigned char *host=mmap(NULL,host_bytes+2*guard,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(host!=MAP_FAILED && !mprotect(host+guard,host_bytes,PROT_READ|PROT_WRITE));
    host_low=(uintptr_t)host+guard;host_high=host_low+host_bytes;
    assert(!pthread_key_create(&hle_key,NULL) && hle_key);
    runtime_hle_stack_init((uint64_t)hle_key*8);runtime_hle_stack_set(host_high-64);
    assert(!setenv("TOTK_LIVE_DISPLAY_AUDIT",argv[5],1));
    Module module={.name="main",.base=image,.file_size=size,.size=mapped,.text_end=44722704};
#ifdef FIXTURE_PAIRED
    assert(!setenv("TOTK_ASPECT","4:3",1) && !setenv("TOTK_IPAD_ASPECT_PATCH","1",1) && !setenv("TOTK_LIVE_DISPLAY","1",1));
    const uint64_t camera_sites[]={0x1968c2c,0x1e5eae8};
    aspect_install(&module,"CPU fixture",camera_sites,2,(module.text_end+15)&~UINT64_C(15),(module.file_size+0x3fff)&~UINT64_C(0x3fff));
    /* Install the exact prepared viewport load pairs only in this CPU image.
       The complete real-game PCHTXT preparation is checked by the game probe. */
    const uint64_t viewport_sites[]={0x165b4,0x296b4,0x34b64,0x454c4,0x51204};
    put_float(image,0x16438,4.0f/3.0f);
    for(unsigned i=0;i<5;i++) {
        int64_t pages=(int64_t)(0x16438>>12)-(int64_t)(viewport_sites[i]>>12);
        uint32_t imm=(uint32_t)pages&0x1fffffu;
        uint32_t words[]={0x90000000u|((imm&3)<<29)|((imm>>2)<<5),0xbd443800u};
        memcpy(image+viewport_sites[i],words,sizeof words);
    }
#endif
#ifdef FIXTURE_PAIRED
    /* Prepare the same veneers exported by the Mac host, without exporting a
       host callback pointer. Then bind with executable pages read-only, as on
       iOS. Later fixture-only method stubs are installed in a separate phase. */
    const uint64_t owner_slots[]={0x40e9650,0x40eaf10,0x40e9618,0x40eaed8,0x40e95a0,0x40eae60,0x40e95a8,0x40eae68,0x44fbe88,0x44fbe50,0x44fbdd8,0x44fbde0};
    uintptr_t original_owners[12];
    for(unsigned i=0;i<12;i++)memcpy(&original_owners[i],image+owner_slots[i],8);
    assert(!setenv("TOTK_EXPORT_IOS","fixture-code-only",1));
    runtime_live_display_install(&module);
    assert(access(argv[5],F_OK)!=0);
    uint64_t extra=(module.file_size+0x3fff)&~UINT64_C(0x3fff),unbound;
    memcpy(&unbound,image+extra+72,8);assert(!unbound);
    memcpy(&unbound,image+extra+80,8);assert(!unbound);
    for(unsigned i=0;i<12;i++)assert(!memcmp(&original_owners[i],image+owner_slots[i],8));
    assert(!unsetenv("TOTK_EXPORT_IOS"));
    /* Missing/corrupt signed instructions fail before any callback is bound. */
    const uint64_t corrupt_sites[]={0xc20fb8,0x747f4c,0x165b4};
    for(unsigned i=0;i<3;i++) {
        pid_t child=fork();assert(child>=0);
        if(!child) {
            image[corrupt_sites[i]]^=1;expected_install_rejection=1;
            assert(!mprotect(image,0x2aa8000,PROT_READ|PROT_EXEC));
            runtime_live_display_install(&module);_exit(78);
        }
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77);
    }
    assert(!mprotect(image,0x2aa8000,PROT_READ|PROT_EXEC));
#endif
    runtime_live_display_install(&module);
#ifdef FIXTURE_PAIRED
    memcpy(&unbound,image+extra+72,8);assert(unbound);
    memcpy(&unbound,image+extra+80,8);assert(unbound);
    assert(!mprotect(image,0x2aa8000,PROT_READ|PROT_WRITE));
#endif
    /* Only this fixture replaces the original Build body after its displaced
       SUB SP instruction. Production installation already checked real bytes. */
    uint32_t undo_stack=0x910743ff;
    memcpy(image+0x747f50,&undo_stack,4);
    fixture_tail_jump(image+0x747f54,(uintptr_t)build_adapter);
    fixture_tail_jump(image+0x29f4594,(uintptr_t)delete_adapter);
#ifdef FIXTURE_PAIRED
    /* Synthetic leaves have no fonts/materials/draw resources. Dispatch them
       through the receiving common Calculate path under test, retaining SDK
       kind identities. Real derived rendering is qualified only in the game. */
    const uint64_t leaf_tables[]={0x40ea1a0,0x40e9e90,0x40ea338,0x40ea488,0x40ea5d8,0x40ea728,0x4110288};
    for(unsigned i=0;i<sizeof leaf_tables/sizeof leaf_tables[0];i++)put_pointer(image,leaf_tables[i]+0xd0,image+0xc20fb8);
    fixture_tail_jump(image+0x115ede0,(uintptr_t)runtime_hle_wrap_named((void *)fixture_capture_projection,"fixture capture projection service"));
    fixture_tail_jump(image+0x910de0,(uintptr_t)runtime_hle_wrap_named((void *)fixture_capture_default,"fixture capture default projection service"));
    fixture_tail_jump(image+0x2aa4260,(uintptr_t)runtime_hle_wrap_named((void *)memcpy,"fixture capture memcpy service"));
#endif
    assert(!mprotect(image,0x2aa8000,PROT_READ|PROT_EXEC));
    Calculate calculate=(Calculate)(void *)(image+0xc20fb8);Local local=(Local)(void *)(image+0x4770c);
    unsigned char context[64]={0},draw_info[0x200]={0};put_pointer(context,8,identity);put_float(context,0x18,1);
    unsigned negative_controls=0;
    for(unsigned iteration=0;iteration<240;iteration++) {
        Matrix root_local,child_local,grand_local,fill,fill_inverse,child_fill,child_inverse;
        pose(root_local,iteration*.013f,17,23,1.1f,.9f);
        pose(child_local,iteration*-.017f,11,-9,.8f,1.2f);
        pose(grand_local,iteration*.011f,-3,7,1,.7f);
        memcpy(fill,identity,sizeof fill);memcpy(fill_inverse,identity,sizeof fill_inverse);
        memcpy(child_fill,identity,sizeof child_fill);memcpy(child_inverse,identity,sizeof child_inverse);
        fill[5]=1.05f+iteration*.001f;fill_inverse[5]=1/fill[5];
        child_fill[0]=1.08f;child_inverse[0]=1/child_fill[0];
        Pane root,child,grand;
        pane_init(&root,image+0x40e9cc0,NULL,root_local);pane_init(&child,image+0x40e9cc0,&root,child_local);pane_init(&grand,image+0x40e9cc0,&child,grand_local);
        attach(&root,&child);attach(&child,&grand);
        calculate(&root,draw_info,context,1);
        Matrix expected,intermediate;multiply(intermediate,root_local,child_local);multiply(expected,intermediate,grand_local);
        equal((float *)(void *)(grand.bytes+0x70),expected);
        Matrix changed_root,changed_child,changed_grand,tmp;
        multiply(changed_root,root_local,fill);
        multiply(tmp,fill_inverse,child_local);multiply(changed_child,tmp,child_fill);
        multiply(changed_grand,child_inverse,grand_local);
        put_pointer(&root,0xa0,changed_root);put_pointer(&child,0xa0,changed_child);put_pointer(&grand,0xa0,changed_grand);
        calculate(&root,draw_info,context,1);
        equal((float *)(void *)(grand.bytes+0x70),expected);
        Matrix own_geometry;multiply(own_geometry,intermediate,child_fill);
        equal((float *)(void *)(child.bytes+0x70),own_geometry);
        unsigned char fields[0x28];memcpy(fields,root.bytes+0x30,sizeof fields);
        restore_user(&root,root_local,0x31);restore_user(&child,child_local,0x31);restore_user(&grand,grand_local,0x31);
        equal((float *)(void *)(root.bytes+0x70),changed_root);
        equal((float *)(void *)(grand.bytes+0x70),expected);
        assert(!memcmp(fields,root.bytes+0x30,sizeof fields));checks++;
        calculate(&root,draw_info,context,1);
        equal((float *)(void *)(grand.bytes+0x70),expected);
        Matrix wrong;multiply(tmp,changed_root,child_local);multiply(wrong,tmp,grand_local);
        int differs=0;for(unsigned n=0;n<12;n++)differs|=fabsf(wrong[n]-expected[n])>.001f;
        assert(differs);negative_controls++;
        // The original local-matrix routine's prefix is independently checked.
        // A copied parent with identity globals isolates the child's true local
        // transform while retaining its parent-relative origin dimensions.
        Pane local_pane,parent;
        pane_init(&parent,image+0x40e9cc0,NULL,identity);memcpy(parent.bytes+0x70,identity,sizeof identity);
        pane_init(&local_pane,image+0x40e9cc0,&parent,identity);
        put_float(&local_pane,0x30,13);put_float(&local_pane,0x34,-19);put_float(&local_pane,0x38,5);
        put_float(&local_pane,0x48,1.2f);put_float(&local_pane,0x4c,.8f);
        local(&local_pane,context);
        Matrix simple={1.2f,0,0,13,0,.8f,0,-19,0,0,1,5};
        equal((float *)(void *)(local_pane.bytes+0x70),simple);
        // Exercise the native Euler and parent-origin branches, including
        // context magnification. Parent dimensions and global transform vary
        // independently so an identity-only local extraction cannot pass.
        unsigned px=iteration%3,py=(iteration/3)%3;
        float width=180+(float)iteration,height=90+(float)(iteration%31);
        put_float(&parent,0x50,width);put_float(&parent,0x54,height);
        local_pane.bytes[0x5b]=(unsigned char)(px<<4|py<<6|0xf);
        float rx=(int)(iteration%71)-35,ry=(int)(iteration%59)-29,rz=(int)(iteration%83)-41;
        put_float(&local_pane,0x3c,rx);put_float(&local_pane,0x40,ry);put_float(&local_pane,0x44,rz);
        local_pane.bytes[0x58]=(unsigned char)(0x11|(iteration%2?4:0));
        context[0x1c]=1;put_float(context,0x10,1.15f);put_float(context,0x14,.85f);
        float mx=iteration%2?1.15f:1,my=iteration%2?.85f:1;
        float tx=13+(px==1?-width*.5f:px==2?width*.5f:0);
        float ty=-19+(py==1?height*.5f:py==2?-height*.5f:0);
        Matrix native_reference;raw_reference(native_reference,rx,ry,rz,tx,ty,5,1.2f*mx,.8f*my);
        local(&local_pane,context);
        equal((float *)(void *)(local_pane.bytes+0x70),native_reference);
        Matrix parent_world,world_reference;pose(parent_world,.37f,15,-21,1.3f,.7f);
        memcpy(parent.bytes+0x70,parent_world,sizeof parent_world);
        multiply(world_reference,parent_world,native_reference);
        calculate(&local_pane,draw_info,context,1);
        equal((float *)(void *)(local_pane.bytes+0x70),world_reference);
        context[0x1c]=0;
    }
    uint64_t callbacks=runtime_live_display_calculate_calls();assert(callbacks>=1440);
    unsigned char *guest=mmap(NULL,3*guard,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(guest!=MAP_FAILED && !mprotect(guest+guard,guard,PROT_READ|PROT_WRITE));
    memset(guest+guard,0xa5,guard);void *guest_top=guest+2*guard;
    unsigned sentinel_cases=0;
    for(unsigned mode=0;mode<3;mode++) {
        if(mode)runtime_hle_trace_set((int)mode);
        double (*wrapped)(uint64_t,uint64_t,uint64_t,uint64_t,double,double)=runtime_hle_wrap_named((void *)argument_probe,"fixture integer and floating arguments");
        double result=wrapped(0x111,0x222,0x333,0x444,1.25,-2.5);
        assert(result==1.25+2*-2.5+(0x111+3*0x222+5*0x333+7*0x444));
        assert(pthread_getspecific(hle_key)==(void *)(host_high-64));
        Pane root,child;pane_init(&root,image+0x40e9cc0,NULL,identity);pane_init(&child,image+0x40e9cc0,&root,identity);attach(&root,&child);
        assert(fixture_call_sentinels(calculate,&root,draw_info,context,1));
        assert(fixture_call_on_guest_stack(guest_top,calculate,&root,draw_info,context,1));
        assert(pthread_getspecific(hle_key)==(void *)(host_high-64));
        for(unsigned n=0;n<guard-4096;n++)assert(guest[guard+n]==0xa5);
        if(mode)runtime_hle_trace_set(0);
        sentinel_cases++;
    }
    unsigned owner_cases=0;
    unsigned char owner[0x60]={0},resource[128]={0};owner_address=(uintptr_t)owner;
    memcpy(resource,"FLYT\xff\xfe\x14\0",8);
    uint32_t version=0x09000000,total=sizeof resource,length=sizeof resource-20;
    memcpy(resource+8,&version,4);memcpy(resource+12,&total,4);
    memcpy(resource+20,"lyt1",4);memcpy(resource+24,&length,4);
    BuildEntry build_entry=(BuildEntry)(void *)(image+0x747f4c);
    void (*delete_entry)(uintptr_t);memcpy(&delete_entry,image+0x44fbde0,8);
    for(unsigned mode=0;mode<3;mode++) {
        if(mode)runtime_hle_trace_set((int)mode);
        for(unsigned iteration=0;iteration<3;iteration++) {
            const char *name=iteration%2?"Fixture_B_00":"Fixture_A_00";
            strcpy((char *)resource+48,name);
            uint64_t result=build_entry(owner_address,0x1111222233334444,0x5555666677778888,
                (uintptr_t)resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991);
            assert(result==1);char copied[96];
            runtime_live_display_owner_name(owner_address,(uintptr_t)&owner_root,copied);assert(!strcmp(copied,name));
            strcpy((char *)resource+48,"Reused_Buffer_00");
            runtime_live_display_owner_name(owner_address,(uintptr_t)&owner_root,copied);assert(!strcmp(copied,name));
            delete_entry(owner_address);
            runtime_live_display_owner_name(owner_address,(uintptr_t)&owner_root,copied);assert(!copied[0]);
            result=build_entry(owner_address,0x1111222233334444,0x5555666677778888,
                (uintptr_t)resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889990);
            assert(result==0);runtime_live_display_owner_name(owner_address,(uintptr_t)&owner_root,copied);assert(!copied[0]);
            assert(pthread_getspecific(hle_key)==(void *)(host_high-64));owner_cases++;
        }
        if(mode)runtime_hle_trace_set(0);
    }
    assert(build_adapter_calls==18 && delete_adapter_calls==9);
    unsigned paired_cases=0;
#ifdef FIXTURE_PAIRED
    assert(runtime_live_display_enabled());
    size_t player_size;unsigned char *player_resource=read_file(argv[8],&player_size);
    assert(player_size>128);
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)player_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    unsigned player_layout=UINT16_MAX;
    for(unsigned i=0;i<sizeof totk_live_layouts/sizeof totk_live_layouts[0];i++)if(!strcmp(totk_live_layouts[i].name,"PlayerStatus_00"))player_layout=i;
    assert(player_layout!=UINT16_MAX);
    const TotkLiveLayoutSpec *player_spec=&totk_live_layouts[player_layout];
    pane_init(&owner_root,image+0x40e9cc0,NULL,identity);
    strcpy((char *)owner_root.bytes+0xb0,"RootPane");owner_root.bytes[0x58]&=~0x20u;
    put_float(&owner_root,0x4c,.75f);put_float(&owner_root,0x34,135);
    Pane *player_children=fixture_tree(&owner_root,image,player_layout);
    put_pointer(draw_info,0xc0,owner);
    (void)runtime_live_display_finish_interval(90); /* discard earlier deliberately unowned synthetic matrix cases */
    const double dimensions[][2]={{951,669},{678,466},{951,669},{500,1000},{1600,900}};
    double insets[4]={0};uint64_t generation=0;
    for(unsigned d=0;d<5;d++) {
        assert(runtime_live_display_request(dimensions[d][0],dimensions[d][1],3,insets));
        (void)runtime_live_display_finish_interval(100+d);
        float aspect=(float)(dimensions[d][0]/dimensions[d][1]);
        assert(fabsf(runtime_aspect_now()-aspect)<1e-6f);
        uint64_t data=(module.file_size+0x3fff)&~UINT64_C(0x3fff);float viewport;
        memcpy(&viewport,image+data+60,4);assert(fabsf(viewport-(16.0f/9.0f)/aspect)<1e-6f);
        for(unsigned frame=0;frame<20;frame++) {
            calculate(&owner_root,draw_info,context,1);
            Matrix expected;memcpy(expected,identity,sizeof expected);
            expected[5]=aspect/(16.0f/9.0f);expected[7]=540-303.75f*aspect;
            equal((float *)(void *)(owner_root.bytes+0x70),expected);
            float restored;memcpy(&restored,owner_root.bytes+0x4c,4);assert(restored==.75f);
            memcpy(&restored,owner_root.bytes+0x34,4);assert(restored==135);paired_cases++;
        }
        assert(!runtime_live_display_request(NAN,900,3,insets));
        assert(!runtime_live_display_request(0,900,3,insets));
        TotkLiveSource source=runtime_live_display_finish_interval(200+d);
        assert(source.paired && source.hud_adjustments==20 && source.hud_misses==0 && source.generation>generation);
        assert(fabs(source.aspect-aspect)<1e-6);generation=source.generation;
    }
    assert(geometry_notifications==6);
    /* Receiving eui Alignment mutates child Y before its base Calculate call.
       The live child transaction must consume and preserve that updated pose. */
    unsigned char scroll_storage[sizeof(Pane)]={0};Pane *scroller=(Pane *)(void *)scroll_storage;
    pane_init(scroller,image+0x44fbc98,NULL,identity);scroller->bytes[0x58]&=~0x20u;
    strcpy((char *)scroller->bytes+0xb0,"OuterScroller");
    put_pointer(&owner_root,0x18,scroller);attach(scroller,&owner_root);
    put_float(scroll_storage,0xd4,17);put_float(scroll_storage,0xd8,0);scroll_storage[0xde]=1;
    Calculate scroll_calculate=(Calculate)(void *)(image+0xc20e98);
    scroll_calculate(scroller,draw_info,context,1);
    float updated;memcpy(&updated,owner_root.bytes+0x34,4);assert(updated==152);
    Matrix aligned;memcpy(aligned,identity,sizeof aligned);aligned[7]=17;
    equal((float *)(void *)(owner_root.bytes+0x70),aligned);
    /* Negative control: a matrix frozen before Alignment's update loses17. */
    put_float(&owner_root,0x34,135);put_float(scroll_storage,0xd8,0);
    Matrix frozen;memcpy(frozen,identity,sizeof frozen);
    put_pointer(&owner_root,0xa0,frozen);owner_root.bytes[0x58]|=0x30;
    scroll_calculate(scroller,draw_info,context,1);
    float lost;memcpy(&lost,owner_root.bytes+0x70+7*4,4);assert(fabsf(lost-17)>16);
    paired_cases++;
    /* Whole-plan failures must publish no dynamic source and atomically
       restore the prepared camera/UI pair for the next interval. */
    owner_root.bytes[0x58]=0x11;put_pointer(&owner_root,0x18,NULL);
    put_float(&owner_root,0x34,135);put_float(&owner_root,0x4c,.75f);
    void *original_table;memcpy(&original_table,player_children[1].bytes,8);
    put_pointer(&player_children[1],0,image+0x40ea1a0); /* required pan1 became pic1 */
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource rejected=runtime_live_display_finish_interval(300);
    assert(!rejected.valid && !rejected.paired && rejected.hud_misses);
    assert(fabsf(runtime_aspect_now()-4.0f/3.0f)<1e-6f);
    float fallback_ui;uint64_t pair_page=(module.file_size+0x3fff)&~UINT64_C(0x3fff);
    memcpy(&fallback_ui,image+pair_page+60,4);assert(fabsf(fallback_ui-4.0f/3.0f)<1e-6f);
    float untouched;memcpy(&untouched,owner_root.bytes+0x4c,4);assert(untouched==.75f);
    put_pointer(&player_children[1],0,original_table);
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource fallback=runtime_live_display_finish_interval(301);
    assert(fallback.valid && fallback.fallback && !fallback.paired);
    calculate(&owner_root,draw_info,context,1);
    assert(runtime_live_display_finish_interval(302).paired);
    strcpy((char *)owner_root.bytes+0xb0,"MissingRequiredRoot");
    calculate(&owner_root,draw_info,context,1);rejected=runtime_live_display_finish_interval(303);
    assert(!rejected.valid && rejected.hud_misses);
    strcpy((char *)owner_root.bytes+0xb0,"RootPane");
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(304);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(305).paired);
    /* Receiving Alignment/Scissor inherit pan1 RTTI, so source-kind admission
       must distinguish their independently verified eui Calculate classes. */
    Pane *guide_align=&player_children[fixture_child(player_layout,"A_GuideAlign_00")];
    put_pointer(guide_align,0,image+0x44fd2a0); /* Scissor cannot satisfy authored ali1 */
    calculate(&owner_root,draw_info,context,1);rejected=runtime_live_display_finish_interval(3051);
    assert(!rejected.valid && rejected.hud_misses);
    put_pointer(guide_align,0,image+0x44fbc98);
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(3052);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(3053).paired);paired_cases++;
    size_t map_size;unsigned char *map_resource=read_file(argv[9],&map_size);assert(map_size>128);
    unsigned part_index=0;
    for(unsigned i=1;i<player_spec->pane_count;i++)if(!strcmp(totk_live_panes[player_spec->first_pane+i].name,"Pa_HeartGauge_00"))part_index=i;
    assert(part_index);Pane *part=&player_children[part_index];part->bytes[0x58]=0x11;
    Pane *part_ancestors[48];unsigned part_ancestor_count=0;uintptr_t ancestor;
    memcpy(&ancestor,part->bytes+0x18,8);
    while(ancestor!=(uintptr_t)&owner_root) {
        assert(ancestor && part_ancestor_count<48);Pane *node=(Pane *)ancestor;
        part_ancestors[part_ancestor_count++]=node;node->bytes[0x58]=0x11;
        memcpy(&ancestor,node->bytes+0x18,8);
    }
    unsigned char other_owner[0x60]={0};uintptr_t saved_owner=owner_address;owner_address=(uintptr_t)other_owner;adapter_root=part;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)map_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    calculate(&owner_root,draw_info,context,1);rejected=runtime_live_display_finish_interval(306);
    assert(!rejected.valid && rejected.hud_misses); /* wrong explicit Parts resource */
    delete_entry(owner_address);owner_address=saved_owner;adapter_root=&owner_root;part->bytes[0x58]=0x10;
    for(unsigned i=0;i<part_ancestor_count;i++)part_ancestors[i]->bytes[0x58]=0x10;
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(307);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(308).paired);
    player_resource[player_size-1]^=1;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)player_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    calculate(&owner_root,draw_info,context,1);rejected=runtime_live_display_finish_interval(309);
    assert(!rejected.valid && rejected.hud_misses); /* same copied name, wrong asset bytes */
    player_resource[player_size-1]^=1;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)player_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(310);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(311).paired);
    double safe[4]={20,30,34,10};assert(runtime_live_display_request_scene(951,669,3,safe,3));
    (void)runtime_live_display_finish_interval(312);
    calculate(&owner_root,draw_info,context,1);
    Matrix safe_expected;memcpy(safe_expected,identity,sizeof safe_expected);
    safe_expected[5]=(951.0f/669)/(16.0f/9.0f);
    safe_expected[3]=1920.0f*30/951;
    safe_expected[7]=540-303.75f*(951.0f/669)-1080.0f*20/669;
    equal((float *)(void *)(owner_root.bytes+0x70),safe_expected);
    TotkLiveSource inset_source=runtime_live_display_finish_interval(313);
    assert(inset_source.valid && inset_source.paired && inset_source.orientation==3 && !memcmp(inset_source.safe_insets,safe,sizeof safe));
    uint64_t inset_generation=inset_source.generation;
    safe[0]=10;safe[1]=0;safe[2]=21;safe[3]=30;
    assert(runtime_live_display_request_scene(951,669,3,safe,4));
    TotkLiveSource old_insets=runtime_live_display_finish_interval(314);
    assert(old_insets.generation==inset_generation && old_insets.safe_insets[1]==30);
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource new_insets=runtime_live_display_finish_interval(315);
    assert(new_insets.generation>inset_generation && new_insets.orientation==4 && new_insets.safe_insets[1]==0);
    paired_cases+=6;
    /* Decode both endpoint states independently from the actual receiving
       prepared MiniMap animation, rather than mirroring admission constants.
       The in-world branch has N_MiniMap hidden and four visible clip owners. */
    size_t map_mode_bytes;float *map_modes=(float *)(void *)read_file(argv[12],&map_mode_bytes);assert(map_mode_bytes==40*sizeof(float));
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)map_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    pane_init(&owner_root,image+0x40e9cc0,NULL,identity);owner_root.bytes[0x58]=0x11;
    strcpy((char *)owner_root.bytes+0xb0,"RootPane");put_float(&owner_root,0x4c,.75f);
    unsigned map_layout=fixture_layout("AppMap_00");Pane *map_children=fixture_tree(&owner_root,image,map_layout);
    const char *map_names[]={"N_Cut_00","N_Cut_01","N_Cut_02","N_Cut_03","P_MapCapture_00"};Pane *map_nodes[5];
    const unsigned map_offsets[]={0x30,0x34,0x50,0x54};
    for(unsigned i=0;i<5;i++) {
        map_nodes[i]=&map_children[fixture_child(map_layout,map_names[i])];fixture_visible_path(map_nodes[i],&owner_root);
        if(i<4)put_pointer(map_nodes[i],0,image+0x44fd2a0); /* receiving eui scissor class, authored generic pane */
        for(unsigned f=0;f<4;f++)put_float(map_nodes[i],map_offsets[f],map_modes[20+i*4+f]);
    }
    assert(!(map_children[fixture_child(map_layout,"N_MiniMap_00")].bytes[0x58]&1));
    for(unsigned posture=0;posture<2;posture++) {
        double w=posture?678:951,h=posture?466:669,map_safe[4]={0,0,34,posture?84:30};
        assert(runtime_live_display_request_scene(w,h,3,map_safe,posture?4:3));(void)runtime_live_display_finish_interval(320+posture*2);
        calculate(&owner_root,draw_info,context,1);
        float a=(float)(w/h),root_y=a/(16.0f/9.0f),multiplier=-1.0f/3+(16.0f/9)/a;
        for(unsigned i=0;i<4;i++) {
            float x,y;memcpy(&x,map_nodes[i]->bytes+0x70+3*4,4);memcpy(&y,map_nodes[i]->bytes+0x70+7*4,4);
            assert(fabs(x-(765-1920*map_safe[3]/w))<.001 && fabs(y-(-540*multiplier*root_y+1080*34/h))<.001);
            memcpy(&x,map_nodes[i]->bytes+0x30,4);memcpy(&y,map_nodes[i]->bytes+0x34,4);assert(x==765 && y==-540);
        }
        TotkLiveSource map_source=runtime_live_display_finish_interval(321+posture*2);
        assert(map_source.paired && map_source.valid && !map_source.hud_misses && map_source.hud_adjustments==5);paired_cases++;
    }
    /* The endpoint changes inside the original Calculate traversal, after the
       source plan was admitted and transforms applied. Reject the completed
       interval, then publish the complete fallback and recover next source. */
    uintptr_t cut_parent;memcpy(&cut_parent,map_nodes[0]->bytes+0x18,8);
    mode_change_container=(Pane *)cut_parent;mode_change_aperture=map_nodes[0];common_calculate=calculate;
    void *mode_original_calculate;memcpy(&mode_original_calculate,image+0x40e9cc0+0xd0,8);
    put_pointer(image,0x40e9cc0+0xd0,(void *)fixture_activate);
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource changed_map=runtime_live_display_finish_interval(327);
    assert(!changed_map.valid && changed_map.hud_misses && changed_map.hud_adjustments==5);
    assert(fabsf(runtime_aspect_now()-4.0f/3.0f)<1e-6f);
    mode_change_container=NULL;mode_change_aperture=NULL;put_pointer(image,0x40e9cc0+0xd0,mode_original_calculate);
    put_float(map_nodes[0],0x50,map_modes[22]);
    calculate(&owner_root,draw_info,context,1);TotkLiveSource map_fallback=runtime_live_display_finish_interval(328);
    assert(map_fallback.valid && map_fallback.fallback && !map_fallback.paired);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(329).paired);paired_cases++;
    for(unsigned i=0;i<5;i++)for(unsigned f=0;f<4;f++)put_float(map_nodes[i],map_offsets[f],map_modes[i*4+f]);
    calculate(&owner_root,draw_info,context,1);TotkLiveSource fullmap_rejected=runtime_live_display_finish_interval(324);
    assert(!fullmap_rejected.valid && fullmap_rejected.hud_misses && !fullmap_rejected.hud_adjustments);
    assert(fabsf(runtime_aspect_now()-4.0f/3.0f)<1e-6); /* visible actual frame0 uses complete fallback */
    for(unsigned i=0;i<5;i++)for(unsigned f=0;f<4;f++)put_float(map_nodes[i],map_offsets[f],map_modes[20+i*4+f]);
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(325);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(326).paired);paired_cases++;
    free(map_children);free(map_modes);
    /* Positive owned Parts namespace: a primary PlayerStatus3D resource owns
       PaExtraStaminaGauge's aliased root, and both changed descendant Y fields. */
    size_t world_size,extra_size;unsigned char *world_resource=read_file(argv[10],&world_size),*extra_resource=read_file(argv[11],&extra_size);
    assert(world_size>128 && extra_size>128);
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)world_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    pane_init(&owner_root,image+0x40e9cc0,NULL,identity);owner_root.bytes[0x58]=0x11;
    strcpy((char *)owner_root.bytes+0xb0,"RootPane");put_float(&owner_root,0x4c,.75f);
    unsigned world_layout=fixture_layout("PlayerStatus3D_00"),extra_layout=fixture_layout("PaExtraStaminaGauge_00");
    Pane *world_children=fixture_tree(&owner_root,image,world_layout);
    Pane *extra_root=&world_children[fixture_child(world_layout,"Pa_ExtraStaminaGauge_00")];
    fixture_visible_path(extra_root,&owner_root);
    Pane *extra_children=fixture_tree(extra_root,image,extra_layout);
    unsigned base_pattern=fixture_child(extra_layout,"N_BasePattern_00"),gauge_pattern=fixture_child(extra_layout,"N_GaugeMainPattern_00");
    fixture_visible_path(&extra_children[base_pattern],extra_root);fixture_visible_path(&extra_children[gauge_pattern],extra_root);
    saved_owner=owner_address;owner_address=(uintptr_t)other_owner;adapter_root=extra_root;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)extra_resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    owner_address=saved_owner;adapter_root=&owner_root;
    double no_insets[4]={0};assert(runtime_live_display_request_scene(951,669,3,no_insets,3));
    (void)runtime_live_display_finish_interval(316);
    calculate(&owner_root,draw_info,context,1);
    float expected_shift=(float)((951.0/669)/(16.0/9.0)*(-720+960/(951.0/669)));
    for(unsigned i=0;i<2;i++) {
        Pane *pattern=&extra_children[i?gauge_pattern:base_pattern];float baked,local_y;
        memcpy(&baked,pattern->bytes+0x70+7*4,4);memcpy(&local_y,pattern->bytes+0x34,4);
        assert(fabsf(baked-expected_shift)<.001 && local_y==0);
    }
    float alias_scale;memcpy(&alias_scale,extra_root->bytes+0x4c,4);assert(alias_scale==1); /* no nested RootPane double scale */
    TotkLiveSource parts_source=runtime_live_display_finish_interval(317);
    assert(parts_source.valid && parts_source.paired && !parts_source.hud_misses && parts_source.hud_adjustments==3);
    paired_cases++;
    /* Both real eui capture methods install their own projection/view, copy
       it to GlobalMtx, recurse into text/circle/scissor and owned Parts, then
       restore DrawInfo. Descendant aspect rules and safe offsets must remain
       suspended even though the outer world root continues to adapt. */
    Pane *capture=&world_children[fixture_child(world_layout,"N_ExtraStaminaGauge_00")];
    Pane clip,text,circle;
    pane_init(&clip,image+0x44fd2a0,capture,identity);clip.bytes[0x58]=0x11;
    pane_init(&text,image+0x40e9e90,&clip,identity);text.bytes[0x58]=0x11;
    pane_init(&circle,image+0x40ea1a0,&clip,identity);circle.bytes[0x58]=0x11;
    strcpy((char *)clip.bytes+0xb0,"FixtureCaptureClip");strcpy((char *)text.bytes+0xb0,"FixtureCaptureText");strcpy((char *)circle.bytes+0xb0,"FixtureCaptureCircle");
    put_float(&clip,0x50,240);put_float(&clip,0x54,120);
    put_float(&text,0x30,25);put_float(&text,0x34,-7);put_float(&circle,0x30,-20);
    put_float(&circle,0x50,32);put_float(&circle,0x54,32);
    clip.bytes[0x59]=227;text.bytes[0x59]=155;circle.bytes[0x59]=177;
    fixture_append(capture,&clip);fixture_append(&clip,&text);fixture_append(&clip,&circle);
    pose(capture_view,.2f,17,-13,1.125f,1.125f);
    Matrix capture_frozen;pose(capture_frozen,-.15f,9,-4,1.03f,1.03f);
    /* This native component has an SDK-unknown widget class. Main-scene Parts
       still require full identity/path checks, while a proven capture keeps
       its whole component opaque and leaves every authored local untouched. */
    unsigned char opaque_widget_table[0xe0]={0};put_pointer(opaque_widget_table,0xd0,image+0xc20fb8);
    put_pointer(&extra_children[base_pattern],0,opaque_widget_table);
    memset(capture_projection,0,sizeof capture_projection);
    capture_projection[0]=2.0f/320;capture_projection[5]=2.0f/180;capture_projection[10]=1;capture_projection[15]=1;
    capture_context=context;put_pointer(context,8,draw_info+0x50);context[0x1f]=1;
    const uint64_t capture_tables[]={0x44fc050,0x44fbee0};
    unsigned char alpha_reference[3]={0};
    for(unsigned projection=0;projection<2;projection++)for(unsigned kind=0;kind<2;kind++)for(unsigned posture=0;posture<2;posture++) {
        put_pointer(capture,0,image+capture_tables[kind]);capture->bytes[0x58]=projection?0x11:0x51;
        memcpy(capture->bytes+0x70,capture_frozen,sizeof capture_frozen);
        capture->bytes[0x59]=201;capture->bytes[0xe9]=(unsigned char)projection;capture->bytes[0xd3]=(unsigned char)projection;capture->bytes[0xd4]=1;
        put_float(capture,0x50,320);put_float(capture,0x54,180);put_float(capture,0xec,45);put_float(capture,0xd8,45);
        double safe_capture[4]={20,30,34,10};double w=posture?678:951,h=posture?466:669;
        assert(runtime_live_display_request_scene(w,h,3,safe_capture,posture?4:3));
        (void)runtime_live_display_finish_interval(330+kind*10+posture*2);
        float old_projection[16]={0};old_projection[0]=2;old_projection[5]=3;old_projection[10]=4;old_projection[15]=1;
        memcpy(draw_info+0x10,old_projection,sizeof old_projection);memcpy(draw_info+0x50,identity,sizeof identity);
        calculate(&owner_root,draw_info,context,1);
        const float *expected_view=projection?capture_view:capture_frozen;
        assert(capture->bytes[0x58]&0x40);equal((float *)(void *)(capture->bytes+0x70),expected_view);
        assert(!memcmp(draw_info+0x10,old_projection,sizeof old_projection) && !memcmp(draw_info+0x50,identity,sizeof identity) && context[0x1f]==1);
        for(unsigned i=0;i<2;i++) {
            Pane *pattern=&extra_children[i?gauge_pattern:base_pattern];
            equal((float *)(void *)(pattern->bytes+0x70),expected_view); /* descendant rule deliberately suspended */
            float local_y;memcpy(&local_y,pattern->bytes+0x34,4);assert(local_y==0);
        }
        equal((float *)(void *)(clip.bytes+0x70),expected_view);
        Matrix text_local,circle_local,expected_text,expected_circle;
        memcpy(text_local,identity,sizeof identity);text_local[3]=25;text_local[7]=-7;
        memcpy(circle_local,identity,sizeof identity);circle_local[3]=-20;
        multiply(expected_text,expected_view,text_local);multiply(expected_circle,expected_view,circle_local);
        equal((float *)(void *)(text.bytes+0x70),expected_text);equal((float *)(void *)(circle.bytes+0x70),expected_circle);
        const float *m=(const float *)(void *)(circle.bytes+0x70);
        assert(fabsf(hypotf(m[0],m[4])-hypotf(m[1],m[5]))<1e-6f); /* circle stays round */
        unsigned char alpha[]={clip.bytes[0x5a],text.bytes[0x5a],circle.bytes[0x5a]};
        if(!projection && !kind && !posture)memcpy(alpha_reference,alpha,sizeof alpha);else assert(!memcmp(alpha_reference,alpha,sizeof alpha));
        float clip_width,clip_height;memcpy(&clip_width,clip.bytes+0x50,4);memcpy(&clip_height,clip.bytes+0x54,4);assert(clip_width==240 && clip_height==120);
        TotkLiveSource capture_source=runtime_live_display_finish_interval(331+kind*10+posture*2);
        assert(capture_source.valid && capture_source.paired && !capture_source.hud_misses && capture_source.hud_adjustments==1);paired_cases++;
    }
    assert(capture_projection_calls==8 && capture_default_calls==4);
    capture->bytes[0xe9]=0;capture->bytes[0xd3]=0;capture->bytes[0x58]=0x11;
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource unisolated=runtime_live_display_finish_interval(350);assert(!unisolated.valid && unisolated.hud_misses);
    capture->bytes[0x58]=0x51;memcpy(capture->bytes+0x70,capture_frozen,sizeof capture_frozen);
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(351);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(352).paired);paired_cases++;
    /* A capture-class pointer alone cannot admit a mismatched context. */
    capture_bad_basis=1;calculate(&owner_root,draw_info,context,1);
    TotkLiveSource capture_rejected=runtime_live_display_finish_interval(360);assert(!capture_rejected.valid && capture_rejected.hud_misses);
    capture_bad_basis=0;put_pointer(context,8,draw_info+0x50);
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(361);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(362).paired);
    owner_root.bytes[0x58]|=0x40;calculate(&owner_root,draw_info,context,1);
    TotkLiveSource outside_absolute=runtime_live_display_finish_interval(3621);assert(!outside_absolute.valid && outside_absolute.hud_misses);
    owner_root.bytes[0x58]&=~0x40u;
    calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(3622);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(3623).paired);paired_cases++;
    /* An unrecognized absolute/global matrix still rejects the whole source. */
    clip.bytes[0x58]|=0x40;calculate(&owner_root,draw_info,context,1);
    capture_rejected=runtime_live_display_finish_interval(363);assert(!capture_rejected.valid && capture_rejected.hud_misses);
    clip.bytes[0x58]&=~0x40u;calculate(&owner_root,draw_info,context,1);(void)runtime_live_display_finish_interval(364);
    calculate(&owner_root,draw_info,context,1);assert(runtime_live_display_finish_interval(365).paired);paired_cases+=2;
    put_pointer(context,8,identity);context[0x1f]=0;
    /* An unknown owner is harmless only while its known containers draw no
       geometry; activation during Calculate must still poison the source. */
    strcpy((char *)resource+48,"Fixture_Empty_00");
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)resource,0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    pane_init(&owner_root,image+0x40e9cc0,NULL,identity);owner_root.bytes[0x58]=0x11;
    strcpy((char *)owner_root.bytes+0xb0,"RootPane");
    Pane empty_container,hidden_picture;
    pane_init(&empty_container,image+0x40e9cc0,&owner_root,identity);empty_container.bytes[0x58]=0x11;
    pane_init(&hidden_picture,image+0x40ea1a0,&empty_container,identity);hidden_picture.bytes[0x58]=0x10;
    strcpy((char *)empty_container.bytes+0xb0,"EmptyContainer");strcpy((char *)hidden_picture.bytes+0xb0,"HiddenPicture");
    attach(&owner_root,&empty_container);attach(&empty_container,&hidden_picture);
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource empty_source=runtime_live_display_finish_interval(318);assert(empty_source.valid && empty_source.paired && !empty_source.hud_misses);
    activating_container=&empty_container;appearing_picture=&hidden_picture;common_calculate=calculate;
    void *previous_calculate;memcpy(&previous_calculate,image+0x40e9cc0+0xd0,8);
    put_pointer(image,0x40e9cc0+0xd0,(void *)fixture_activate);
    calculate(&owner_root,draw_info,context,1);
    TotkLiveSource appeared_source=runtime_live_display_finish_interval(319);
    assert(!appeared_source.valid && appeared_source.hud_misses);
    put_pointer(image,0x40e9cc0+0xd0,previous_calculate);paired_cases+=2;
    /* Ordinary movement hints and autosave are simultaneously visible. Their
       exact resources provide bottom-center and bottom-left outer geometry;
       save capture internals retain their independent native GlobalMtx. */
    Pane overlay_roots[2],*overlay_children[2];unsigned char overlay_owners[2][0x60]={{0}};
    unsigned overlay_layouts[2]={fixture_layout("CtrlGuide_00"),fixture_layout("SaveLoadIcon_00")};
    size_t overlay_sizes[2];unsigned char *overlay_resources[2]={read_file(argv[13],&overlay_sizes[0]),read_file(argv[14],&overlay_sizes[1])};
    saved_owner=owner_address;
    for(unsigned i=0;i<2;i++) {
        owner_address=(uintptr_t)overlay_owners[i];adapter_root=&overlay_roots[i];
        assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
            (uintptr_t)overlay_resources[i],0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
        pane_init(&overlay_roots[i],image+0x40e9cc0,NULL,identity);strcpy((char *)overlay_roots[i].bytes+0xb0,"RootPane");overlay_roots[i].bytes[0x58]=0x11;
        overlay_children[i]=fixture_tree(&overlay_roots[i],image,overlay_layouts[i]);
        fixture_authored(&overlay_roots[i],overlay_children[i],overlay_layouts[i],overlay_resources[i],overlay_sizes[i]);
        for(unsigned p=1;p<totk_live_layouts[overlay_layouts[i]].pane_count;p++)fixture_visible_path(&overlay_children[i][p],&overlay_roots[i]);
    }
    Pane *save_capture=&overlay_children[1][fixture_child(overlay_layouts[1],"C_Capture_00")];
    put_pointer(save_capture,0,image+0x44fc050);save_capture->bytes[0xe9]=0;save_capture->bytes[0x58]=0x51;
    memcpy(save_capture->bytes+0x70,identity,sizeof identity);capture_expected_width=360;capture_expected_height=69;
    Pane *save_text_capture=&overlay_children[1][fixture_child(overlay_layouts[1],"T_TextCapture_00")];
    put_pointer(context,8,draw_info+0x50);memcpy(draw_info+0x50,identity,sizeof identity);
    for(unsigned posture=0;posture<2;posture++) {
        double w=posture?678:951,h=posture?466:669,overlay_safe[4]={posture?0:20,posture?0:30,34,posture?84:10};
        assert(runtime_live_display_request_scene(w,h,3,overlay_safe,posture?4:3));(void)runtime_live_display_finish_interval(400+posture*2);
        for(unsigned i=0;i<2;i++) {
            put_pointer(draw_info,0xc0,overlay_owners[i]);calculate(&overlay_roots[i],draw_info,context,1);
            float x,y,sy;memcpy(&x,overlay_roots[i].bytes+0x70+3*4,4);memcpy(&y,overlay_roots[i].bytes+0x70+7*4,4);memcpy(&sy,overlay_roots[i].bytes+0x70+5*4,4);
            double dx=i?overlay_safe[1]:(overlay_safe[1]-overlay_safe[3])*.5;
            assert(fabs(x-dx*1920/w)<.001 && fabs(y-(-540+303.75*w/h+overlay_safe[2]*1080/h))<.001);
            assert(fabs(sy-(w/h)/(16.0/9))<1e-6);
            float local_x,local_y;memcpy(&local_x,overlay_roots[i].bytes+0x30,4);memcpy(&local_y,overlay_roots[i].bytes+0x34,4);assert(local_x==0 && local_y==-135);
        }
        equal((float *)(void *)(save_capture->bytes+0x70),identity);
        float raw_x,raw_y,world_x,world_y;memcpy(&raw_x,save_text_capture->bytes+0x30,4);memcpy(&raw_y,save_text_capture->bytes+0x34,4);
        memcpy(&world_x,save_text_capture->bytes+0x70+3*4,4);memcpy(&world_y,save_text_capture->bytes+0x70+7*4,4);assert(world_x==raw_x && world_y==raw_y);
        TotkLiveSource overlay_source=runtime_live_display_finish_interval(401+posture*2);
        assert(overlay_source.valid && overlay_source.paired && !overlay_source.hud_misses && overlay_source.hud_adjustments==2);paired_cases++;
    }
    owner_address=(uintptr_t)overlay_owners[1];adapter_root=&overlay_roots[1];overlay_resources[1][overlay_sizes[1]-1]^=1;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)overlay_resources[1],0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    for(unsigned i=0;i<2;i++){put_pointer(draw_info,0xc0,overlay_owners[i]);calculate(&overlay_roots[i],draw_info,context,1);}
    TotkLiveSource bad_overlay=runtime_live_display_finish_interval(404);assert(!bad_overlay.valid && bad_overlay.hud_misses);
    overlay_resources[1][overlay_sizes[1]-1]^=1;
    assert(build_entry(owner_address,0x1111222233334444,0x5555666677778888,
        (uintptr_t)overlay_resources[1],0x9999aaaabbbbcccc,0xddddeeeeffff0001,0x2222333344445555,0x6666777788889991)==1);
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned i=0;i<2;i++){put_pointer(draw_info,0xc0,overlay_owners[i]);calculate(&overlay_roots[i],draw_info,context,1);}
        TotkLiveSource recovered_overlay=runtime_live_display_finish_interval(405+pass);if(pass)assert(recovered_overlay.valid && recovered_overlay.paired && !recovered_overlay.hud_misses);
    }
    paired_cases++;
    /* UIKit can move a narrow build outside its prepared range. Publish a
       paired baseline fallback with the real scene identity; never retain the
       last native aspect or briefly stretch its completed source. */
    double shipping_safe[4]={0,0,34,0};
    assert(runtime_live_display_request_scene_mode(932,430,3,shipping_safe,3,1));
    (void)runtime_live_display_finish_interval(410);
    TotkLiveSource unsupported=runtime_live_display_finish_interval(411);
    assert(unsupported.valid && unsupported.fallback && !unsupported.paired && fabs(unsupported.aspect-4.0/3.0)<1e-7);
    assert(unsupported.width==932 && unsupported.height==430 && unsupported.orientation==3);
    uint64_t fallback_request=unsupported.requested_generation;
    assert(!runtime_live_display_request_scene_mode(0,430,3,shipping_safe,3,1));
    assert(runtime_live_display_finish_interval(412).requested_generation==fallback_request);
    assert(runtime_live_display_request_scene_mode(951,669,3,shipping_safe,4,0));
    (void)runtime_live_display_finish_interval(413);
    TotkLiveSource supported=runtime_live_display_finish_interval(414);
    assert(supported.valid && supported.paired && !supported.fallback && fabs(supported.aspect-951.0/669)<1e-7);
    assert(supported.requested_generation==fallback_request+1 && supported.orientation==4);paired_cases++;
    for(unsigned i=0;i<2;i++){owner_address=(uintptr_t)overlay_owners[i];delete_entry(owner_address);free(overlay_children[i]);free(overlay_resources[i]);}
    owner_address=saved_owner;adapter_root=&owner_root;
    free(extra_children);free(world_children);free(extra_resource);free(world_resource);
    free(map_resource);free(player_children);free(player_resource);
#endif
    assert(atomic_load(&hle_notes)>0);
    callbacks=runtime_live_display_calculate_calls();
    printf("{\"status\":\"pass\",\"scope\":\"exact TOTK1.0.0 CPU-only Pane Calculate/local matrix ABI and actual entry/HLE wrappers\",\"checks\":%u,\"negative_controls\":%u,\"entry_callbacks\":%llu,\"host_stack_trace_sentinel_cases\":%u,\"eui_build_owner_abi_cases\":%u,\"paired_transform_cases\":%u,\"gpu\":false,\"gameplay\":false}\n",checks,negative_controls,(unsigned long long)callbacks,sentinel_cases,owner_cases,paired_cases);
    assert(!pthread_setspecific(hle_key,NULL));assert(!pthread_key_delete(hle_key));
    assert(!munmap(guest,3*guard));assert(!munmap(host,host_bytes+2*guard));
    assert(!munmap(image,mapped));free(sdk);return 0;
}
