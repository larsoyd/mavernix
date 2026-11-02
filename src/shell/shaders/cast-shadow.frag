#version 440

// white letters in ink over the shadow they cast, repainted passes times so the dark builds up
// the gaussian stops at reach, with a deviation of half that

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 ink;
    vec4 shadowColor;
    vec2 texel;
    float reach;
    float drop;
    float passes;
};
layout(binding = 1) uniform sampler2D source;

// qt may draw them with lcd fringes, the mean of the three is plain grey cover
float cover(vec2 place)
{
    vec3 letters = texture(source, place).rgb;
    return (letters.r + letters.g + letters.b) / 3.0;
}

float castCover()
{
    int edge = int(reach);
    float spread = max(reach * reach * 0.5, 0.001);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -edge; y <= edge; y++) {
        for (int x = -edge; x <= edge; x++) {
            float weight = exp(-float(x * x + y * y) / spread);
            sum += weight * cover(qt_TexCoord0 + texel * vec2(float(x), float(y) - drop));
            total += weight;
        }
    }
    return sum / total;
}

void main()
{
    vec4 shape = ink * cover(qt_TexCoord0);
    vec4 shadow = shadowColor * castCover();
    vec4 painted = vec4(0.0);
    for (int pass = 0; pass < int(passes); pass++) {
        painted = shadow + painted * (1.0 - shadow.a);
        painted = shape + painted * (1.0 - shape.a);
    }
    fragColor = painted * qt_Opacity;
}
