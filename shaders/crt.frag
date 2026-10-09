#version 440
// Efecto CRT: curvatura del tubo, líneas de barrido cuyo grosor depende del brillo,
// máscara de fósforo RGB y esquinas oscurecidas.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float curvature;
    float scanline;
    float mask;
    vec2 sourceSize;
    vec2 outputSize;
} ubuf;
layout(binding = 1) uniform sampler2D source;

void main()
{
    // Curvatura: los bordes se comprimen hacia el centro; lo que cae fuera del tubo queda negro
    vec2 p = uv * 2.0 - 1.0;
    vec2 k = vec2(0.045, 0.065) * ubuf.curvature;
    p *= (1.0 + p.yx * p.yx * k) / (1.0 + k * 0.35);
    vec2 pc = p * 0.5 + 0.5;

    // Borde redondeado y suave del tubo
    vec2 edge = smoothstep(vec2(0.0), vec2(0.012) * (0.4 + ubuf.curvature), 1.0 - abs(p));
    float inside = edge.x * edge.y;

    // Cada línea del juego es un haz: se muestrea en el centro de la línea
    vec2 texel = pc * ubuf.sourceSize;
    float fy = fract(texel.y) - 0.5;
    vec3 col = texture(source, vec2(pc.x, (floor(texel.y) + 0.5) / ubuf.sourceSize.y)).rgb;

    // El haz es más ancho cuanto más brillante (las zonas claras casi tapan el hueco entre líneas).
    // Con pocos píxeles por línea el efecto se atenúa para que no parpadee.
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    float sigma = mix(0.20, 0.42, lum);
    float beam = exp(-(fy * fy) / (2.0 * sigma * sigma));
    float strength = ubuf.scanline * clamp(ubuf.outputSize.y / ubuf.sourceSize.y / 3.0, 0.0, 1.0);
    col *= mix(1.0, beam * 1.25, strength);

    // Máscara de fósforo: franjas verticales rojo / verde / azul de un píxel de pantalla
    float m = mod(floor(gl_FragCoord.x), 3.0);
    vec3 phosphor = vec3(1.0 - ubuf.mask);
    if (m < 0.5) phosphor.r = 1.0;
    else if (m < 1.5) phosphor.g = 1.0;
    else phosphor.b = 1.0;
    col *= phosphor * (1.0 + ubuf.mask * 0.55);

    // Esquinas algo más oscuras, como en un tubo real
    col *= 1.0 - 0.16 * ubuf.curvature * dot(p, p);

    fragColor = vec4(col * inside, 1.0) * ubuf.qt_Opacity;
}
