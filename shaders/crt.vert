#version 440
// Efecto CRT de EmulatorView: el vértice solo pasa la coordenada de textura.
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 uv;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float curvature;   // 0 = pantalla plana, 1 = tubo curvo
    float scanline;    // fuerza de las líneas de barrido (0..1)
    float mask;        // fuerza de la máscara de fósforo RGB (0..1)
    vec2 sourceSize;   // tamaño del cuadro del juego en píxeles
    vec2 outputSize;   // tamaño en pantalla, en píxeles reales
} ubuf;

void main()
{
    uv = qt_MultiTexCoord0;
    gl_Position = ubuf.qt_Matrix * qt_Vertex;
}
