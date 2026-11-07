// SPDX-License-Identifier: GPL-3.0-only
#include "brawl_menu_touch.h"

#include "abi_bridge.h"
#include "save_states.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

extern "C" float AuroraProjectionHorizontalScale(float, float, float, float);

namespace BrawlMenuTouch {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kHandMain = 0x8069D2F8;
constexpr uint32_t kHandCollision = 0x8069DB68;
constexpr uint32_t kPortraitCollision = 0x806913B8;
constexpr uint32_t kProject = 0x80019A2C;
constexpr uint32_t kSelect = 0x806A50F4;
constexpr uint32_t kPadUpdate = 0x806A4E04;
constexpr uint32_t kStagePoint = 0x806B626C;
constexpr uint32_t kPauseInput = 0x800F14C0;
constexpr uint32_t kResultInput = 0x800EB684;
constexpr uint32_t kNoticeUpdate = 0x800BF78C;
constexpr uint16_t kA = 0x0100;
enum class Surface { None, Css, Menu, Stage, Pause, Result, Notice, Movie, MovieList, Names, NameEntry, Title, Rules, NameDelete, ItemSwitch, Options };
struct ScreenKey {
    uint32_t manager = 0, scene = 0, hand = 0, task = 0, phase = 0, mode = 0;
    uint32_t scroll = 0;
    Surface surface = Surface::None;
    uint32_t ownerSlot = 0;
    uint32_t panelDepth = 0;
    bool operator==(const ScreenKey& b) const {
        return manager == b.manager && scene == b.scene && hand == b.hand && task == b.task &&
               phase == b.phase && mode == b.mode && scroll == b.scroll && surface == b.surface && ownerSlot == b.ownerSlot && panelDepth == b.panelDepth;
    }
};
struct Tap { float x, y; uint64_t epoch; };
struct Navigation { uint64_t epoch; uint32_t proc; uint16_t target; unsigned remaining; };
std::mutex mutex;
ScreenKey published;
uint64_t epoch = 1;
bool ready = false;
std::optional<Tap> pending;
std::optional<Navigation> navigation;
struct NoticeNavigation {
    uint64_t epoch;
    uint32_t task;
    unsigned target;
    bool directionSent;
    Clock::time_point expires;
};
std::optional<NoticeNavigation> noticeNavigation;
struct ItemNavigation {
    uint64_t epoch;
    uint32_t proc;
    unsigned target, lastPosition, remaining;
    bool waiting;
    Clock::time_point expires;
};
std::optional<ItemNavigation> itemNavigation;
uint16_t padPulse = 0;
uint16_t navigationButtons=0;
void ResetItemNavigation() { itemNavigation.reset();navigationButtons=0; }
struct PointerContact {
    unsigned id=0;
    float x=0,y=0,lastX=0,lastY=0,downX=0,downY=0;
    uint16_t mode=0;
    bool active=false;
    uint64_t serial=0;
};
uint64_t pointerSerial=0;
std::array<PointerContact,2> pointerContacts{};
std::array<int8_t,4> pointerAxes{};
uint16_t pointerButtons=0;
void ClearPointersLocked() {
    pointerContacts={};pointerAxes={};pointerButtons=0;
}
Clock::time_point lastObservation{};
thread_local bool inspecting = false;
// Game-thread-only, bounded diagnostics for the next CSS observations after a tap.
uint32_t cssTraceHand=0;
unsigned cssTraceRemaining=0;

void InvalidateLocked() {
    if(ready)RT_LOGF(RT_TAG_RUNTIME,"[menu touch] invalidated surface=%u scene=%08x owner=%08x phase=%u\n",
        static_cast<unsigned>(published.surface),published.scene,published.hand,published.phase);
    ++epoch;
    pending.reset();
    navigation.reset();
    noticeNavigation.reset();
    ResetItemNavigation();
    padPulse = 0;
    ClearPointersLocked();
    ready = false;
}

void PublishLocked(const ScreenKey& key) {
    if(!(published==key)){InvalidateLocked();published=key;}
    if(!ready)RT_LOGF(RT_TAG_RUNTIME,"[menu touch] ready surface=%u scene=%08x owner=%08x task=%08x phase=%u page=%u\n",
        static_cast<unsigned>(key.surface),key.scene,key.hand,key.task,key.phase,key.mode);
    ready=true;
    lastObservation=Clock::now();
}

bool FreshLocked() {
    return ready && Clock::now() - lastObservation < std::chrono::milliseconds(250);
}

// Run an ordinary guest function with a private CPU context and stack frame.
// The observer is below the guest ABI entry, so neither volatile registers nor
// LR/CR/FPSCR in the interrupted function may be modified by these probes.
class Probe {
public:
    explicit Probe(const CpuContext& original) : cpu(original) {
        if (cpu.gpr[1] < 0x80001000u ||
            !Memory::Contains(cpu.gpr[1] - 0x1000, 0x1000)) return;
        scratch = cpu.gpr[1] - 0x100;
        cpu.gpr[1] = scratch - 0x100;
        std::memcpy(saved.data(), Memory::GetPointer(scratch, saved.size()), saved.size());
    }
    ~Probe() {
        if (scratch) std::memcpy(Memory::GetPointer(scratch, saved.size()), saved.data(), saved.size());
    }
    uint32_t Call(uint32_t target, uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
        cpu.gpr[3] = a; cpu.gpr[4] = b; cpu.gpr[5] = c;
        cpu.gpr[6] = d;
        CpuContextScope scope(&cpu);
        InvokeIndirectCpu(target, &cpu);
        return cpu.gpr[3];
    }
    bool Project(uint32_t camera, double x, double y, float z, Detail::Point& out) {
        Memory::WriteFloat32(scratch, x);
        Memory::WriteFloat32(scratch + 4, y);
        Memory::WriteFloat32(scratch + 8, z);
        Call(kProject, camera, scratch, scratch + 16);
        out = {Memory::ReadFloat32(scratch + 16), Memory::ReadFloat32(scratch + 20)};
        const float width=Memory::ReadFloat32(camera+0x11C), height=Memory::ReadFloat32(camera+0x120);
        float aspect=Memory::ReadFloat32(camera+0xE4);
        if(Memory::Read32(camera+0x100)!=1) {
            const float top=Memory::ReadFloat32(cpu.gpr[2]-0x7EDC), bottom=Memory::ReadFloat32(cpu.gpr[2]-0x7ED8);
            const float left=Memory::ReadFloat32(cpu.gpr[2]-0x7ED4), right=Memory::ReadFloat32(cpu.gpr[2]-0x7ED0);
            aspect=(right-left)/(top-bottom);
        }
        if(!std::isfinite(aspect)||aspect<=0)return false;
        // Match Aurora's post-guest Hor+ projection around the viewport center.
        const float scale=AuroraProjectionHorizontalScale(1/aspect,1,width,height);
        out.x=Detail::WidenScreenX(out.x,width*.5,scale);
        return std::isfinite(out.x) && std::isfinite(out.y);
    }
    CpuContext cpu;
    uint32_t scratch = 0;
    std::array<uint8_t, 64> saved{};
};

std::string GuestString(uint32_t p) {
    if (!Memory::Contains(p, 96)) return {};
    const char* s = reinterpret_cast<const char*>(Memory::GetPointer(p, 96));
    return std::string(s, strnlen(s, 96));
}
std::string ProcName(uint32_t proc) {
    if(!Memory::Contains(proc,0x66C))return {};
    const uint32_t vt=Memory::Read32(proc+0x668);
    if(!Memory::Contains(vt,4))return {};
    const uint32_t rtti=Memory::Read32(vt);
    return Memory::Contains(rtti,4) ? GuestString(Memory::Read32(rtti)) : std::string{};
}

uint32_t ActiveMainProc(uint32_t scene) {
    if(!Memory::Contains(scene,0xABC)||GuestString(Memory::Read32(scene))!="muMenuMain"||
       static_cast<int32_t>(Memory::Read32(scene+0xAB8))>0)return 0;
    const unsigned index=Memory::Read32(scene+0x3D0);
    if(index>=0x22||index==0x1E)return 0;
    return Memory::Read32(scene+0x35C+index*4);
}

uint32_t Group(uint32_t mdl, uint32_t offset) {
    const uint32_t relative = Memory::Read32(mdl + offset);
    if (!relative || relative > 0x1000000) return 0;
    const uint32_t group = mdl + relative;
    if (!Memory::Contains(group, 24)) return 0;
    const uint32_t count = Memory::Read32(group + 4);
    if (count > 1024 || !Memory::Contains(group, 24 + 16*count)) return 0;
    return group;
}
uint32_t Entry(uint32_t group, unsigned i) {
    if (!group || i >= Memory::Read32(group + 4)) return 0;
    const uint32_t result = group + Memory::Read32(group + 24 + i*16 + 12);
    return Memory::Contains(result, 0x40) ? result : 0;
}
struct Mesh {
    uint32_t model = 0, bone = 0;
    uint32_t boneFlags = 0;
    bool worldSpace = false;
    std::vector<std::array<float,3>> vertices;
    std::vector<std::array<unsigned,3>> triangles;
};

struct MeshRegion {
    const char* vertexArray;
    float left,top,right,bottom;
};

// MDL0 v9: parse the actual base mesh of MenMainIcon, including its GX
// primitive topology. The rounded/slanted transparent corners consequently
// do not activate a button, unlike a bounding rectangle or a convex hull.
bool ObjectMesh(uint32_t object, Mesh& mesh, const char* modelPrefix, const char* boneName, const MeshRegion* region=nullptr) {
    if (!Memory::Contains(object, 0x60)) return false;
    mesh.model = Memory::Read32(object + 0x10);
    if (!Memory::Contains(mesh.model, 0xEC)) return false;
    const uint32_t mdl = Memory::Read32(mesh.model + 0xE8);
    if (!Memory::Contains(mdl, 0x40) || Memory::Read32(mdl) != 0x4D444C30 ||
        Memory::Read32(mdl+8) != 9) return false;
    const auto modelName = GuestString(mdl + Memory::Read32(mdl + 0x3C));
    if (modelName.rfind(modelPrefix,0) != 0) return false;
    const uint32_t bones = Group(mdl,0x14), polygons = Group(mdl,0x30), arrays = Group(mdl,0x18);
    if (!bones || !polygons || !arrays) return false;
    uint32_t nodeId = 0xFFFFFFFF;
    for (unsigned i=0; i<Memory::Read32(bones+4); ++i) {
        const uint32_t node = Entry(bones,i);
        if (!node || !Memory::Contains(node,0xD0)) return false;
        const auto name = GuestString(node + Memory::Read32(node+8));
        if (name == boneName || (std::strcmp(boneName,"base_base")==0 && name=="base_base1")) {
            mesh.bone = Memory::Read32(node+0xC);
            nodeId = Memory::Read32(node+0x10);
            mesh.boneFlags = Memory::Read32(node+0x14);
            break;
        }
    }
    if (nodeId == 0xFFFFFFFF) return false;
    for (unsigned i=0; i<Memory::Read32(polygons+4); ++i) {
        const uint32_t polygon = Entry(polygons,i);
        if (!polygon || !Memory::Contains(polygon,0x64)) return false;
        if (Memory::Read32(polygon+8) != nodeId) continue;
        // Invisible and matrix-indexed/skinned meshes require a different path.
        if (Memory::Read32(polygon+0x34) & 3) return false;
        const uint32_t vertexArray = Entry(arrays,Memory::Read16(polygon+0x48));
        if (!vertexArray) return false;
        if(region&&GuestString(vertexArray+Memory::Read32(vertexArray+0xC))!=region->vertexArray)continue;
        const uint32_t count = Memory::Read16(vertexArray+0x1E);
        const uint32_t stride = Memory::Read8(vertexArray+0x1D);
        const bool xyz = Memory::Read32(vertexArray+0x14) != 0;
        if (!count || count > 1024 || Memory::Read32(vertexArray+0x18) != 4 ||
            stride < (xyz ? 12u : 8u) || stride > 64) return false;
        const uint32_t data = vertexArray + Memory::Read32(vertexArray+8);
        if (!Memory::Contains(data,count*stride)) return false;
        const unsigned vertexBase = mesh.vertices.size();
        for (unsigned j=0;j<count;++j) {
            std::array<float,3> v{Memory::ReadFloat32(data+j*stride),
                Memory::ReadFloat32(data+j*stride+4),xyz ? Memory::ReadFloat32(data+j*stride+8) : 0};
            for (float f:v) if (!std::isfinite(f)) return false;
            mesh.vertices.push_back(v);
        }
        const uint32_t low=Memory::Read32(polygon+0xC), high=Memory::Read32(polygon+0x10);
        if (low & 0x1FF) return false;
        const unsigned positionType=(low>>9)&3;
        if (positionType < 2) return false;
        unsigned streamStride=0;
        for (unsigned k=0;k<12;++k) {
            const unsigned type=k<4 ? (low>>(9+2*k))&3 : (high>>(2*(k-4)))&3;
            if (type == 1) return false;
            streamStride += type == 3 ? 2 : type == 2 ? 1 : 0;
        }
        unsigned uvOffset=0,uvType=0,uvStride=0,uvCount=0;
        uint32_t uvData=0;
        if(region) {
            for(unsigned k=0;k<4;++k) {
                const unsigned type=(low>>(9+2*k))&3;
                uvOffset+=type==3 ? 2 : type==2 ? 1 : 0;
            }
            uvType=high&3;
            const uint32_t uvArray=Entry(Group(mdl,0x24),Memory::Read16(polygon+0x50));
            if(uvType<2||!uvArray||Memory::Read32(uvArray+0x18)!=4)return false;
            uvStride=Memory::Read8(uvArray+0x1D);uvCount=Memory::Read16(uvArray+0x1E);
            uvData=uvArray+Memory::Read32(uvArray+8);
            if(uvStride<8||uvStride>64||!uvCount||!Memory::Contains(uvData,uvCount*uvStride))return false;
        }
        const uint32_t size=Memory::Read32(polygon+0x28);
        uint32_t at=polygon+0x24+Memory::Read32(polygon+0x2C);
        if (!streamStride || size > 0x10000 || !Memory::Contains(at,size)) return false;
        const uint32_t end=at+size;
        while (at<end) {
            const uint8_t op=Memory::Read8(at++);
            if (!op) continue;
            if (at+2>end) return false;
            const unsigned n=Memory::Read16(at); at+=2;
            if (n>4096 || n*streamStride>end-at) return false;
            std::vector<unsigned> indices;
            std::array<float,4> uvBounds{2,2,-2,-2};
            for (unsigned j=0;j<n;++j) {
                unsigned index=positionType==2 ? Memory::Read8(at) : Memory::Read16(at);
                if (index>=count) return false;
                if(region) {
                    const unsigned uvIndex=uvType==2 ? Memory::Read8(at+uvOffset) : Memory::Read16(at+uvOffset);
                    if(uvIndex>=uvCount)return false;
                    const float u=Memory::ReadFloat32(uvData+uvIndex*uvStride),v=Memory::ReadFloat32(uvData+uvIndex*uvStride+4);
                    if(!std::isfinite(u)||!std::isfinite(v))return false;
                    uvBounds[0]=std::min(uvBounds[0],u);uvBounds[1]=std::min(uvBounds[1],v);
                    uvBounds[2]=std::max(uvBounds[2],u);uvBounds[3]=std::max(uvBounds[3],v);
                }
                indices.push_back(vertexBase+index); at+=streamStride;
            }
            if(region&&(std::abs(uvBounds[0]-region->left)>0.0001f||std::abs(uvBounds[1]-region->top)>0.0001f||
                std::abs(uvBounds[2]-region->right)>0.0001f||std::abs(uvBounds[3]-region->bottom)>0.0001f))continue;
            auto tri=[&](unsigned a,unsigned b,unsigned c) {
                mesh.triangles.push_back({indices[a],indices[b],indices[c]});
            };
            switch (op&0xF8) {
            case 0x80: if(n%4) return false; for(unsigned j=0;j<n;j+=4){tri(j,j+1,j+2);tri(j,j+2,j+3);} break;
            case 0x90: if(n%3) return false; for(unsigned j=0;j<n;j+=3)tri(j,j+1,j+2); break;
            case 0x98: for(unsigned j=2;j<n;++j)tri(j-2,j-1,j); break;
            case 0xA0: for(unsigned j=2;j<n;++j)tri(0,j-1,j); break;
            default: return false;
            }
        }
    }
    return !mesh.triangles.empty();
}

// MuMsg lays out a line using the model's textL/textR nodes. Reuse the same
// live MuObject rectangle helper for lists whose rows contain dynamic text
// rather than a dedicated background polygon.
bool ObjectField(Probe& probe,uint32_t object,Mesh& mesh,const char* modelName,unsigned line) {
    if(!Memory::Contains(object,0x60))return false;
    mesh.model=Memory::Read32(object+0x10);
    if(!Memory::Contains(mesh.model,0xEC))return false;
    const uint32_t mdl=Memory::Read32(mesh.model+0xE8);
    if(!Memory::Contains(mdl,0x40)||Memory::Read32(mdl)!=0x4D444C30||Memory::Read32(mdl+8)!=9||
       GuestString(mdl+Memory::Read32(mdl+0x3C))!=modelName)return false;
    const uint32_t bones=Group(mdl,0x14);
    if(!bones)return false;
    uint32_t center=0,left=0,right=0;
    const auto suffix=std::to_string(line);
    for(unsigned i=0;i<Memory::Read32(bones+4);++i) {
        const uint32_t node=Entry(bones,i);
        if(!node||!Memory::Contains(node,0xD0))return false;
        const auto name=GuestString(node+Memory::Read32(node+8));
        if(name=="textN"+suffix)center=node;
        else if(name=="textL"+suffix)left=node;
        else if(name=="textR"+suffix)right=node;
    }
    if(!center||!left||!right||left+Memory::Read32(left+0x5C)!=center||right+Memory::Read32(right+0x5C)!=center)return false;
    mesh.bone=Memory::Read32(center+0xC);mesh.boneFlags=Memory::Read32(center+0x14);
    const unsigned leftIndex=Memory::Read32(left+0xC),rightIndex=Memory::Read32(right+0xC);
    probe.Call(0x800B6A04,object,probe.scratch,leftIndex,rightIndex);
    const float x0=Memory::ReadFloat32(probe.scratch),x1=Memory::ReadFloat32(probe.scratch+4);
    const float y1=Memory::ReadFloat32(probe.scratch+8),y0=Memory::ReadFloat32(probe.scratch+12);
    probe.Call(0x800B6C30,probe.scratch,object,leftIndex);
    const float z=Memory::ReadFloat32(probe.scratch+8);
    probe.Call(0x800B6C30,probe.scratch,object,rightIndex);
    const float z1=Memory::ReadFloat32(probe.scratch+8);
    if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1)||
       !std::isfinite(z)||!std::isfinite(z1)||x0>=x1||y0>=y1||std::abs(z-z1)>.001f)return false;
    mesh.worldSpace=true;
    mesh.vertices={{{x0,y0,z}},{{x1,y0,z}},{{x1,y1,z}},{{x0,y1,z}}};
    mesh.triangles={{{0,1,2}},{{0,2,3}}};
    return true;
}

bool MenuMesh(uint32_t object, Mesh& mesh) {
    return ObjectMesh(object,mesh,"MenMainIcon","base_base");
}

// NW4R scene roots carry their own camera. In particular the pause/results
// UI does not use the match camera. Walk the live render tree, verify its
// RTTI, and use the matrices/viewport that were actually used to draw it.
bool RenderCamera(Probe& probe, const Mesh& mesh, uint32_t& camera,const char** reason=nullptr) {
    auto fail=[&](const char* why){if(reason)*reason=why;return false;};
    uint32_t object=mesh.model;
    bool hasParent=false;
    for(unsigned depth=0;depth<16;++depth) {
        if(!Memory::Contains(object,0xDC))return fail("object-range");
        const uint32_t flags=Memory::Read32(object+0xCC);
        if((flags&0x10) || (flags&0x60)==0x60)return fail("object-hidden");
        const uint32_t parent=Memory::Read32(object+4);
        if(parent){hasParent=true;object=parent;continue;}
        if(!hasParent || !Memory::Contains(object,0xF8))return fail("no-root");
        const uint32_t vt=Memory::Read32(object);
        if(!Memory::Contains(vt,0x1C))return fail("root-vtable");
        const auto type=GuestString(probe.Call(Memory::Read32(vt+0x18),object));
        if(type!="ScnRoot")return fail("root-type");
        const unsigned id=Memory::Read8(object+0xF4);
        if(id>=32)return fail("camera-index");
        camera=object+0xF8+id*0x10C;
        if(!Memory::Contains(camera,0x10C))return fail("camera-range");
        if((Memory::Read32(camera+0x70)&0x88)!=0x88)return fail("camera-not-ready");
        // CopiedVisAccess::IsVisible reads this byte for animated/copied
        // node visibility and falls back to ResNode's visible flag.
        const uint32_t modelVt=Memory::Read32(mesh.model);
        if(!Memory::Contains(modelVt,0x1C))return fail("model-vtable");
        const auto modelType=GuestString(probe.Call(Memory::Read32(modelVt+0x18),mesh.model));
        if(modelType=="ScnMdl" || modelType=="ScnMdlExpand") {
            if(!Memory::Contains(mesh.model,0x14C))return fail("model-range");
            const uint32_t vis=Memory::Read32(mesh.model+0x148);
            if(vis) return (Memory::Contains(vis+mesh.bone,1)&&Memory::Read8(vis+mesh.bone)!=0)||fail("bone-hidden");
        }
        return (mesh.boneFlags&0x100)!=0||fail("bone-hidden");
    }
    return fail("tree-depth");
}

bool ProjectRenderCamera(uint32_t camera, const std::array<double,3>& world, Detail::Point& pixel) {
    std::array<double,4> eye{0,0,0,1},clip{};
    for(unsigned row=0;row<3;++row) {
        eye[row]=Memory::ReadFloat32(camera+row*16+12);
        for(unsigned col=0;col<3;++col)
            eye[row]+=world[col]*Memory::ReadFloat32(camera+row*16+col*4);
    }
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col)
        clip[row]+=eye[col]*Memory::ReadFloat32(camera+0x30+row*16+col*4);
    if(!std::isfinite(clip[3])||clip[3]<=0)return false;
    const float x=Memory::ReadFloat32(camera+0xDC),y=Memory::ReadFloat32(camera+0xE0);
    const float w=Memory::ReadFloat32(camera+0xE4),h=Memory::ReadFloat32(camera+0xE8);
    if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0)return false;
    clip[0]*=AuroraProjectionHorizontalScale(Memory::ReadFloat32(camera+0x30),
        Memory::ReadFloat32(camera+0x44),w,h);
    pixel={x+(clip[0]/clip[3]+1)*w*.5,y+(1-clip[1]/clip[3])*h*.5};
    return std::isfinite(pixel.x)&&std::isfinite(pixel.y);
}

bool HitMesh(Probe& probe, uint32_t camera, const Mesh& mesh, Detail::Point pixel) {
    (void)camera;
    uint32_t renderCamera;
    if(!RenderCamera(probe,mesh,renderCamera))return false;
    std::array<float,12> matrix{1,0,0,0,0,1,0,0,0,0,1,0};
    if(!mesh.worldSpace) {
        probe.Call(0x801AEF80,mesh.model,probe.scratch,1,mesh.bone);
        for (unsigned i=0;i<12;++i) {
            matrix[i]=Memory::ReadFloat32(probe.scratch+i*4);
            if (!std::isfinite(matrix[i])) return false;
        }
    }
    std::vector<Detail::Point> projected;
    for (const auto& v:mesh.vertices) {
        std::array<double,3> w;
        for (unsigned j=0;j<3;++j)
            w[j]=matrix[j*4]*v[0]+matrix[j*4+1]*v[1]+matrix[j*4+2]*v[2]+matrix[j*4+3];
        Detail::Point p;
        if (!ProjectRenderCamera(renderCamera,w,p)) return false;
        projected.push_back(p);
    }
    for (const auto& t:mesh.triangles)
        if (Detail::InTriangle(pixel,projected[t[0]],projected[t[1]],projected[t[2]])) return true;
    return false;
}

struct MeshAction { Mesh mesh; uint16_t buttons; };
bool AddAction(Probe& probe, std::vector<MeshAction>& actions, uint32_t object,
               const char* model, const char* node, uint16_t buttons, const MeshRegion* region=nullptr) {
    MeshAction action{{},buttons};
    if(!ObjectMesh(object,action.mesh,model,node,region)) {
        static std::string lastFailure;
        const auto failure=std::to_string(object)+":"+model+":"+node;
        if(failure!=lastFailure) {
            RT_LOGF(RT_TAG_RUNTIME,"[menu touch] missing mesh object=%08x model=%s node=%s\n",object,model,node);
            lastFailure=failure;
        }
        return false;
    }
    uint32_t camera;const char* reason="unknown";
    if(RenderCamera(probe,action.mesh,camera,&reason))actions.push_back(std::move(action));
    else if(std::strncmp(model,"InfPause",8)==0||std::strncmp(model,"InfResult",9)==0||std::strncmp(model,"InfReset",8)==0) {
        static std::vector<std::string> reported;
        const auto failure=std::to_string(object)+":"+model+":"+node+":"+reason;
        if(std::find(reported.begin(),reported.end(),failure)==reported.end()) {
            if(reported.size()>=64)reported.clear();
            reported.push_back(failure);
            RT_LOGF(RT_TAG_RUNTIME,"[menu touch] HUD geometry object=%08x model=%s node=%s gate=%s\n",object,model,node,reason);
        }
    }
    return true;
}

void ObserveActions(CpuContext& cpu, const ScreenKey& key, Probe& probe,
                    const std::vector<MeshAction>& actions) {
    if(actions.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==key.surface)InvalidateLocked();
        return;
    }
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(camera,0x134))return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(!tap)return;
    uint16_t buttons=0;
    for(const auto& action:actions)if(HitMesh(probe,camera,action.mesh,{tap->x*width,tap->y*height})) {
        if(buttons && buttons!=action.buttons)return;
        buttons=action.buttons;
    }
    // When the original photo help is hidden, a stationary background tap
    // brings it back. Drag/pinch are excluded by the frontend tap tracker.
    if(!buttons&&key.surface==Surface::Pause&&(key.mode&1)&&key.scroll)buttons=0x200;
    if(!buttons||(key.surface==Surface::Pause&&(buttons&0xE000)))return;
    std::lock_guard<std::mutex> lock(mutex);
    if(epoch==acceptedEpoch)padPulse|=buttons;
}

// Common save/confirmation windows. Their native controller numbers the left
// choice 1 and right choice 0; do not infer that numbering from the labels.
void ObserveNotice(CpuContext& cpu) {
    const uint32_t task=cpu.gpr[3];
    if(!Memory::Contains(task,0x118))return;
    const unsigned state=Memory::Read32(task+0x104),kind=Memory::Read32(task+0xFC);
    const int32_t delay=static_cast<int32_t>(Memory::Read32(task+0x110));
    const int32_t ticks=static_cast<int32_t>(Memory::Read32(task+0x114));
    if(state!=1 || Memory::Read32(task+0x108)!=1 || kind>2 ||
       !(Memory::Read8(task+0x58)&1) || delay<0 || ticks<delay) {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==Surface::Notice && published.task==task)InvalidateLocked();
        return;
    }
    const uint32_t object=Memory::Read32(task+0x40);
    const unsigned selected=Memory::Read8(task+0x100);
    if(selected>1)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),object,task,1,kind,
        static_cast<uint32_t>(delay),Surface::Notice};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> choices;
    constexpr const char* model="MenCmnNotice0000_TopN";
    if(kind==1) {
        // MuMsg lines2/3 attach to textN2/textN3. These meshes are children
        // of those exact nodes, including their live scale and visibility.
        if(!AddAction(probe,choices,object,model,"frame1",1) ||
           !AddAction(probe,choices,object,model,"frame4",1) ||
           !AddAction(probe,choices,object,model,"frame2",0) ||
           !AddAction(probe,choices,object,model,"frame3",0))return;
        bool left=false,right=false;
        for(const auto& choice:choices)(choice.buttons ? left : right)=true;
        if(!left||!right)return;
    } else if(kind==2) {
        // Notice line4 attaches to textN5 (line5 attaches to textN4).
        if(!AddAction(probe,choices,object,model,"frame2_2",0) ||
           !AddAction(probe,choices,object,model,"frame3_2",0) || choices.empty())return;
    } else {
        // Kind0 is the advance indicator, seen after creating the save. The
        // native proc accepts A here; kind3's progress spinner accepts none.
        if(!AddAction(probe,choices,object,model,"frame2_1",0) ||
           !AddAction(probe,choices,object,model,"frame3_1",0) ||
           !AddAction(probe,choices,object,model,"sankaku",0) || choices.empty())return;
    }
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& choice:choices)
            if(HitMesh(probe,camera,choice.mesh,{tap->x*width,tap->y*height})) {
                if(target>=0&&target!=choice.buttons){target=-1;break;}
                target=choice.buttons;
            }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key&&target>=0)
            noticeNavigation=NoticeNavigation{epoch,task,static_cast<unsigned>(target),false,
                Clock::now()+std::chrono::milliseconds(250)};
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!noticeNavigation || noticeNavigation->epoch!=epoch || noticeNavigation->task!=task)return;
    if(Clock::now()>noticeNavigation->expires){noticeNavigation.reset();return;}
    if(kind!=1 || selected==noticeNavigation->target) {
        padPulse|=kA;noticeNavigation.reset();return;
    }
    // Send one real left/right edge, then wait for the native notice state
    // to acknowledge it before confirming. Never patch its selected byte.
    if(!noticeNavigation->directionSent) {
        padPulse|=noticeNavigation->target==1 ? 0x1 : 0x2;
        noticeNavigation->directionSent=true;
    }
}

void ProcessPhotoPointers(CpuContext& cpu,const ScreenKey& key,Probe& probe,const std::vector<MeshAction>& actions) {
    std::array<PointerContact,2> contacts;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(!FreshLocked()||!(published==key))return;
        contacts=pointerContacts;acceptedEpoch=epoch;
    }
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(camera,0x134))return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    for(auto& contact:contacts)if(contact.active&&!contact.mode) {
        uint16_t hit=0;
        for(const auto& action:actions)if(HitMesh(probe,camera,action.mesh,{contact.downX*width,contact.downY*height})) {
            if(hit&&hit!=action.buttons){hit=0xFFFF;break;}
            hit=action.buttons;
        }
        // Rotation starts on the scene background or its original stick icon.
        // Discrete commands activate on UP through the ordinary tap path.
        contact.mode=!hit ? 0x8000 : hit==0x4000||hit==0x8000||hit==0x2000||
            hit==0x400||hit==0x800||hit==0x20||hit==0x40 ? hit : 0xFFFF;
    }
    std::array<int8_t,4> axes{};uint16_t held=0;
    auto axis=[](float delta) -> int8_t {
        return static_cast<int8_t>(std::lround(std::clamp(delta*4096.0f,-127.0f,127.0f)));
    };
    const bool pinch=contacts[0].active&&contacts[1].active&&contacts[0].mode==0x8000&&contacts[1].mode==0x8000;
    if(pinch) {
        const auto& a=contacts[0];const auto& b=contacts[1];
        const float oldDistance=std::hypot(a.lastX-b.lastX,a.lastY-b.lastY);
        const float distance=std::hypot(a.x-b.x,a.y-b.y);
        if(distance>oldDistance+0.0001f)held|=0x400;
        else if(distance<oldDistance-0.0001f)held|=0x800;
    } else for(const auto& contact:contacts)if(contact.active) {
        const float dx=contact.x-contact.lastX,dy=contact.y-contact.lastY;
        if(contact.mode==0x8000){axes[0]=axis(dx);axes[1]=axis(-dy);}
        else if(contact.mode==0x4000){axes[2]=axis(dx);axes[3]=axis(-dy);}
        else if(contact.mode==0x2000){if(dx>0.0001f)held|=0x20;else if(dx<-.0001f)held|=0x40;}
        else if(contact.mode!=0xFFFF)held|=contact.mode;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(epoch!=acceptedEpoch||!(published==key))return;
    // UP/CANCEL wins over an in-flight guest geometry probe.
    for(unsigned i=0;i<contacts.size();++i)if(contacts[i].active&&
        (!pointerContacts[i].active||pointerContacts[i].id!=contacts[i].id||pointerContacts[i].serial!=contacts[i].serial))return;
    for(unsigned i=0;i<contacts.size();++i)if(contacts[i].active) {
        pointerContacts[i].mode=contacts[i].mode;
        pointerContacts[i].lastX=contacts[i].x;pointerContacts[i].lastY=contacts[i].y;
    }
    pointerAxes=axes;pointerButtons=held;
}

void ObservePause(CpuContext& cpu) {
    const uint32_t pause=cpu.gpr[3];
    if(!Memory::Contains(pause,0x74))return;
    const uint32_t state=Memory::Read32(pause+0x34),caps=Memory::Read32(pause+0x6C);
    const uint32_t reason=Memory::Read32(pause+0x70);
    const uint32_t owner=Memory::Read8(pause+0x48);
    const bool debounce=static_cast<int32_t>(Memory::Read32(pause+0x60))>=0;
    static std::array<uint32_t,5> previous{};
    const std::array<uint32_t,5> signature{pause,state,caps,reason,(owner<<1)|debounce};
    if(signature!=previous) {
        previous=signature;
        RT_LOGF(RT_TAG_RUNTIME,"[menu touch] pause owner=%08x state=%u caps=%x reason=%u port=%u debounce=%u hidden=%u\n",
            pause,state,caps,reason,owner,debounce,Memory::Read8(pause+0x64));
    }
    // Only the local GameCube owner's active pause page, after the original
    // input debounce. Photo dialogs and pending exit/retry invalidate it.
    if(state!=1 || Memory::Read8(pause+0x48)!=0 || Memory::Read32(pause+0x70)!=1 ||
       static_cast<int32_t>(Memory::Read32(pause+0x60))>=0) {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==Surface::Pause)InvalidateLocked();
        return;
    }
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    if(!Memory::Contains(manager,8))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),pause,pause,state,caps,
        Memory::Read8(pause+0x64),Surface::Pause};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> actions;
    const uint32_t pauseModel=Memory::Read32(pause+8);
    if(pauseModel) {
        if(!AddAction(probe,actions,pauseModel,"InfPause0010_TopN","back",0x1000))return;
        if((caps&2) && !AddAction(probe,actions,pauseModel,"InfPause0010_TopN","retry",0x10))return;
        if(caps&1) {
            // Original help sprites are independent fan primitives on one bone.
            // Their atlas UV bounds identify the actual labels/buttons, while
            // the live model matrix supplies every on-screen position/size.
            struct PhotoRegion { MeshRegion region; uint16_t action; };
            static constexpr PhotoRegion regions[]={
                {{"button__lambert4",0,0,.1243f,1},0x10},
                {{"button__lambert4",.1243f,0,.2957f,1},0x400},
                {{"button__lambert4",.2950f,0,.4053f,1},0x800},
                {{"button__lambert4",.1265f,0,.2371f,1},0x800},
                {{"button__lambert4",.4053f,0,.5040f,1},0x4000},
                {{"button__lambert4",.5040f,0,.6349f,1},0x8000},
                {{"button__lambert4",.6349f,0,.7490f,1},0x2000},
                {{"button__lambert4",.7490f,0,.8505f,1},0x200},
                {{"button__lambert4",.8505f,0,1,1},0x200},
                {{"button__lambert194",.5529f,.8419f,1,1},0x10},
                {{"button__lambert194",.3320f,.2817f,.5529f,.5669f},0x400},
                {{"button__lambert194",0,.1939f,.3320f,.3827f},0x800},
                {{"button__lambert194",.3320f,.5669f,.5529f,.7879f},0x4000},
                {{"button__lambert194",0,.3827f,.3320f,.6906f},0x8000},
                {{"button__lambert194",.5529f,.3357f,1,.5888f},0x40},
                {{"button__lambert194",.5529f,.5888f,1,.8419f},0x20},
                {{"button__lambert194",.3320f,.7879f,.5529f,1},0x200},
            };
            for(const auto& region:regions)
                if(!AddAction(probe,actions,pauseModel,"InfPause0010_TopN","button",region.action,&region.region))return;
        }
    }
    if(caps&4) {
        const uint32_t reset=Memory::Read32(pause+0x1C);
        if(reset && !AddAction(probe,actions,reset,"InfReset0010_TopN","reset",0x1160))return;
    }
    ObserveActions(cpu,key,probe,actions);
    if(caps&1)ProcessPhotoPointers(cpu,key,probe,actions);
}

bool LocalResultPlayer(uint32_t slot,uint32_t task,unsigned player) {
    if(player>=4 || !Memory::Contains(slot,4) || !Memory::Contains(task,0xC2))return false;
    const uint32_t game=Memory::Read32(slot);
    if(!Memory::Contains(game,0x1C))return false;
    const uint32_t players=Memory::Read32(game+0x18);
    return Memory::Contains(players+player*0x2AC,0x29)&&
        (Memory::Read8(task+0xC1)&(1u<<player))&&Memory::Read8(players+player*0x2AC+0x28)==1;
}

void ObserveResult(CpuContext& cpu) {
    const uint32_t task=cpu.gpr[3],player=cpu.gpr[4];
    if(player>=4 || !Memory::Contains(task,0x4460))return;
    const bool local=LocalResultPlayer(cpu.gpr[13]-0x4340,task,player);
    static std::array<std::array<uint32_t,7>,4> previous{};
    const std::array<uint32_t,7> signature{task,Memory::Read32(task+0x40),Memory::Read32(task+0x48+player*4),
        Memory::Read32(task+0x658),Memory::Read8(task+0x66A),Memory::Read32(task+0x78+player*4)>=120,local};
    if(previous[player]!=signature) {
        previous[player]=signature;
        RT_LOGF(RT_TAG_RUNTIME,"[menu touch] result task=%08x player=%u state=%u playerState=%u modal=%u final=%u delayReady=%u local=%u mask=%x controls=%08x\n",
            task,player,signature[1],signature[2],signature[3],signature[4],signature[5],local,Memory::Read8(task+0xC1),Memory::Read32(task+0x138+4*player));
    }
    if(!local)return;
    const uint32_t state=Memory::Read32(task+0x40),playerState=Memory::Read32(task+0x48+player*4);
    if(state!=4 || playerState>2 || Memory::Read32(task+0x658)!=0 ||
       Memory::Read8(task+0x66A)!=0 || Memory::Read32(task+0x78+player*4)<120) {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==Surface::Result)InvalidateLocked();
        return;
    }
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    if(!Memory::Contains(manager,8))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),player,task,state,playerState,
        Memory::Read8(task+0x444C),Surface::Result,cpu.gpr[13]-0x4340};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> actions;
    const uint32_t controls=Memory::Read32(task+0x138+player*4);
    if(!AddAction(probe,actions,controls,"InfResultContl_TopN","abutton",kA))return;
    if(!AddAction(probe,actions,controls,"InfResultContl_TopN","contloller",kA))return;
    if(key.scroll) {
        const uint32_t replay=Memory::Read32(task+0xF8+player*4);
        if(!AddAction(probe,actions,replay,"InfResultReplay_TopN","Zbutton",0x10))return;
        if(!AddAction(probe,actions,replay,"InfResultReplay_TopN","replayE",0x10))return;
    }
    ObserveActions(cpu,key,probe,actions);
}

unsigned NextDirection(uint32_t graph, unsigned count, unsigned current, unsigned target) {
    std::array<int,64> first; first.fill(-1); first[current]=0;
    std::vector<unsigned> queue{current};
    for (unsigned q=0;q<queue.size();++q) {
        const unsigned item=queue[q];
        if(item==target) return first[item];
        for(unsigned d=0;d<4;++d) {
            const unsigned next=Memory::Read16(graph+item*16+d*4);
            if(next>=count || first[next]>=0) continue;
            first[next]=item==current ? 1u<<d : first[item];
            queue.push_back(next);
        }
    }
    return 0;
}

constexpr unsigned kPreviousPage=0x1000,kNextPage=0x1001,kParentPage=0x2000;
void ObserveMovie(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x688)||ProcName(proc)!="muProcMovie")return;
    if(!(Memory::Read8(proc+0x44)&1)||Memory::Read32(proc+0x664)!=2||Memory::Read32(proc+0x670)!=0)return;
    const unsigned total=Memory::Read8(proc+0x678),page=Memory::Read8(proc+0x679);
    const unsigned cell=Memory::Read8(proc+0x67A),row=Memory::Read8(proc+0x67B);
    if(!total||page*6>=total||row>1)return;
    const unsigned count=std::min(6u,total-page*6),firstRowSize=count>3 ? (count+1)/2 : count;
    if(cell>=(row ? count-firstRowSize : firstRowSize))return;
    const unsigned position=row*firstRowSize+cell;
    const uint32_t list=Memory::Read32(proc+0x674),objects=Memory::Read32(proc+0x64C);
    const uint32_t panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(list,total*4)||!Memory::Contains(objects,0x218)||!Memory::Contains(panel,0x84)||
       Memory::Read32(panel+0x50)!=3||Memory::Read32(panel+0x58)!=0)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134))return;
    if(ActiveMainProc(Memory::Read32(manager+4))!=proc)return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,list,2,page,total,Surface::Movie,objects};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> items;
    unsigned normal=0;
    for(unsigned i=0;i<count;++i) {
        const bool adventure=Memory::Read32(list+(page*6+i)*4)==0x79;
        const uint32_t object=Memory::Read32(objects+(adventure ? 0x210 : 0x1F8+4*normal++));
        const unsigned before=items.size();
        if(adventure) {
            if(!AddAction(probe,items,object,"MenMainIcon0613_TopN","pPlane159",i)||
               !AddAction(probe,items,object,"MenMainIcon0613_TopN","pPlane3",i))return;
        } else {
            if(!AddAction(probe,items,object,"MenMainIcon0612_TopN","filmcenter",i)||
               !AddAction(probe,items,object,"MenMainIcon0612_TopN","filmside",i)||
               !AddAction(probe,items,object,"MenMainIcon0612_TopN","picture",i))return;
        }
        if(items.size()==before)return;
    }
    const uint32_t arrows=Memory::Read32(objects+0x1F4);
    if(page && (!AddAction(probe,items,arrows,"MenMainIcon0611_TopN","leftallow2",kPreviousPage)||
                !AddAction(probe,items,arrows,"MenMainIcon0611_TopN","leftallow3",kPreviousPage)))return;
    if((page+1)*6<total && (!AddAction(probe,items,arrows,"MenMainIcon0611_TopN","rightallow3",kNextPage)||
                         !AddAction(probe,items,arrows,"MenMainIcon0611_TopN","rightallow4",kNextPage)))return;
    const unsigned beforeParent=items.size();
    if(!AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","mark",kParentPage)||
       !AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","markbase",kParentPage)||
       items.size()==beforeParent)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=item.buttons){target=-1;break;}
            target=item.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),position,8,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    if(nav.waiting) {
        if(position!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}
        return; // A release poll separates repeated directional edges.
    }
    if(!nav.remaining--){ResetItemNavigation();return;}
    if(nav.target==kParentPage){padPulse|=0x200;ResetItemNavigation();return;}
    if(nav.target==position){padPulse|=kA;ResetItemNavigation();return;}
    uint16_t direction;
    if(nav.target==kPreviousPage)direction=0x1; // native moveCursorMain wraps at the first column
    else if(nav.target==kNextPage)direction=0x2;
    else {
        const unsigned targetRow=nav.target>=firstRowSize,targetCell=nav.target-targetRow*firstRowSize;
        // padUpdate converts raw UP/DOWN to logical1/2 and LEFT/RIGHT
        // to logical4/8. The stored outer group is the display row.
        direction=row!=targetRow ? (targetRow ? 0x4 : 0x8) : (targetCell<cell ? 0x1 : 0x2);
    }
    navigationButtons=direction;nav.lastPosition=position;nav.waiting=true;
}

void ObserveMovieList(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x688)||ProcName(proc)!="muProcMovieAdv"||
       !(Memory::Read8(proc+0x44)&1)||Memory::Read32(proc+0x664)!=2||Memory::Read32(proc+0x670)!=0)return;
    const unsigned total=Memory::Read8(proc+0x678),selected=Memory::Read8(proc+0x679),scroll=Memory::Read8(proc+0x67A);
    if(!total||scroll>=total)return;
    const unsigned count=std::min(9u,total-scroll);
    if(selected>=count)return;
    const uint32_t list=Memory::Read32(proc+0x674),objects=Memory::Read32(proc+0x64C),panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(list,total*4)||!Memory::Contains(objects,0x220)||!Memory::Contains(panel,0x84)||
       Memory::Read32(panel+0x58)!=0)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||ActiveMainProc(Memory::Read32(manager+4))!=proc)return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,list,2,scroll,total,Surface::MovieList,objects};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> items;
    const uint32_t object=Memory::Read32(objects+0x218);
    for(unsigned i=0;i<count;++i) {
        const unsigned before=items.size();
        // The model binds base5..base13 to the nine textN1..textN9 rows.
        const auto node="base"+std::to_string(i+5);
        if(!AddAction(probe,items,object,"MenMainIcon0615_TopN",node.c_str(),i)||items.size()==before)return;
    }
    if(scroll && (!AddAction(probe,items,object,"MenMainIcon0615_TopN","leftallow2",kPreviousPage)||
                  !AddAction(probe,items,object,"MenMainIcon0615_TopN","leftallow3",kPreviousPage)))return;
    if(scroll+count<total && (!AddAction(probe,items,object,"MenMainIcon0615_TopN","rightallow3",kNextPage)||
                            !AddAction(probe,items,object,"MenMainIcon0615_TopN","rightallow4",kNextPage)))return;
    const unsigned beforeParent=items.size();
    // B returns from the sub-list to the movie catalog.
    if(!AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","mark",kParentPage)||
       !AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","markbase",kParentPage)||
       items.size()==beforeParent)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=item.buttons){target=-1;break;}
            target=item.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),selected,12,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    if(nav.waiting){if(selected!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}return;}
    if(!nav.remaining--){ResetItemNavigation();return;}
    if(nav.target==kParentPage){padPulse|=0x200;ResetItemNavigation();return;}
    if(nav.target==selected){padPulse|=kA;ResetItemNavigation();return;}
    const bool up=nav.target==kPreviousPage||(nav.target<9&&nav.target<selected);
    navigationButtons=up ? 0x8 : 0x4;
    nav.lastPosition=selected;nav.waiting=true;
}

bool MenuPadEnabled(uint32_t scene,uint32_t proc) {
    if(!Memory::Contains(proc,0x45)||!(Memory::Read8(proc+0x44)&1)||!Memory::Contains(scene,4))return false;
    if(GuestString(Memory::Read32(scene))!="muMenuMain")return true;
    return Memory::Contains(scene,0xABC)&&static_cast<int32_t>(Memory::Read32(scene+0xAB8))<=0;
}

uint32_t RuleEnabledMask(uint32_t proc,bool more=false) {
    unsigned mask=0;
    const unsigned count=more ? 6 : 7,offset=more ? 0x694 : 0x6A0;
    for(unsigned i=0;i<count;++i)if(Memory::Read8(proc+offset+i))mask|=1u<<i;
    if(!Memory::Read8(proc+(more ? 0x69A : 0x6A7)))mask&=~(1u<<(count-1));
    return mask;
}

// Rule1 has a custom selector: UP/DOWN choose a row, LEFT/RIGHT edit its
// value. Its final row has two separate items. Feed those original commands
// after hitting the live item/arrow polygons, without changing its save data.
void ObserveRules(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    const auto type=ProcName(proc);
    const bool more=type=="muProcRule2";
    if((!more&&type!="muProcRule1")||!Memory::Contains(proc,more ? 0x6A0 : 0x6B0)||
       !(Memory::Read8(proc+0x44)&1)||Memory::Read32(proc+0x664)!=2)return;
    const uint32_t objects=Memory::Read32(proc+0x64C),owner=objects-0x48,panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(owner,0x29C)||GuestString(Memory::Read32(owner))!="muRuleTask"||
       Memory::Read32(owner+0x268+(more ? 4 : 0))!=proc||Memory::Read32(owner+0x278)!=(more ? 1u : 0u)||
       !Memory::Contains(panel,0x84)||Memory::Read32(panel+0x58)!=0)return;
    const unsigned count=more ? 6 : 7;
    const unsigned selected=Memory::Read16(proc+0x42),mode=more ? 0x100 : Memory::Read8(proc+0x66C),enabled=RuleEnabledMask(proc,more);
    if(selected>=count||(!more&&mode>2)||!(enabled&(1u<<selected)))return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134))return;
    if(!MenuPadEnabled(Memory::Read32(manager+4),proc))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,owner,2,mode,enabled,Surface::Rules,panel};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> bodies,arrows,back;
    for(unsigned row=0;row<count;++row) {
        if(!(enabled&(1u<<row)))continue;
        const unsigned index=more ? row+0x12 : row==1&&mode==1 ? 7 : row;
        // Constructor/table bindings, including stock's replacement row.
        constexpr const char* names[]={"MenMainIcon0121_TopN","MenMainIcon0122_TopN","MenMainIcon0124_TopN",
            "MenMainIcon0125_TopN","MenMainIcon0126_TopN","MenMainIcon0127_TopN","MenMainIcon0128_TopN","MenMainIcon0123_TopN"};
        constexpr const char* moreNames[]={"MenMainIcon0131_TopN","MenMainIcon0132_TopN","MenMainIcon0133_TopN",
            "MenMainIcon0134_TopN","MenMainIcon0136_TopN","MenMainIcon0135_TopN"};
        const char* model=more ? moreNames[row] : names[index];
        const uint32_t object=Memory::Read32(objects+index*4);
        const unsigned before=bodies.size();
        if(!AddAction(probe,bodies,object,model,"base_base",row*4)||bodies.size()==before)return;
        if(row<5) {
            const char* left=index==1 ? "arrowL1" : "arrowL";
            const char* right=index==1 ? "arrowR1" : "arrowR";
            if(!AddAction(probe,arrows,object,model,left,row*4+1)||
               !AddAction(probe,arrows,object,model,right,row*4+2))return;
        }
    }
    if(!AddAction(probe,back,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","mark",kParentPage)||
       !AddAction(probe,back,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","markbase",kParentPage)||back.empty())return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        auto hit=[&](const std::vector<MeshAction>& items) {
            int target=-1;
            for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
                if(target>=0&&target!=item.buttons)return -2;
                target=item.buttons;
            }
            return target;
        };
        // Arrows are drawn over their own row background and take precedence.
        int target=hit(arrows);
        if(target==-1)target=hit(bodies);
        const int parent=hit(back);
        if(parent>=0)target=target==-1 ? parent : -2;
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),selected,12,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    if(nav.waiting){if(selected!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}return;}
    if(nav.target==kParentPage){padPulse|=0x200;ResetItemNavigation();return;}
    const unsigned target=nav.target/4,action=nav.target%4;
    if(target==selected) {
        // Native A value editing debounces until this counter exceeds five.
        if(action==0&&selected<5&&Memory::Read32(proc+(more ? 0x69C : 0x6AC))<=5)return;
        padPulse|=action==1 ? 0x1 : action==2 ? 0x2 : kA;
        ResetItemNavigation();return;
    }
    if(!nav.remaining--){ResetItemNavigation();return;}
    if(!more&&target>=5&&selected>=5)navigationButtons=0x2;
    else navigationButtons=target<selected ? 0x8 : 0x4;
    nav.lastPosition=selected;nav.waiting=true;
}

void ObserveOptions(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];const auto type=ProcName(proc);
    const bool deflick=type=="muProcOptDeflick";
    if((!deflick&&type!="muProcOptScreen")||!Memory::Contains(proc,0x66C))return;
    const unsigned phase=Memory::Read32(proc+0x664),selected=Memory::Read16(proc+0x42);
    if(phase>1||selected>1)return;
    const uint32_t objects=Memory::Read32(proc+0x64C),panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(objects,0xA8)||!Memory::Contains(panel,0x84)||Memory::Read32(panel+0x58)!=0)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||
       ActiveMainProc(Memory::Read32(manager+4))!=proc||!MenuPadEnabled(Memory::Read32(manager+4),proc))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,objects,phase,deflick ? 1u : 0u,0,Surface::Options,panel};
    Probe probe(cpu);if(!probe.scratch)return;
    std::vector<MeshAction> items;
    for(unsigned i=0;i<2;++i) {
        const unsigned before=items.size();
        if(deflick) {
            // The original proc calls gfVideo::setDeflicker(selected==1).
            if(!AddAction(probe,items,Memory::Read32(objects+0xA4),"MenMainOption0101_TopN",i ? "on3" : "off3",i))return;
        } else {
            const char* model=i ? "MenMainOption0002_TopN" : "MenMainOption0001_TopN";
            const uint32_t object=Memory::Read32(objects+0x9C+4*i);
            for(const char* node:{"pict","font","frame"})if(!AddAction(probe,items,object,model,node,i))return;
        }
        if(items.size()==before)return;
    }
    const unsigned beforeBack=items.size();
    if(!AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","mark",kParentPage)||
       !AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","markbase",kParentPage)||items.size()==beforeBack)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(!tap)return;
    int target=-1;
    for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
        if(target>=0&&target!=item.buttons){target=-1;break;}
        target=item.buttons;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(epoch!=acceptedEpoch||!(published==key)||target<0)return;
    if(target==kParentPage)padPulse|=0x200;
    else if(static_cast<unsigned>(target)!=selected)padPulse|=deflick ? kA : target ? 2 : 1;
}

void ObserveItemSwitch(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x690)||ProcName(proc)!="muProcItemSwitch"||Memory::Read32(proc+0x664)!=1)return;
    const uint32_t objects=Memory::Read32(proc+0x64C),owner=objects-0x48,panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(owner,0x29C)||GuestString(Memory::Read32(owner))!="muRuleTask"||
       Memory::Read32(owner+0x270)!=proc||Memory::Read32(owner+0x278)!=2||
       !Memory::Contains(panel,0x84)||Memory::Read32(panel+0x58)!=0)return;
    const unsigned row=Memory::Read32(proc+0x674),column=Memory::Read32(proc+0x670),frequency=Memory::Read8(proc+0x678);
    if(row>8||column>6||(row==8&&column>1)||frequency>3)return;
    const unsigned position=row*7+column,selected=Detail::ItemSwitchIndex(row,column);
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||!MenuPadEnabled(Memory::Read32(manager+4),proc))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,owner,1,0,0,Surface::ItemSwitch,panel};
    Probe probe(cpu);if(!probe.scratch)return;
    std::vector<MeshAction> items;
    // The task binds 49 independent item objects and the All object. Use their
    // live model transforms/visibility, including the original irregular grid.
    for(unsigned i=0;i<50;++i) {
        const unsigned before=items.size();
        const uint32_t object=Memory::Read32(objects+4*(i==49 ? 0x51 : 0x20+i));
        const char* model=i==49 ? "MenMainSwitch0004_TopN" : "MenMainSwitch0003_TopN";
        const unsigned action=i==49 ? 1 : i+2;
        for(const char* node:{"pPlane17","pPlane20","pPlane3"})
            if(!AddAction(probe,items,object,model,node,action))return;
        if(items.size()==before)return;
    }
    const uint32_t frequencyObject=Memory::Read32(objects+0x7C);
    // The four frequency label nodes are permanently present. Animation frame
    // and command bindings are checked against the original switch resource.
    constexpr const char* frequencyNodes[]={"ENItem009","ENItem008","ENItem007","ENItem006"};
    constexpr const char* frequencyBases[]={"noselbase","noselbase1","noselbase3","noselbase4"};
    for(unsigned i=0;i<4;++i) {
        const unsigned before=items.size();
        if(!AddAction(probe,items,frequencyObject,"MenMainSwitch0002_TopN",frequencyNodes[i],0x100+i)||
           !AddAction(probe,items,frequencyObject,"MenMainSwitch0002_TopN",frequencyBases[i],0x100+i)||items.size()==before)return;
    }
    const unsigned beforeBack=items.size();
    // This full-page menu covers the previous title panel and has no Back
    // glyph. Its own visible title is the touch return target (native B).
    if(!AddAction(probe,items,Memory::Read32(objects+0x78),"MenMainSwitch0001_TopN","ENItem005",kParentPage)||
       items.size()==beforeBack)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=item.buttons){target=-1;break;}
            target=item.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),position*4+frequency,20,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    const unsigned acknowledgement=position*4+frequency;
    if(nav.waiting){if(acknowledgement!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}return;}
    if(nav.target==kParentPage){padPulse|=0x200;ResetItemNavigation();return;}
    uint16_t direction=0;
    if(nav.target>=0x100) {
        const unsigned targetFrequency=nav.target-0x100;
        if(row==0) {
            if(targetFrequency==frequency){ResetItemNavigation();return;}
            direction=targetFrequency<frequency ? 1 : 2;
        } else direction=Detail::ItemSwitchDirection(position,0);
    } else {
        if(selected==nav.target){padPulse|=kA;ResetItemNavigation();return;}
        direction=Detail::ItemSwitchDirection(position,nav.target);
    }
    if(!direction||!nav.remaining--){ResetItemNavigation();return;}
    navigationButtons=direction;nav.lastPosition=acknowledgement;nav.waiting=true;
}

bool IsNameProc(uint32_t proc) {
    const auto type=ProcName(proc);
    return type=="muProcName"||type=="muProcOptBtn";
}

void ObserveNameEntry(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x720)||!IsNameProc(proc)||Memory::Read32(proc+0x664)!=0)return;
    const uint32_t task=Memory::Read32(proc+0x71C),entry=task+0x158;
    if(!Memory::Contains(task,0x480)||Memory::Read32(task+0x9C)!=5||Memory::Read32(task+0xA0)!=5||
       Memory::Read32(entry)!=1||Memory::Read32(entry+0x90)!=0)return;
    const unsigned selected=Memory::Read32(entry+0x40),remembered=Memory::Read32(entry+0x80),mode=Memory::Read32(entry+0x3C);
    if(selected>13||remembered>13||mode>=5)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||ActiveMainProc(Memory::Read32(manager+4))!=proc)return;
    const uint32_t object=Memory::Read32(entry+0x44);
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,task,5,mode,Memory::Read8(entry+0x88),Surface::NameEntry,object};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> items;
    constexpr const char* model="MenSelchrWbase";
    // CLR0's selected+1 frames and DrawOpa bindings prove these indices.
    // waku12 is the alphabet switch, not another group of characters.
    for(unsigned i=0;i<14;++i) {
        const unsigned before=items.size();
        if(i==0) {
            for(const auto node:{"yajirusi","yajirusi2","yajirusiwaku","yajirusiwaku1"})
                if(!AddAction(probe,items,object,model,node,i))return;
        } else if(i==13) {
            if(!AddAction(probe,items,object,model,"enter",i)||!AddAction(probe,items,object,model,"kettei",i))return;
        } else {
            const auto node=std::string("waku")+(i<10 ? "0" : "")+std::to_string(i);
            if(!AddAction(probe,items,object,model,node.c_str(),i))return;
            if(i==12&&!AddAction(probe,items,object,model,"kirikae",i))return;
        }
        if(items.size()==before)return;
    }
    constexpr unsigned random=0x3000;
    if(!key.scroll&&!AddAction(probe,items,object,model,"randam",random))return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    const unsigned position=selected*14+remembered;
    if(tap) {
        int target=-1;
        for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=item.buttons){target=-1;break;}
            target=item.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),position,16,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    if(nav.waiting){if(position!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}return;}
    if(nav.target==random){padPulse|=0x10;ResetItemNavigation();return;}
    if(nav.target==selected){padPulse|=kA;ResetItemNavigation();return;}
    if(!nav.remaining--){ResetItemNavigation();return;}
    const uint16_t direction=Detail::NameEntryDirection(selected,remembered,nav.target);
    if(!direction){ResetItemNavigation();return;}
    navigationButtons=direction;nav.lastPosition=position;nav.waiting=true;
}

void ObserveNameDelete(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x720)||ProcName(proc)!="muProcName"||Memory::Read32(proc+0x664)!=1)return;
    const uint32_t task=Memory::Read32(proc+0x71C),objects=Memory::Read32(proc+0x64C);
    if(!Memory::Contains(task,0x480)||!Memory::Contains(objects,0x2B8))return;
    const uint32_t object=Memory::Read32(objects+0x2B4),manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||
       ActiveMainProc(Memory::Read32(manager+4))!=proc||!MenuPadEnabled(Memory::Read32(manager+4),proc))return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,task,1,Memory::Read16(task+0x1EC),
        Memory::Read16(task+0x1EE),Surface::NameDelete,object};
    Probe probe(cpu);
    if(!probe.scratch)return;
    std::vector<MeshAction> choices;
    // Model hierarchy binds frame1/4 and no1E to the right NO button;
    // frame2/3 and yes1E share the left YES button's native position.
    for(const auto node:{"frame1","frame4","no1E"})
        if(!AddAction(probe,choices,object,"MenMainIcon0144_TopN",node,0))return;
    for(const auto node:{"frame2","frame3","yes1E"})
        if(!AddAction(probe,choices,object,"MenMainIcon0144_TopN",node,1))return;
    bool yes=false,no=false;
    for(const auto& choice:choices)(choice.buttons ? yes : no)=true;
    if(!yes||!no)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& choice:choices)if(HitMesh(probe,camera,choice.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=choice.buttons){target=-1;break;}
            target=choice.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key&&target>=0)
            noticeNavigation=NoticeNavigation{epoch,proc,static_cast<unsigned>(target),false,
                Clock::now()+std::chrono::milliseconds(500)};
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!noticeNavigation||noticeNavigation->epoch!=epoch||noticeNavigation->task!=proc)return;
    if(Clock::now()>noticeNavigation->expires){noticeNavigation.reset();return;}
    const unsigned selected=(Memory::Read8(proc+0x66C)&0x40) ? 1 : 0;
    if(selected==noticeNavigation->target){padPulse|=kA;noticeNavigation.reset();return;}
    if(!noticeNavigation->directionSent) {
        padPulse|=noticeNavigation->target ? 0x1 : 0x2;
        noticeNavigation->directionSent=true;
    }
}

// Name rows are dynamic text: use the same textL/textR bounds as MuMsg,
// with the current native node transforms. The menu owns input in muNameTask
// and never calls the generic menu pad routine while editing its list.
void ObserveNames(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    if(!Memory::Contains(proc,0x720)||!IsNameProc(proc)||Memory::Read32(proc+0x664)!=0)return;
    const uint32_t task=Memory::Read32(proc+0x71C),panel=Memory::Read32(proc+0x658);
    if(!Memory::Contains(task,0x480)||Memory::Read32(task+0x9C)!=2||Memory::Read32(task+0xA0)!=2||
       !(Memory::Read8(task+0xB0)&1)||!Memory::Contains(panel,0x84)||Memory::Read32(panel+0x58)!=0)return;
    const unsigned selected=Memory::Read16(task+0x1EC),scroll=Memory::Read16(task+0x1EE),total=Memory::Read32(task+0x3D8);
    if(!total||total>122||scroll>=total)return;
    const unsigned count=std::min(5u,total-scroll);
    if(selected>=count)return;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0),camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134)||ActiveMainProc(Memory::Read32(manager+4))!=proc)return;
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,task,2,scroll,total,Surface::Names,panel};
    Probe probe(cpu);
    if(!probe.scratch)return;
    const uint32_t object=Memory::Read32(task+0x44);
    const char* model=ProcName(proc)=="muProcOptBtn" ? "MenMainOption0302_TopN" : "MenMainIcon0142_TopN";
    std::vector<MeshAction> items;
    for(unsigned i=0;i<count;++i) {
        if(Memory::Read8(task+0x1F0+4*(scroll+i))&0x80)continue;
        MeshAction item{{},static_cast<uint16_t>(i)};
        if(!ObjectField(probe,object,item.mesh,model,i))return;
        uint32_t renderedCamera;
        if(!RenderCamera(probe,item.mesh,renderedCamera))return;
        items.push_back(std::move(item));
    }
    if(scroll&&(!AddAction(probe,items,object,model,"allowUp",kPreviousPage)||
                !AddAction(probe,items,object,model,"allow2",kPreviousPage)))return;
    if(scroll+count<total&&(!AddAction(probe,items,object,model,"allowDown",kNextPage)||
                           !AddAction(probe,items,object,model,"allow3",kNextPage)))return;
    const unsigned beforeParent=items.size();
    if(!AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","mark",kParentPage)||
       !AddAction(probe,items,Memory::Read32(panel+0x6C),"MenMainTitle_TopN","markbase",kParentPage)||
       items.size()==beforeParent)return;
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(const auto& item:items)if(HitMesh(probe,camera,item.mesh,{tap->x*width,tap->y*height})) {
            if(target>=0&&target!=item.buttons){target=-1;break;}
            target=item.buttons;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            ResetItemNavigation();
            if(target>=0)itemNavigation=ItemNavigation{epoch,proc,static_cast<unsigned>(target),selected,8,false,
                Clock::now()+std::chrono::seconds(3)};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!itemNavigation||itemNavigation->epoch!=epoch||itemNavigation->proc!=proc)return;
    auto& nav=*itemNavigation;
    if(Clock::now()>nav.expires){ResetItemNavigation();return;}
    if(nav.waiting){if(selected!=nav.lastPosition){nav.waiting=false;navigationButtons=0;nav.expires=Clock::now()+std::chrono::seconds(3);}return;}
    if(!nav.remaining--){ResetItemNavigation();return;}
    if(nav.target==kParentPage){padPulse|=0x200;ResetItemNavigation();return;}
    if(nav.target==selected){padPulse|=kA;ResetItemNavigation();return;}
    const bool up=nav.target==kPreviousPage||(nav.target<5&&nav.target<selected);
    navigationButtons=up ? 0x8 : 0x4;
    nav.lastPosition=selected;nav.waiting=true;
}

void TraceMenuGate(CpuContext& cpu,const char* reason) {
    struct Seen { uint32_t proc; std::string reason; };
    static std::vector<Seen> seen;
    const uint32_t proc=cpu.gpr[3];
    auto it=std::find_if(seen.begin(),seen.end(),[&](const Seen& value){return value.proc==proc;});
    if(it!=seen.end()&&it->reason==reason)return;
    if(it==seen.end()) { if(seen.size()>=32)seen.erase(seen.begin());seen.push_back({proc,reason}); }
    else it->reason=reason;
    if(!Memory::Contains(proc,0x66C))return;
    const uint32_t panel=Memory::Read32(proc+0x658);
    const unsigned depth=Memory::Contains(panel,0x84) ? Memory::Read32(panel+0x50) : 0;
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    const uint32_t scene=Memory::Contains(manager,8) ? Memory::Read32(manager+4) : 0;
    RT_LOGF(RT_TAG_RUNTIME,"[menu touch] menu gate=%s proc=%08x type=%s scene=%08x active=%08x enabled=%02x phase=%u depth=%u count=%u selected=%u\n",
        reason,proc,ProcName(proc).c_str(),scene,ActiveMainProc(scene),Memory::Read8(proc+0x44),Memory::Read32(proc+0x664),
        depth,Memory::Read16(proc+0x640),Memory::Read16(proc+0x42));
}

void ObserveMenu(CpuContext& cpu) {
    const uint32_t proc=cpu.gpr[3];
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==Surface::Menu && published.hand!=proc)InvalidateLocked();
    }
    if(!Memory::Contains(proc,0x668) || !Memory::Read8(proc+0x44)){TraceMenuGate(cpu,"disabled");return;}
    const uint32_t panel=Memory::Read32(proc+0x658);
    const unsigned depth=Memory::Contains(panel,0x84) ? Memory::Read32(panel+0x50) : 0;
    if(depth<1||depth>4) {
        TraceMenuGate(cpu,"panel-depth");
        std::lock_guard<std::mutex> lock(mutex);
        if(published.surface==Surface::Menu)InvalidateLocked();
        return;
    }
    const unsigned count=Memory::Read16(proc+0x640), selected=Memory::Read16(proc+0x42);
    const uint32_t graph=Memory::Read32(proc+0x63C), objects=Memory::Read32(proc+0x64C);
    const uint32_t offset=Memory::Read32(proc+0x644);
    if(!count || count>64 || selected>=count || offset>512 ||
       !Memory::Contains(graph,count*16) || !Memory::Contains(objects,(offset+count)*4)){TraceMenuGate(cpu,"selection-table");return;}
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(manager,8) || !Memory::Contains(camera,0x134)){TraceMenuGate(cpu,"camera");return;}
    if(ActiveMainProc(Memory::Read32(manager+4))!=proc){TraceMenuGate(cpu,"inactive-proc");return;}
    const ScreenKey key{manager,Memory::Read32(manager+4),proc,graph,
        Memory::Read32(proc+0x664),objects,offset,Surface::Menu,panel,depth};
    std::vector<Mesh> meshes(count);
    // The native graph excludes locked entries, even when count still includes
    // their allocated objects. Solo's locked All-Star is an unparented model
    // with all incoming edges removed by muProc1p::init. Require geometry only
    // for items reachable through the currently active native graph.
    std::array<bool,64> reachable{};
    std::vector<unsigned> queue{selected};reachable[selected]=true;
    for(unsigned q=0;q<queue.size();++q)for(unsigned d=0;d<4;++d) {
        const unsigned next=Memory::Read16(graph+queue[q]*16+d*4);
        if(next<count&&!reachable[next]){reachable[next]=true;queue.push_back(next);}
    }
    for(unsigned i=0;i<count;++i) {
        if(!reachable[i])continue;
        if(!MenuMesh(Memory::Read32(objects+(offset+i)*4),meshes[i])) {
            TraceMenuGate(cpu,"item-mesh");
            static std::vector<uint32_t> reported;
            const uint32_t object=Memory::Read32(objects+(offset+i)*4);
            if(reported.size()<32 && std::find(reported.begin(),reported.end(),object)==reported.end()) {
                reported.push_back(object);
                RT_LOGF(RT_TAG_RUNTIME,"[menu touch] unsupported menu geometry: proc=%08x item=%u object=%08x\n",proc,i,object);
            }
            std::lock_guard<std::mutex> lock(mutex);
            if(published.surface==Surface::Menu)InvalidateLocked();
            return;
        }
    }
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1) return;
    Probe probe(cpu);
    if(!probe.scratch)return;
    for(unsigned i=0;i<count;++i) {
        if(!reachable[i])continue;
        uint32_t renderedCamera;
        if(!RenderCamera(probe,meshes[i],renderedCamera)) {
            TraceMenuGate(cpu,"item-visibility-camera");
            std::lock_guard<std::mutex> lock(mutex);
            InvalidateLocked();
            return;
        }
    }
    std::vector<MeshAction> back;
    if(depth>1) {
        // The real category emblem (mark/markbase) is the touch return
        // affordance. Its pose and dimensions come from the title model.
        const uint32_t title=Memory::Read32(panel+0x6C);
        if(!AddAction(probe,back,title,"MenMainTitle_TopN","mark",0x200)||
           !AddAction(probe,back,title,"MenMainTitle_TopN","markbase",0x200)||back.empty()){TraceMenuGate(cpu,"back-visibility-camera");return;}
    }
    TraceMenuGate(cpu,"ready");
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending && pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(tap) {
        int target=-1;
        for(unsigned i=0;i<count;++i) if(reachable[i]&&HitMesh(probe,camera,meshes[i],{tap->x*width,tap->y*height})) {
            // Overlapping animated objects are ambiguous; never guess one.
            if(target>=0){target=-1;break;}
            target=i;
        }
        bool hitBack=false;
        for(const auto& action:back)
            hitBack|=HitMesh(probe,camera,action.mesh,{tap->x*width,tap->y*height});
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch==acceptedEpoch&&published==key) {
            navigation.reset();
            if(hitBack&&target<0)padPulse|=0x200;
            else if(!hitBack&&target>=0)
                navigation=Navigation{epoch,proc,static_cast<uint16_t>(target),count+1};
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if(!navigation || navigation->epoch!=epoch || navigation->proc!=proc)return;
    if(cpu.gpr[4]) {navigation.reset();return;}
    if(!navigation->remaining--){navigation.reset();return;}
    if(selected==navigation->target) {padPulse|=kA;navigation.reset();return;}
    const unsigned direction=NextDirection(graph,count,selected,navigation->target);
    if(!direction){navigation.reset();return;}
    // Let the original selectProc, repeat counters, sound and parent selection
    // animation run. No menu state or selection index is patched by the host.
    cpu.gpr[4]=direction;
}

void ObserveStage(CpuContext& cpu) {
    const uint32_t task=cpu.gpr[3];
    if(!Memory::Contains(task,0x6154))return;
    if(Memory::Read32(task+0x224)!=0 || Memory::Read8(task+0x6094)!=0) {
        std::lock_guard<std::mutex> lock(mutex);
        if(published.task==task)InvalidateLocked();
        return;
    }
    const uint32_t cursor=Memory::Read32(task+0x200);
    const uint32_t manager=Memory::Read32(cpu.gpr[13]-0x43C0);
    const uint32_t camera=Memory::Read32(cpu.gpr[13]-0x4460);
    if(!Memory::Contains(cursor,0x60)||!Memory::Contains(manager,8)||!Memory::Contains(camera,0x134))return;
    const unsigned page=Memory::Read32(task+0x228);
    if(page>2)return;
    const ScreenKey key{manager,Memory::Read32(manager+4),cursor,task,0,page,
        page==2 ? Memory::Read32(task+0x6088) : 0,Surface::Stage};
    const float width=Memory::ReadFloat32(camera+0x11C),height=Memory::ReadFloat32(camera+0x120);
    if(!std::isfinite(width)||!std::isfinite(height)||width<1||height<1)return;
    Probe probe(cpu);
    if(!probe.scratch)return;
    const float z=Memory::ReadFloat32(cursor+0x44);
    std::array<Detail::Point,4> corners;
    for(unsigned i=0;i<4;++i)
        if(!probe.Project(camera,(i&1)*100,(i>>1)*100,z,corners[i]))return;
    Detail::Point point;
    if(!Detail::Unproject(corners,corners[0],point))return;
    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);acceptedEpoch=epoch;
        if(pending&&pending->epoch==epoch)tap=pending;
        pending.reset();
    }
    if(!tap)return;
    const Detail::Point pixel{tap->x*width,tap->y*height};
    if(!Detail::Unproject(corners,pixel,point))return;
    point.x*=100;point.y*=100;
    Detail::Point again;
    if(!probe.Project(camera,point.x,point.y,z,again)||
       std::abs(again.x-pixel.x)>.5||std::abs(again.y-pixel.y)>.5)return;
    Memory::WriteFloat32(probe.scratch,point.x);
    Memory::WriteFloat32(probe.scratch+4,point.y);
    const uint32_t article=probe.Call(0x806B6EA8,task,probe.scratch);
    if(!article)return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(epoch!=acceptedEpoch)return;
    }
    // getPointedArticle tested the real colL/colR nodes and current page.
    // SetTrans updates both the cursor's coordinates and its rendered model.
    const std::array<float,3> old{Memory::ReadFloat32(cursor+0x3C),
        Memory::ReadFloat32(cursor+0x40),Memory::ReadFloat32(cursor+0x44)};
    Memory::WriteFloat32(probe.scratch,point.x);
    Memory::WriteFloat32(probe.scratch+4,point.y);
    Memory::WriteFloat32(probe.scratch+8,z);
    probe.Call(0x800B6838,cursor,probe.scratch);
    bool accepted=false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        accepted=epoch==acceptedEpoch&&published==key;
        if(accepted)padPulse|=kA;
    }
    if(!accepted) {
        for(unsigned i=0;i<3;++i)Memory::WriteFloat32(probe.scratch+i*4,old[i]);
        probe.Call(0x800B6838,cursor,probe.scratch);
        return;
    }
    RT_LOGF(RT_TAG_RUNTIME,"[menu touch] SSS article %u at %.1f,%.1f\n",article,point.x,point.y);
}

bool CssKey(const CpuContext& cpu, ScreenKey& key, uint32_t& camera) {
    // Verified RSBE01 muSelCharHand / muSelCharPlayerArea layouts. Require
    // live calls from the game; never scan RAM for a coincidental pointer.
    const uint32_t hand = cpu.gpr[3];
    if (!Memory::Contains(hand, 0xE0) || !Memory::Contains(cpu.gpr[13] - 0x4460, 4)) return false;
    const uint32_t area = Memory::Read32(hand + 0x9C);
    if (!Memory::Contains(area, 0x1F8) || Memory::Read32(area + 0x1DC) != 0) return false;
    const uint32_t task = Memory::Read32(area + 0x1F4);
    if (!Memory::Contains(task, 0x650)) return false;
    const uint32_t manager = Memory::Read32(cpu.gpr[13] - 0x43C0);
    if (!Memory::Contains(manager, 8)) return false;
    camera = Memory::Read32(cpu.gpr[13] - 0x4460);
    if (!Memory::Contains(camera, 0x134)) return false;
    key = {manager, Memory::Read32(manager + 4), hand, task,
           Memory::Read32(task + 0x5E8), Memory::Read32(hand + 0xA4),0,Surface::Css};
    // Mode 0 is the freely moving hand. Other modes include automatic motion
    // and grabbed value controls; they must not receive a delayed click.
    return key.scene && key.mode == 0 && Memory::Read32(area) != 1;
}

void ObserveCss(CpuContext& cpu) {
    ScreenKey key;
    uint32_t camera;
    if (!CssKey(cpu, key, camera)) {
        // Calls for P2..P4 must not cancel P1. An invalidated P1 object or
        // automatic hand animation, however, retires its published snapshot.
        std::lock_guard<std::mutex> lock(mutex);
        if (published.hand == cpu.gpr[3]) InvalidateLocked();
        return;
    }
    const auto sceneName = SaveStates::SceneName();
    if (sceneName.rfind("scSel", 0) != 0) return;
    if(cssTraceHand==key.hand&&cssTraceRemaining) {
        --cssTraceRemaining;
        const uint32_t pads=Memory::Read32(0x805A0040),area=Memory::Read32(key.hand+0x9C);
        if(Memory::Contains(pads,0x284))RT_LOGF(RT_TAG_RUNTIME,
            "[menu touch] CSS after tap left=%u status=%08x/%08x/%08x trigger=%08x blocked=%x coin=%08x kind=%u cached=%u xy=%.1f,%.1f\n",
            cssTraceRemaining,Memory::Read32(pads+0x244),Memory::Read32(pads+0x248),Memory::Read32(pads+0x24C),
            Memory::Read32(pads+0x250),Memory::Read16(pads+0x3A),Memory::Read32(key.hand+0xA0),
            Memory::Read32(area+0x1B4),Memory::Read32(key.hand+0xAC),Memory::ReadFloat32(key.hand+0x90),Memory::ReadFloat32(key.hand+0x94));
    }
    // Preserve modified/modded functions rather than probing an unknown ABI.
    if (Memory::Read32(kHandMain) != 0x9421FFE0u ||
        Memory::Read32(kHandCollision) != 0x9421FFA0u || Memory::Read32(kPortraitCollision)!=0x9421FFD0u) return;
    const float width = Memory::ReadFloat32(camera + 0x11C);
    const float height = Memory::ReadFloat32(camera + 0x120);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1) return;
    Probe probe(cpu);
    if (!probe.scratch) return;
    const float z = Memory::ReadFloat32(key.hand + 0x98);
    std::array<Detail::Point, 4> corners;
    constexpr double kSpan = 100;
    for (unsigned i = 0; i < 4; ++i)
        if (!probe.Project(camera, (i & 1) * kSpan, (i >> 1) * kSpan, z, corners[i])) return;
    Detail::Point check;
    if (!Detail::Unproject(corners, corners[0], check)) return;

    std::optional<Tap> tap;
    uint64_t acceptedEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex);
        PublishLocked(key);
        acceptedEpoch = epoch;
        if (pending && pending->epoch == epoch) tap = pending;
        pending.reset();
    }
    if (!tap) return;
    const Detail::Point pixel{tap->x * width, tap->y * height};
    Detail::Point point;
    if (!Detail::Unproject(corners, pixel, point)) {
        RT_LOGF(RT_TAG_RUNTIME,"[menu touch] CSS tap rejected inverse x=%.3f y=%.3f\n",tap->x,tap->y);
        return;
    }
    point.x *= kSpan; point.y *= kSpan;
    Detail::Point reprojected;
    if (!probe.Project(camera, point.x, point.y, z, reprojected) ||
        std::abs(pixel.x - reprojected.x) > 0.5 || std::abs(pixel.y - reprojected.y) > 0.5) {
        RT_LOGF(RT_TAG_RUNTIME,"[menu touch] CSS tap rejected projection pixel=%.1f,%.1f world=%.1f,%.1f result=%.1f,%.1f\n",
            pixel.x,pixel.y,point.x,point.y,reprojected.x,reprojected.y);
        return;
    }

    // The original collision routine uses the live layout, disabled entries,
    // page and player panel. A background tap is never turned into A.
    const float oldX = Memory::ReadFloat32(key.hand + 0x90);
    const float oldY = Memory::ReadFloat32(key.hand + 0x94);
    Memory::WriteFloat32(key.hand + 0x90, point.x);
    Memory::WriteFloat32(key.hand + 0x94, point.y);
    uint32_t collision = 0,portrait=0x28;
    try {
        collision = probe.Call(kHandCollision, key.hand, 0, 0);
        // Hand collision handles controls and coins, not character portraits.
        // The portrait routine tests each live face rectangle and returns
        // 0x28 for no face; character zero (Mario) is a valid result.
        Memory::WriteFloat32(probe.scratch,point.x);
        Memory::WriteFloat32(probe.scratch+4,point.y);
        portrait=probe.Call(kPortraitCollision,key.task,probe.scratch);
    } catch (...) {
        Memory::WriteFloat32(key.hand + 0x90, oldX);
        Memory::WriteFloat32(key.hand + 0x94, oldY);
        throw;
    }
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        accepted = epoch == acceptedEpoch && published == key && (collision != 0 || portrait != 0x28);
        if (accepted) padPulse |= kA;
    }
    if (!accepted) {
        Memory::WriteFloat32(key.hand + 0x90, oldX);
        Memory::WriteFloat32(key.hand + 0x94, oldY);
    }
    if(accepted){cssTraceHand=key.hand;cssTraceRemaining=3;}
    RT_LOGF(RT_TAG_RUNTIME,"[menu touch] CSS tap accepted=%u control=%u portrait=%u pixel=%.1f,%.1f world=%.1f,%.1f\n",
        accepted,collision,portrait,pixel.x,pixel.y,point.x,point.y);
}
// The title screen is deliberately a full-screen START target. The native
// title state machine still controls its startup/loading acceptance window.
void ObserveTitle(CpuContext& cpu) {
    const uint32_t slot=cpu.gpr[13]-0x43C0;
    const uint32_t manager=Memory::Contains(slot,4) ? Memory::Read32(slot) : 0;
    if(!Memory::Contains(manager,8))return;
    const uint32_t scene=Memory::Read32(manager+4);
    if(!Memory::Contains(scene,4)||GuestString(Memory::Read32(scene))!="scTitle")return;
    const ScreenKey key{manager,scene,scene,scene,0,0,0,Surface::Title};
    std::lock_guard<std::mutex> lock(mutex);
    PublishLocked(key);
    if(pending&&pending->epoch==epoch)padPulse|=0x1000;
    pending.reset();
}

bool CurrentSnapshotValid() {
    const bool sameScene = Memory::Contains(published.manager, 8) &&
        Memory::Read32(published.manager + 4) == published.scene;
    if(!sameScene)return false;
    bool samePage=false;
    switch(published.surface) {
    case Surface::Title:
        samePage=Memory::Contains(published.scene,4)&&GuestString(Memory::Read32(published.scene))=="scTitle";
        break;
    case Surface::Css: {
        const uint32_t area=Memory::Contains(published.hand,0xE0) ? Memory::Read32(published.hand+0x9C) : 0;
        samePage=Memory::Contains(area,0x1F8)&&Memory::Read32(area+0x1DC)==0&&
        Memory::Read32(area+0x1F4)==published.task&&Memory::Read32(area)!=1&&
        Memory::Contains(published.hand,0xE0)&&Memory::Contains(published.task,0x5EC)&&
        Memory::Read32(published.hand+0xA4)==published.mode&&
        Memory::Read32(published.task+0x5E8)==published.phase;
        break;
    }
    case Surface::Menu:
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x668)&&
        Memory::Read8(published.hand+0x44)&&Memory::Contains(published.ownerSlot,0x84)&&
        Memory::Read32(published.hand+0x658)==published.ownerSlot&&
        Memory::Read32(published.ownerSlot+0x50)==published.panelDepth&&
        Memory::Read32(published.hand+0x664)==published.phase&&
        Memory::Read32(published.hand+0x63C)==published.task&&
        Memory::Read32(published.hand+0x64C)==published.mode&&
        Memory::Read32(published.hand+0x644)==published.scroll;
        break;
    case Surface::Stage:
        samePage=Memory::Contains(published.task,0x6095)&&Memory::Contains(published.hand,0x60)&&
        Memory::Read32(published.task+0x200)==published.hand&&
        Memory::Read32(published.task+0x224)==0&&
        Memory::Read8(published.task+0x6094)==0&&
        Memory::Read32(published.task+0x228)==published.mode&&
        (published.mode!=2||Memory::Read32(published.task+0x6088)==published.scroll);
        break;
    case Surface::Pause:
        samePage=Memory::Contains(published.task,0x74)&&Memory::Read8(published.task+0x48)==0&&
        static_cast<int32_t>(Memory::Read32(published.task+0x60))<0&&
        Memory::Read32(published.task+0x34)==1&&
        Memory::Read32(published.task+0x70)==1&&
        Memory::Read32(published.task+0x6C)==published.mode&&
        Memory::Read8(published.task+0x64)==published.scroll;
        break;
    case Surface::Rules: {
        const bool more=published.mode==0x100;
        samePage=Memory::Contains(published.hand,more ? 0x6A0 : 0x6B0)&&
        ProcName(published.hand)==(more ? "muProcRule2" : "muProcRule1")&&
        MenuPadEnabled(published.scene,published.hand)&&Memory::Read32(published.hand+0x664)==2&&
        Memory::Contains(published.task,0x29C)&&GuestString(Memory::Read32(published.task))=="muRuleTask"&&
        Memory::Read32(published.hand+0x64C)==published.task+0x48&&
        Memory::Read32(published.task+0x268+(more ? 4 : 0))==published.hand&&
        Memory::Read32(published.task+0x278)==(more ? 1u : 0u)&&
        (more||Memory::Read8(published.hand+0x66C)==published.mode)&&RuleEnabledMask(published.hand,more)==published.scroll&&
        Memory::Read32(published.hand+0x658)==published.ownerSlot&&Memory::Contains(published.ownerSlot,0x84)&&
        Memory::Read32(published.ownerSlot+0x58)==0;
        break;
    }
    case Surface::Options:
        samePage=Memory::Contains(published.hand,0x66C)&&ActiveMainProc(published.scene)==published.hand&&
        ProcName(published.hand)==(published.mode ? "muProcOptDeflick" : "muProcOptScreen")&&
        Memory::Read32(published.hand+0x664)==published.phase&&MenuPadEnabled(published.scene,published.hand)&&
        Memory::Read32(published.hand+0x64C)==published.task&&Memory::Contains(published.task,0xA8)&&
        Memory::Read32(published.hand+0x658)==published.ownerSlot&&Memory::Contains(published.ownerSlot,0x84)&&
        Memory::Read32(published.ownerSlot+0x58)==0;
        break;
    case Surface::ItemSwitch:
        samePage=Memory::Contains(published.hand,0x690)&&ProcName(published.hand)=="muProcItemSwitch"&&
        Memory::Read32(published.hand+0x664)==1&&MenuPadEnabled(published.scene,published.hand)&&
        Memory::Contains(published.task,0x29C)&&GuestString(Memory::Read32(published.task))=="muRuleTask"&&
        Memory::Read32(published.hand+0x64C)==published.task+0x48&&Memory::Read32(published.task+0x270)==published.hand&&
        Memory::Read32(published.task+0x278)==2&&Memory::Read32(published.hand+0x658)==published.ownerSlot&&
        Memory::Contains(published.ownerSlot,0x84)&&Memory::Read32(published.ownerSlot+0x58)==0;
        break;
    case Surface::NameEntry: {
        const uint32_t entry=published.task+0x158;
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x720)&&
        IsNameProc(published.hand)&&Memory::Read32(published.hand+0x664)==0&&
        Memory::Read32(published.hand+0x71C)==published.task&&Memory::Contains(published.task,0x480)&&
        Memory::Read32(published.task+0x9C)==5&&Memory::Read32(published.task+0xA0)==5&&
        Memory::Read32(entry)==1&&Memory::Read32(entry+0x90)==0&&Memory::Read32(entry+0x44)==published.ownerSlot&&
        Memory::Read32(entry+0x3C)==published.mode&&Memory::Read8(entry+0x88)==published.scroll;
        break;
    }
    case Surface::NameDelete: {
        const uint32_t objects=Memory::Contains(published.hand,0x720) ? Memory::Read32(published.hand+0x64C) : 0;
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x720)&&
        ProcName(published.hand)=="muProcName"&&Memory::Read32(published.hand+0x664)==1&&
        MenuPadEnabled(published.scene,published.hand)&&Memory::Read32(published.hand+0x71C)==published.task&&
        Memory::Contains(published.task,0x480)&&Memory::Read16(published.task+0x1EC)==published.mode&&
        Memory::Read16(published.task+0x1EE)==published.scroll&&Memory::Contains(objects,0x2B8)&&
        Memory::Read32(objects+0x2B4)==published.ownerSlot;
        break;
    }
    case Surface::Names:
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x720)&&
        IsNameProc(published.hand)&&Memory::Read32(published.hand+0x664)==0&&
        Memory::Read32(published.hand+0x71C)==published.task&&Memory::Contains(published.task,0x480)&&
        Memory::Read32(published.task+0x9C)==2&&Memory::Read32(published.task+0xA0)==2&&
        (Memory::Read8(published.task+0xB0)&1)&&Memory::Read16(published.task+0x1EE)==published.mode&&
        Memory::Read32(published.task+0x3D8)==published.scroll&&
        Memory::Read32(published.hand+0x658)==published.ownerSlot&&Memory::Contains(published.ownerSlot,0x84)&&
        Memory::Read32(published.ownerSlot+0x58)==0;
        break;
    case Surface::MovieList:
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x688)&&
        ProcName(published.hand)=="muProcMovieAdv"&&(Memory::Read8(published.hand+0x44)&1)&&
        Memory::Read32(published.hand+0x664)==2&&Memory::Read32(published.hand+0x670)==0&&
        Memory::Read32(published.hand+0x674)==published.task&&Memory::Read32(published.hand+0x64C)==published.ownerSlot&&
        Memory::Read8(published.hand+0x67A)==published.mode&&Memory::Read8(published.hand+0x678)==published.scroll;
        break;
    case Surface::Movie:
        samePage=ActiveMainProc(published.scene)==published.hand&&Memory::Contains(published.hand,0x688)&&ProcName(published.hand)=="muProcMovie"&&
        (Memory::Read8(published.hand+0x44)&1)&&Memory::Read32(published.hand+0x664)==2&&
        Memory::Read32(published.hand+0x670)==0&&Memory::Read32(published.hand+0x674)==published.task&&
        Memory::Read32(published.hand+0x64C)==published.ownerSlot&&
        Memory::Read8(published.hand+0x679)==published.mode&&Memory::Read8(published.hand+0x678)==published.scroll;
        break;
    case Surface::Notice:
        samePage=Memory::Contains(published.task,0x118)&&
        Memory::Read32(published.task+0x104)==1&&Memory::Read32(published.task+0x108)==1&&
        Memory::Read32(published.task+0xFC)==published.mode&&
        Memory::Read32(published.task+0x40)==published.hand&&
        Memory::Read32(published.task+0x110)==published.scroll&&
        static_cast<int32_t>(Memory::Read32(published.task+0x114))>=static_cast<int32_t>(published.scroll)&&
        (Memory::Read8(published.task+0x58)&1);
        break;
    case Surface::Result:
        samePage=Memory::Contains(published.task,0x4460)&&
        LocalResultPlayer(published.ownerSlot,published.task,published.hand)&&
        Memory::Read32(published.task+0x78+published.hand*4)>=120&&
        Memory::Read32(published.task+0x40)==4&&
        Memory::Read32(published.task+0x48+published.hand*4)==published.mode&&
        Memory::Read32(published.task+0x658)==0&&Memory::Read8(published.task+0x66A)==0&&
        Memory::Read8(published.task+0x444C)==published.scroll;
        break;
    default:break;
    }
    return sameScene && samePage;
}
} // namespace

void SubmitTap(float x, float y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (FreshLocked()) pending = Tap{x, y, epoch};
}
void Cancel() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    // A UI mode/focus/viewport transition cancels input, not the independently
    // observed game capability. Clearing readiness here would make mode
    // polling alternate between full HUD and menu HUD after every UI cancel.
    if(padPulse||pending)RT_LOGF(RT_TAG_RUNTIME,"[menu touch] UI cancel surface=%u pulse=%04x tap=%u\n",
        static_cast<unsigned>(published.surface),padPulse,pending.has_value());
    ++epoch;
    pending.reset();navigation.reset();noticeNavigation.reset();ResetItemNavigation();padPulse=0;
    ClearPointersLocked();
}
void SubmitPointer(unsigned action,unsigned id,float x,float y) noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if(action==3){ClearPointersLocked();return;}
    if(action==1) {
        for(auto& contact:pointerContacts)if(contact.active&&contact.id==id)contact.active=false;
        pointerAxes={};pointerButtons=0;return;
    }
    if(action>2||!FreshLocked()||published.surface!=Surface::Pause||!(published.mode&1)||
       !std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)return;
    for(auto& contact:pointerContacts)if(contact.active&&contact.id==id){contact.x=x;contact.y=y;return;}
    if(action==0)for(auto& contact:pointerContacts)if(!contact.active){contact={id,x,y,x,y,x,y,0,true,++pointerSerial};return;}
}
bool ReadPointerAxes(unsigned port,int8_t* sx,int8_t* sy,int8_t* cx,int8_t* cy) noexcept {
    if(port||!sx||!sy||!cx||!cy)return false;
    std::lock_guard<std::mutex> lock(mutex);
    try {
        if(!FreshLocked()||published.surface!=Surface::Pause||!CurrentSnapshotValid()) {
            ClearPointersLocked();return false;
        }
    } catch(...) {ClearPointersLocked();return false;}
    if(pointerAxes==std::array<int8_t,4>{})return false;
    *sx=pointerAxes[0];*sy=pointerAxes[1];*cx=pointerAxes[2];*cy=pointerAxes[3];return true;
}
bool IsDirectTouchScreen() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    return FreshLocked();
}
uint16_t ConsumePadButtons(unsigned port) noexcept {
    if (port) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    const uint16_t requested=padPulse;
    if(padPulse||pointerButtons||navigationButtons) {
        try { if(!CurrentSnapshotValid())InvalidateLocked(); }
        catch(...) { InvalidateLocked(); }
    }
    const uint16_t result = FreshLocked() ? (padPulse|pointerButtons|navigationButtons) : 0;
    if(requested)RT_LOGF(RT_TAG_RUNTIME,"[menu touch] pad pulse surface=%u requested=%04x delivered=%04x ready=%u\n",
        static_cast<unsigned>(published.surface),requested,result,ready);
    padPulse = 0;
    return result;
}
void Update() noexcept {
    // The overlay is called on the guest thread after the frame. Scene changes
    // invalidate even if the old menu is never called again.
    try {
        if(!inspecting) {
            if(const auto* active=TryGetCpuContext()) {
                CpuContext cpu=*active;
                ObserveTitle(cpu);
                const uint32_t slot=cpu.gpr[13]-0x43C0;
                const uint32_t manager=Memory::Contains(slot,4) ? Memory::Read32(slot) : 0;
                const uint32_t proc=Memory::Contains(manager,8) ? ActiveMainProc(Memory::Read32(manager+4)) : 0;
                if(proc&&IsNameProc(proc)) {
                    cpu.gpr[3]=proc;
                    inspecting=true;
                    try { ObserveNames(cpu); ObserveNameEntry(cpu); } catch(...) { inspecting=false; throw; }
                    inspecting=false;
                }
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (!FreshLocked() || !CurrentSnapshotValid()) InvalidateLocked();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        InvalidateLocked();
    }
}
void ObserveGuestEntry(uint32_t address, CpuContext* cpu) noexcept {
    if (!cpu || inspecting || (address != kHandMain && address != kSelect && address != kStagePoint &&
        address!=kPauseInput && address!=kResultInput && address!=kNoticeUpdate && address!=kPadUpdate)) return;
    inspecting = true;
    try {
        if(address==kHandMain) ObserveCss(*cpu);
        else if(address==kStagePoint) ObserveStage(*cpu);
        else if(address==kPauseInput) ObservePause(*cpu);
        else if(address==kResultInput) ObserveResult(*cpu);
        else if(address==kNoticeUpdate) ObserveNotice(*cpu);
        else if(address==kPadUpdate) {
            const auto type=ProcName(cpu->gpr[3]);
            static std::vector<uint32_t> seenPadOwners;
            if(std::find(seenPadOwners.begin(),seenPadOwners.end(),cpu->gpr[3])==seenPadOwners.end()) {
                if(seenPadOwners.size()>=32)seenPadOwners.erase(seenPadOwners.begin());
                seenPadOwners.push_back(cpu->gpr[3]);
                RT_LOGF(RT_TAG_RUNTIME,"[menu touch] pad observer proc=%08x type=%s\n",cpu->gpr[3],type.c_str());
            }
            if(type=="muProcMovieAdv")ObserveMovieList(*cpu);
            else if(type=="muProcRule1"||type=="muProcRule2")ObserveRules(*cpu);
            else if(type=="muProcName")ObserveNameDelete(*cpu);
            else if(type=="muProcItemSwitch")ObserveItemSwitch(*cpu);
            else if(type=="muProcOptScreen"||type=="muProcOptDeflick")ObserveOptions(*cpu);
            else ObserveMovie(*cpu);
        }
        else ObserveMenu(*cpu);
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex);
        InvalidateLocked();
        static bool reported=false;
        if(!reported){RT_LOGF(RT_TAG_RUNTIME, "[menu touch] guest probe disabled: %s\n", e.what());reported=true;}
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        InvalidateLocked();
    }
    inspecting = false;
}
} // namespace BrawlMenuTouch
