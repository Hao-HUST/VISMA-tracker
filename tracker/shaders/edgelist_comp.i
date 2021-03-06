#pragma once
namespace feh {
#include <string>
static const std::string edgelist_comp = R"(
#version 430 core

layout(local_size_x = 16, local_size_y = 16) in;
layout(std430, binding=0) buffer EdgeListLayout {
    float edgelist[];
};
// reference on atomic counter:
// https://www.khronos.org/opengl/wiki/Atomic_Counter
layout(binding=1) uniform atomic_uint edgepixel_counter;

uniform sampler2D this_texture;
uniform float z_near = 0.05;
uniform float z_far = 5.0;

const float threshold = 0.1;

// convert normalized depth to actual depth
// reference:
// https://www.opengl.org/discussion_boards/showthread.php/145308-Depth-Buffer-How-do-I-get-the-pixel-s-Z-coord
// http://web.archive.org/web/20130416194336/http://olivers.posterous.com/linear-depth-in-glsl-for-real
float linearize_depth(in float z) {
    if (z == 1.0) return -1;
    return 2.0 * z_near * z_far / (z_far + z_near - (2.0 * z - 1) * (z_far - z_near));
}

void compute_edge_info(ivec2 pos) {
    float value[9];
    value[0] = linearize_depth(texelFetch(this_texture, pos+ivec2(-1,-1), 0).r);
    value[1] = linearize_depth(texelFetch(this_texture, pos+ivec2(-1, 0), 0).r);
    value[2] = linearize_depth(texelFetch(this_texture, pos+ivec2(-1,+1), 0).r);
    value[3] = linearize_depth(texelFetch(this_texture, pos+ivec2(0, -1), 0).r);
    value[4] = linearize_depth(texelFetch(this_texture, pos+ivec2(0,  0), 0).r);
    value[5] = linearize_depth(texelFetch(this_texture, pos+ivec2(0, +1), 0).r);
    value[6] = linearize_depth(texelFetch(this_texture, pos+ivec2(+1,-1), 0).r);
    value[7] = linearize_depth(texelFetch(this_texture, pos+ivec2(+1, 0), 0).r);
    value[8] = linearize_depth(texelFetch(this_texture, pos+ivec2(+1,+1), 0).r);
    float delta = 0.25*(abs(value[1]-value[7]) + abs(value[5]-value[3]) + abs(value[0]-value[8]) + abs(value[2]-value[6]));

    if (value[4] != -1 && delta >= threshold) {
        // fill in edgelist
        uint current_index = atomicCounterIncrement(edgepixel_counter);
        // TODO: need to check range of edgelist buffer
        edgelist[current_index*4 + 0] = float(pos.x);
        edgelist[current_index*4 + 1] = float(pos.y);
        float dy = -(3*value[0]  - 3*value[2] + 10*value[3] - 10*value[5] + 3*value[6] - 3*value[8]);
        float dx = -(3*value[0]  + 10*value[1] + 3*value[2] - 3*value[6] - 10*value[7] - 3*value[8]);
//        float dy = value[5] - value[3];
//        float dx = value[7] - value[1];
        edgelist[current_index*4 + 2] = atan(dy, dx);
        edgelist[current_index*4 + 3] = value[4];

        // float min_depth = z_far;
        // for (int i = 0; i < 9; ++i) {
        //     float tmp = value[i];
        //     if (tmp < min_depth) {
        //         min_depth = tmp;
        //     }
        // }
        // if (min_depth < z_far) {
        //     edgelist[current_index*4 + 3] = min_depth; //linearize_depth(min_depth);
        // }
    }
}

void main() {
    ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = textureSize(this_texture, 0);
    if (uv.x < size.x-1 && uv.y < size.y-1
    && uv.x >= 1 && uv.y >= 1) {
        compute_edge_info(uv);
    }
}

)";
}