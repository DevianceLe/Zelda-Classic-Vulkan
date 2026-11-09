#version 450
// Samples the uploaded game frame (already palette-converted to BGRA on the CPU).
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main()
{
    color = texture(frame, uv);
}
