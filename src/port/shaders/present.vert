#version 450
// Draws the game frame as a quad (4-vertex triangle strip, no vertex buffer).
// rect = (x0, y0, x1, y1) of the destination in normalized device coordinates.
layout(push_constant) uniform PushConstants { vec4 rect; } pc;
layout(location = 0) out vec2 uv;

void main()
{
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    uv = corner;
    gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, corner), 0.0, 1.0);
}
