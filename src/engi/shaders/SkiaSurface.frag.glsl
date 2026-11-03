#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

// Bound directly to the heaps; no set/binding, no pipeline layout
layout(descriptor_heap) uniform texture2D heapTexture2D[];
layout(descriptor_heap) uniform sampler   heapSampler[];

layout(push_constant, std430) uniform Push {
    vec4 src_rect;   // xy = uv offset, zw = uv size inside the UI image
    uint tex;        // heap.image_index(ui_slot)
    uint smp;        // heap.sampler_index(smp_slot)
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = pc.src_rect.xy + vUV * pc.src_rect.zw;
    outColor = texture(sampler2D(heapTexture2D[pc.tex], heapSampler[pc.smp]), uv);
}