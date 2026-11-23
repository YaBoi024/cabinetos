// The console's shaders: eight GLSL sources, linked into five programs by
// Renderer::init (ui.cpp). Included only by ui.cpp, inside its anonymous
// namespace, so the strings keep their internal linkage and need no
// declarations. Moved out of ui.cpp in the restructure (#398); the text is
// unchanged.

// A fullscreen triangle rather than a quad: two triangles share an edge that
// the rasteriser evaluates twice, and one oversized triangle has no seam at all.
// The vertex shader derives its own positions, so there is nothing in the buffer
// but a vertex index.
const char* kQuadVS = R"(#version 300 es
precision highp float;
uniform vec2 uCanvas;
uniform vec4 uRect;     // x, y, w, h in design points
uniform float uShadow;  // blur radius, so the geometry can be grown to fit it
out vec2 vPoint;        // fragment position in design points
void main() {
    // gl_VertexID 0,1,2,3 -> the four corners of a unit quad.
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    // Grow outward so the shadow has room to fall off outside the shape.
    float pad = uShadow * 2.0 + 4.0;
    vec2 p = uRect.xy - pad + corner * (uRect.zw + pad * 2.0);
    vPoint = p;
    // Design points -> clip space. Y is flipped: the canvas is top-left origin.
    vec2 ndc = vec2(p.x / uCanvas.x * 2.0 - 1.0, 1.0 - p.y / uCanvas.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// The rounded-rectangle signed distance function. Negative inside, positive
// outside, and the value IS the distance in points — which is what lets the rim
// be exactly 4pt and the shadow fall off over exactly 26.
const char* kQuadFS = R"(#version 300 es
precision highp float;
in vec2 vPoint;
uniform vec4 uRect;
uniform float uRadius;
uniform vec4 uFill;
uniform vec4 uFillBottom;    // equals uFill for a flat fill
uniform float uBorder;
uniform vec4 uBorderColor;
uniform vec2 uShadowParams;  // blur, offsetY
uniform vec4 uShadowColor;
uniform vec4 uEdgeLight;     // alpha 0 = off
uniform int uShape;          // 0 box, 1 heart, 2 trophy, 3 bolt, 4 book (ui::Rect::Shape)
out vec4 fragColor;

float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// The heart is Inigo Quilez's exact 2D distance function
// (iquilezles.org/articles/distfunctions2d), y pointing up.
float dot2(vec2 v) { return dot(v, v); }
float heartSDF(vec2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) *
           sign(p.x - p.y);
}

float boxSDF(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// The RetroAchievements cup (#74), y pointing down: a bowl with a flat rim, a
// ring of a handle each side, a stem and a foot. About 1.45 wide and 1.1
// tall in its own units, centred. Built from exact pieces like the heart, so
// an outline of it is as clean as the heart's; the bowl's ellipse is a
// scaled circle, near enough at the size it is drawn.
float trophySDF(vec2 p) {
    vec2 s = p + vec2(0.0, -0.25);
    float bowl = max(length(vec2(s.x, (s.y + 0.80) / 1.3)) - 0.5, -(s.y + 0.80));
    float handle = abs(length(vec2(abs(s.x) - 0.50, s.y + 0.62)) - 0.16) - 0.055;
    float stem = boxSDF(s - vec2(0.0, -0.06), vec2(0.07, 0.16));
    float foot = boxSDF(s - vec2(0.0, 0.20), vec2(0.28, 0.07)) - 0.02;
    return min(min(bowl, handle), min(stem, foot));
}

// A charging bolt (#293), y pointing down: six corners, 0.56 wide and 1 tall,
// centred. Inigo Quilez's exact polygon distance (iquilezles.org,
// distfunctions2d, sdPolygon).
float boltSDF(vec2 p) {
    vec2 v[6] = vec2[6](vec2(0.12, -0.5), vec2(-0.28, 0.06), vec2(-0.02, 0.06),
                        vec2(-0.12, 0.5), vec2(0.28, -0.08), vec2(0.02, -0.08));
    float d = dot(p - v[0], p - v[0]);
    float s = 1.0;
    int j = 5;
    for (int i = 0; i < 6; i++) {
        vec2 e = v[j] - v[i];
        vec2 w = p - v[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 c = bvec3(p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x);
        if (all(c) || all(not(c))) s *= -1.0;
        j = i;
    }
    return s * sqrt(d);
}

// An open book (#364), y pointing down: two pages meeting at the spine, each
// a quadrilateral whose top and foot dip towards it. 1.4 wide and 0.92 tall
// in its own units, centred, with a gap down the middle so it reads as two
// pages at a small size. The right page, mirrored; Inigo Quilez's exact
// polygon distance as the bolt's.
float bookSDF(vec2 p) {
    p.x = abs(p.x);
    vec2 v[4] = vec2[4](vec2(0.06, -0.34), vec2(0.70, -0.46), vec2(0.70, 0.34),
                        vec2(0.06, 0.46));
    float d = dot(p - v[0], p - v[0]);
    float s = 1.0;
    int j = 3;
    for (int i = 0; i < 4; i++) {
        vec2 e = v[j] - v[i];
        vec2 w = p - v[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bvec3 c = bvec3(p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x);
        if (all(c) || all(not(c))) s *= -1.0;
        j = i;
    }
    return s * sqrt(d);
}

// The distance to whichever shape this is, in design points, from the centre.
// The heart and the cup fit their rectangle's shorter side, centred.
float shapeSDF(vec2 p, vec2 halfSize, float r) {
    if (uShape == 4) {
        float k = min(2.0 * halfSize.x / 1.4, 2.0 * halfSize.y / 0.92);
        return bookSDF(p / k) * k - r;
    }
    if (uShape == 3) {
        float k = min(2.0 * halfSize.y, 2.0 * halfSize.x / 0.56);
        return boltSDF(p / k) * k - r;
    }
    if (uShape == 2) {
        float k = 2.0 * min(halfSize.x, halfSize.y) / 1.45;
        return trophySDF(p / k) * k - r;
    }
    if (uShape == 1) {
        // The heart spans 1.21 wide and 1.10 tall in its own units, point at 0.
        float k = 2.0 * min(halfSize.x, halfSize.y) / 1.21;
        vec2 q = vec2(p.x, -p.y) / k + vec2(0.0, 0.552);
        return heartSDF(q) * k - r;
    }
    return roundedBoxSDF(p, halfSize, r);
}

void main() {
    vec2 center = uRect.xy + uRect.zw * 0.5;
    // Not `half`: that is a reserved word in GLSL ES and the compiler rejects
    // it outright.
    vec2 halfSize = uRect.zw * 0.5;
    float r = min(uRadius, min(halfSize.x, halfSize.y));

    float d = shapeSDF(vPoint - center, halfSize, r);

    // One point of feathering. The canvas is scaled to the panel, so this is
    // deliberately in design points and not in pixels: the softness of an edge
    // should look the same on 1080p and 4K, not be twice as sharp on one.
    float aa = 1.0;
    float shapeAlpha = 1.0 - smoothstep(-aa, aa, d);

    vec4 color = vec4(0.0);

    // Shadow first, behind everything, offset downward and blurred by feeding
    // the SDF through a wider smoothstep.
    if (uShadowParams.x > 0.0) {
        float sd = shapeSDF(vPoint - center - vec2(0.0, uShadowParams.y), halfSize, r);
        float sa = 1.0 - smoothstep(-uShadowParams.x, uShadowParams.x, sd);
        // Square it: a linear falloff reads as a grey halo with a visible edge,
        // where a real shadow is dense near the object and fades fast.
        sa = sa * sa;
        color = vec4(uShadowColor.rgb, uShadowColor.a * sa);
    }

    // Fill, composited over the shadow.
    //
    // A VERTICAL GRADIENT, because a large flat panel reads as a hole punched
    // in the screen rather than as a surface. uFillBottom equals uFill unless
    // the caller asked for one, so every existing shape is unchanged.
    float ny = clamp((vPoint.y - uRect.y) / max(uRect.w, 1.0), 0.0, 1.0);
    vec4 fillColor = mix(uFill, uFillBottom, ny);
    vec4 fill = vec4(fillColor.rgb, fillColor.a * shapeAlpha);
    color.rgb = mix(color.rgb, fill.rgb, fill.a);
    color.a = color.a + fill.a * (1.0 - color.a);

    // The rim, drawn INSIDE the edge so a focused card does not grow by its own
    // border width. Band between -border and 0 in SDF space.
    if (uBorder > 0.0) {
        float inner = 1.0 - smoothstep(-uBorder - aa, -uBorder + aa, d);
        float band = shapeAlpha - inner;
        vec4 b = vec4(uBorderColor.rgb, uBorderColor.a * band);
        color.rgb = mix(color.rgb, b.rgb, b.a);
        color.a = color.a + b.a * (1.0 - color.a);
    }

    // A HIGHLIGHT ALONG THE TOP EDGE ONLY, as if lit from above. It is the
    // cheapest thing that makes a panel read as a raised surface rather than a
    // flat shape, and it is what the rim alone cannot do — a rim of even weight
    // all the way round says "outline", not "edge catching the light".
    if (uEdgeLight.a > 0.0) {
        float lw = 2.0;
        float lightInner = 1.0 - smoothstep(-lw - aa, -lw + aa, d);
        float lightBand = shapeAlpha - lightInner;
        // Fade out over the top third, so the sides do not get a stripe.
        float topness = 1.0 - smoothstep(0.0, 0.33, ny);
        vec4 e = vec4(uEdgeLight.rgb, uEdgeLight.a * lightBand * topness);
        color.rgb = mix(color.rgb, e.rgb, e.a);
        color.a = color.a + e.a * (1.0 - color.a);
    }

    // DITHERED, like the backdrop: a large dark panel's gradient bands too.
    // It reaches the screen in proportion to the shape's own alpha, so a
    // faint tint adds next to nothing and an opaque panel gets all of it.
    vec2 q = floor(gl_FragCoord.xy);
    float n1 = fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453);
    float n2 = fract(sin(dot(q, vec2(39.3468, 11.1353))) * 24634.6345);
    color.rgb += (n1 + n2 - 1.0) / 255.0;
    fragColor = color;
}
)";

const char* kBackdropVS = R"(#version 300 es
precision highp float;
out vec2 vUV;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vUV = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Three stops with the middle one at 0.55, which is the icon generator's own
// value and the detail this document previously omitted.
const char* kBackdropFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform vec3 uTop;
uniform vec3 uMid;
uniform vec3 uBottom;
uniform float uMidStop;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    float t = 1.0 - vUV.y;  // vUV.y is 0 at the bottom in clip space
    vec3 c = t < uMidStop
        ? mix(uTop, uMid, t / uMidStop)
        : mix(uMid, uBottom, (t - uMidStop) / (1.0 - uMidStop));
    // DITHERED, as the letterbox glow is, and for the same reason: a long
    // near-black ramp bands on an 8-bit panel. MMagTech, 2026-09-27, saw
    // lines in it, worst in Dark and while a colour change fades, when the
    // bands step from one level to the next. Half a level of static noise
    // either way, triangular, never animated.
    vec2 q = floor(gl_FragCoord.xy);
    float n1 = fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453);
    float n2 = fract(sin(dot(q, vec2(39.3468, 11.1353))) * 24634.6345);
    c += (n1 + n2 - 1.0) / 255.0;
    fragColor = vec4(c, uAlpha);
}
)";

// One textured quad. The same shader serves glyphs and pictures: a glyph atlas
// is a single-channel coverage mask that tints, a cover or a core's frame is
// full colour that does not. One flag decides which, rather than two shaders
// that would drift apart.
const char* kTexturedVS = R"(#version 300 es
precision highp float;
uniform vec2 uCanvas;
uniform vec4 uRect;
uniform vec4 uUV;
// Quarter turns counter-clockwise, 0 to 3, for a vertical arcade board that
// renders sideways and asks the frontend to turn the picture round.
//
// IT IS DONE HERE BECAUSE IT CANNOT BE DONE IN uUV. A ninety-degree turn maps
// the destination's x to the source's y, and an axis-aligned u0,v0,u1,v1
// rectangle has no way to say that however the four numbers are ordered —
// swapping them flips, it never transposes. So the turn is applied to the
// quad's CORNER, before the corner is used to look up a texture coordinate.
//
// Which means it composes with whatever uUV already says rather than fighting
// it: uUV goes on locating the picture inside a larger target and flipping a
// bottom-up hardware frame, and this turns the picture it located.
//
// THE ORDER IS THE WHOLE POINT, AND IT IS NOT CABINET'S. Cabinet rotates the
// texture coordinates AFTER flipping them, and says in as many words that the
// two "never combine today" — its rotations are arcade boards and its flipped
// frames come from the two GL cores, which do not rotate. That is not true
// here: Ikaruga is a vertical NAOMI board, so on this console it is a
// hardware-rendered frame that arrives bottom-row-first AND asks for a quarter
// turn, and a turn applied to already-flipped coordinates turns the wrong way.
//
// So the turn is applied to the corner FIRST, in the picture's own space, and
// the uv rectangle maps that into the texture afterwards. Read it as: uRot
// says which part of the PICTURE this corner shows, and uUV says where that
// part of the picture lives in memory. Neither has to know about the other.
uniform int uRot;
out vec2 vUV;
out vec2 vPoint;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vec2 p = uRect.xy + corner * uRect.zw;
    // Where this corner of the destination reads from in the source picture.
    // Turning the PICTURE one quarter anticlockwise means reading the source
    // the other way: the destination's top-left corner takes the source's
    // top-right, which is (1 - y, x).
    vec2 s = corner;
    if (uRot == 1) s = vec2(1.0 - corner.y, corner.x);
    else if (uRot == 2) s = vec2(1.0 - corner.x, 1.0 - corner.y);
    else if (uRot == 3) s = vec2(corner.y, 1.0 - corner.x);
    vUV = mix(uUV.xy, uUV.zw, s);
    vPoint = p;
    vec2 ndc = vec2(p.x / uCanvas.x * 2.0 - 1.0, 1.0 - p.y / uCanvas.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kTexturedFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
in vec2 vPoint;
uniform sampler2D uTex;
uniform vec4 uTint;
uniform int uSingleChannel;
uniform float uLod;
// The rounded box this draw is clipped to, which is NOT the same rectangle as
// the one being drawn: a cover is fitted or overflowed inside a card, and both
// have to stop at the card's rounded edge. Zero size means no clip.
uniform vec4 uClip;
uniform float uClipRadius;
// Take the texture's colour and ignore its alpha.
//
// For a PICTURE that is what you want, and for a game's frame it is the only
// correct reading: the alpha channel of an emulated machine's framebuffer is
// the machine's own business, not an instruction about how to composite it.
// PPSSPP hands back a frame whose alpha is whatever the PSP game left in it,
// and Lumines leaves it at nearly zero — so the picture drew at 1.5% of its
// brightness, which reads as a core that renders black rather than as a
// blending mistake. The whole 1920x1080 capture peaked at RGB (4,4,4).
uniform int uOpaque;
out vec4 fragColor;

float clipSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 t = uLod > 0.0 ? textureLod(uTex, vUV, uLod) : texture(uTex, vUV);
    vec4 c = uSingleChannel == 1
        // Coverage in red, colour from the tint. Straight alpha, composited by
        // the same blend function everything else uses.
        ? vec4(uTint.rgb, uTint.a * t.r)
        : vec4(t.rgb * uTint.rgb, (uOpaque == 1 ? 1.0 : t.a) * uTint.a);

    if (uClip.z > 0.0) {
        vec2 center = uClip.xy + uClip.zw * 0.5;
        vec2 halfSize = uClip.zw * 0.5;
        float r = min(uClipRadius, min(halfSize.x, halfSize.y));
        float d = clipSDF(vPoint - center, halfSize, r);
        c.a *= 1.0 - smoothstep(-1.0, 1.0, d);
    }
    fragColor = c;
}
)";

// Frosted glass: the scene sampled at a coarse mip level under a rounded
// panel, tinted. The rounding is the same SDF the solid path uses, so a glass
// panel and a solid one have identical edges — which matters, because they sit
// next to each other constantly.
const char* kGlassFS = R"(#version 300 es
precision highp float;
in vec2 vPoint;
uniform vec2 uCanvas;
uniform vec4 uRect;
uniform float uRadius;
uniform vec4 uTint;
uniform sampler2D uScene;
uniform float uLod;
// The screen's transition, so a panel fades with the words on it.
uniform float uAlpha;
out vec4 fragColor;

float glassSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 center = uRect.xy + uRect.zw * 0.5;
    vec2 halfSize = uRect.zw * 0.5;
    float r = min(uRadius, min(halfSize.x, halfSize.y));
    float d = glassSDF(vPoint - center, halfSize, r);
    float a = 1.0 - smoothstep(-1.0, 1.0, d);
    if (a <= 0.0) discard;

    // The scene texture covers the whole canvas, so the sample position is
    // just this fragment's place on it. Y flips: GL's origin is bottom-left
    // and the canvas is top-left.
    vec2 uv = vec2(vPoint.x / uCanvas.x, 1.0 - vPoint.y / uCanvas.y);
    vec3 behind = textureLod(uScene, uv, uLod).rgb;

    // Tint OVER the blur, not mixed into it: the tint is a veil the art shows
    // through, which is what makes it read as glass rather than as paint.
    vec3 c = mix(behind, uTint.rgb, uTint.a);
    // DITHERED AGAIN: the blur averages the backdrop's dither away, so the
    // bands come back inside a glass row unless the glass adds its own.
    vec2 q = floor(gl_FragCoord.xy);
    float n1 = fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453);
    float n2 = fract(sin(dot(q, vec2(39.3468, 11.1353))) * 24634.6345);
    c += (n1 + n2 - 1.0) / 255.0;
    fragColor = vec4(c, a * uAlpha);
}
)";

// The letterbox glow: in-software bias lighting for the dead space a retro
// aspect ratio leaves inside a modern panel, where a physical strip behind the
// television cannot reach.
//
// Every number here is the reference implementation's, not invented:
//
//   * WHITE, deliberately. It "adds luminance without hue, so it can never
//     clash with whatever the game is rendering", and the panel's own
//     calibration keeps it matched to the picture's white point for free.
//   * The ramp is `peak * (1 - t^1.7)`, not linear. The shallow exponent gives
//     a flat start so brightness carries further toward the physical edge
//     instead of collapsing early.
//   * The side bars span the FULL height and the top and bottom bars only the
//     picture's width, so the corners belong to the sides.
//   * It starts exactly at the fitted picture edge with no blur, so not one
//     game pixel is ever covered.
//
// And the part that would have been missed: A LONG NEAR-BLACK RAMP BANDS on an
// 8-bit panel, visibly. A static noise dither is composited into the glow,
// masked by the same ramp so it fades with it — a quiet dither rather than a
// texture of its own. Static, never animated.
const char* kGlowFS = R"(#version 300 es
precision highp float;
in vec2 vPoint;
uniform vec2 uCanvas;
uniform vec4 uPicture;   // the fitted game rect, in design points
uniform float uPeak;
uniform float uOpaque;   // 1: the dead space painted plain black, edge exact, for the glow over it (#692)
out vec4 fragColor;

void main() {
    vec2 p = vPoint;
    float left = uPicture.x;
    float right = uPicture.x + uPicture.z;
    float top = uPicture.y;
    float bottom = uPicture.y + uPicture.w;

    // t is 0 at the picture's edge and 1 at the screen's, per direction, so
    // each bar ramps across whatever dead space it actually has.
    float t = -1.0;
    if (p.x < left) {
        t = left > 0.0 ? (left - p.x) / left : 1.0;
    } else if (p.x > right) {
        float room = uCanvas.x - right;
        t = room > 0.0 ? (p.x - right) / room : 1.0;
    } else if (p.y < top) {
        t = top > 0.0 ? (top - p.y) / top : 1.0;
    } else if (p.y > bottom) {
        float room = uCanvas.y - bottom;
        t = room > 0.0 ? (p.y - bottom) / room : 1.0;
    } else {
        discard;  // inside the picture; never cover a game pixel
    }
    if (uOpaque > 0.5) {
        // Black as the backdrop and every panel draw it, with their dither, so the glow over it comes
        // out as on the host's own screen.
        vec2 q = floor(gl_FragCoord.xy);
        float n1 = fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453);
        float n2 = fract(sin(dot(q, vec2(39.3468, 11.1353))) * 24634.6345);
        fragColor = vec4(vec3((n1 + n2 - 1.0) / 255.0), 1.0);
        return;
    }

    float ramp = 1.0 - pow(clamp(t, 0.0, 1.0), 1.7);

    // Static hash on the fragment position. Not time-varying: an animated
    // dither in the corner of a dark room is a shimmer, which is worse than
    // the banding it fixes.
    float n = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);

    // Additive, scaled by the peak and masked by the same ramp, so it stays a
    // dither rather than becoming visible texture.
    float a = uPeak * ramp * (1.0 + 0.25 * n);
    fragColor = vec4(1.0, 1.0, 1.0, a);
}
)";
