#version 440

// -----------------------------------------------------------------------------
// [1] Vertex Attributes, Varying Outputs & Qt-Compatible UBO Prefix
// -----------------------------------------------------------------------------
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord0;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float _pad0;
    float _pad1;
    float _pad2;
} u;

// -----------------------------------------------------------------------------
// [2] Fullscreen / Item Quad Transform
// -----------------------------------------------------------------------------
void main() {
    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = u.qt_Matrix * qt_Vertex;
}
