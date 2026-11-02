#version 440

// lanczos minification, same-sign neighbours share one bilinear fetch, which is exact

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};
layout(binding = 1) uniform sampler2D source;

const float lobes = 3.0;
const float pi = 3.14159265;
// 61 texels at this shrink, which is at most 36 fetches
const float most_shrink = 10.0;
const int most_fetches = 40;

float lanczos(float x)
{
    if (abs(x) < 0.0001) {
        return 1.0;
    }
    if (abs(x) >= lobes) {
        return 0.0;
    }
    float angle = pi * x;
    return lobes * sin(angle) * sin(angle / lobes) / (angle * angle);
}

int axis(float centre, float shrink, out float places[most_fetches], out float weights[most_fetches])
{
    float reach = lobes * shrink;
    int texel = int(ceil(centre - 0.5 - reach));
    int last = int(floor(centre - 0.5 + reach));
    int count = 0;
    while (texel <= last && count < most_fetches) {
        float first = lanczos((float(texel) + 0.5 - centre) / shrink);
        float next = lanczos((float(texel) + 1.5 - centre) / shrink);
        if (texel < last && first * next > 0.0) {
            places[count] = float(texel) + 0.5 + next / (first + next);
            weights[count] = first + next;
            texel += 2;
        } else {
            places[count] = float(texel) + 0.5;
            weights[count] = first;
            texel += 1;
        }
        count += 1;
    }
    return count;
}

void main()
{
    vec2 side = vec2(textureSize(source, 0));
    vec2 centre = qt_TexCoord0 * side;
    vec2 shrink = clamp(vec2(abs(dFdx(centre.x)), abs(dFdy(centre.y))), 1.0, most_shrink);

    float xs[most_fetches], ys[most_fetches], widths[most_fetches], heights[most_fetches];
    int columns = axis(centre.x, shrink.x, xs, widths);
    int rows = axis(centre.y, shrink.y, ys, heights);

    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            float weight = widths[column] * heights[row];
            sum += weight * texture(source, vec2(xs[column], ys[row]) / side);
            total += weight;
        }
    }
    // the kernel's negative lobes overshoot, and the texture is premultiplied
    vec4 colour = sum / total;
    colour.a = clamp(colour.a, 0.0, 1.0);
    colour.rgb = clamp(colour.rgb, vec3(0.0), vec3(colour.a));
    fragColor = colour * qt_Opacity;
}
