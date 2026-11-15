// SPDX-License-Identifier: MIT
#include "ScenerySprites.hpp"
#include "GeneratedSceneryStyle.hpp"
#include <cmath>
#include <cstdio>

namespace grandleon::nintendo64::pyrite {
bool ScenerySprites::add_house(int tile_x, int tile_y, int x, int floor, int z, unsigned family) {
    if (count_ >= houses_.size()) return false;
    if (!vertices_) vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked)*houses_.size()*2U));
    if (!vertices_) return false;
    const auto style = scenery_style::house_at(tile_x,tile_y,family);
    if (!sprites_[style.drawing]) {
        char path[64];
        snprintf(path,sizeof(path),"rom:/scenery_house_%u.sprite",style.drawing);
        sprites_[style.drawing] = sprite_load(path);
    }
    if (!sprites_[style.drawing]) return false;
    houses_[count_++] = {x+static_cast<int>(std::round(style.x*52)),floor,z+static_cast<int>(std::round(style.z*52)),style.drawing,style.mirror,scenery_style::house_side};
    return true;
}
bool ScenerySprites::add_grove(int x, int floor, int z) {
    if (count_ >= houses_.size()) return false;
    if (!vertices_) vertices_ = static_cast<T3DVertPacked*>(malloc_uncached(sizeof(T3DVertPacked)*houses_.size()*2U));
    if (!vertices_) return false;
    if (!sprites_[scenery_style::house_drawing_count]) sprites_[scenery_style::house_drawing_count] = sprite_load("rom:/scenery_grove.sprite");
    if (!sprites_[scenery_style::house_drawing_count]) return false;
    houses_[count_++] = {x, floor, z, scenery_style::house_drawing_count, false, scenery_style::grove_side};
    return true;
}
void ScenerySprites::draw(float ex,float ey,float ez,float tx,float ty,float tz) {
    if (!count_) return;
    float fx=tx-ex, fy=ty-ey, fz=tz-ez;
    const float length=std::sqrt(fx*fx+fy*fy+fz*fz);
    if (length < 0.001F) return;
    fx/=length;fy/=length;fz/=length;
    const float horizontal=std::sqrt(fx*fx+fz*fz);
    if (horizontal < 0.001F) return;
    const float rx=-fz/horizontal, rz=fx/horizontal;
    const float ux=-rz*fy, uy=rz*fx-rx*fz, uz=rx*fy;
    if (dirty_ || std::fabs(fx-last_x_)+std::fabs(fy-last_y_)+std::fabs(fz-last_z_) > 0.0001F) {
        const fm_vec3_t normal={{0,0,1}};
        for (unsigned i=0;i<count_;++i) {
            const auto& h=houses_[i];
            const float half = h.side * 26.0F;
            for (unsigned corner=0;corner<4;++corner) {
                const float sx=(corner==0 || corner==3) ? -half : half;
                const float sy=corner<2 ? -half : half;
                const unsigned at=i*4U+corner;
                auto* pos=t3d_vertbuffer_get_pos(vertices_,static_cast<int>(at));
                pos[0]=static_cast<std::int16_t>(std::round(h.x+rx*sx+ux*sy));
                pos[1]=static_cast<std::int16_t>(std::round(h.y+uy*(sy+half)));
                pos[2]=static_cast<std::int16_t>(std::round(h.z+rz*sx+uz*sy));
                auto* uv=t3d_vertbuffer_get_uv(vertices_,static_cast<int>(at));
                const bool right=corner==1 || corner==2;
                uv[0]=(right != h.mirror ? 63 : 0)<<5;
                uv[1]=(corner<2 ? 63 : 0)<<5;
                *t3d_vertbuffer_get_color(vertices_,static_cast<int>(at))=0xffffffffU;
                auto& pair=vertices_[at/2U];
                if(at&1U) pair.normB=t3d_vert_pack_normal(&normal);else pair.normA=t3d_vert_pack_normal(&normal);
            }
        }
        last_x_=fx;last_y_=fy;last_z_=fz;dirty_=false;
    }
    rdpq_mode_begin();
        rdpq_mode_zbuf(true,true);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(0);
        rdpq_mode_fog(0);
    rdpq_mode_end();
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_TEXTURED|T3D_FLAG_DEPTH));
    for(unsigned i=0;i<count_;++i) {
        rdpq_sprite_upload(TILE0,sprites_[houses_[i].drawing],nullptr);
        t3d_vert_load(vertices_+i*2U,0,4);
        t3d_tri_draw(0,1,2);t3d_tri_draw(0,2,3);t3d_tri_sync();
    }
}
}
