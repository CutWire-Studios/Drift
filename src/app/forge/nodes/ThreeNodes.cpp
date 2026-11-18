#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

InputDef fromIn()
{
    return colorIn("from", "From", rgba(0, 0, 0, 1));
}

InputDef toIn()
{
    return colorIn("to", "To", rgba(1, 1, 1, 1));
}

InputDef angle(const char *id, const char *label, double def = 0, double min = -180, double max = 180)
{
    return widget(floatIn(id, label, def, min, max), "angle");
}

InputDef perspective()
{
    return hint(floatIn("perspective", "Perspective", 1, 0.1, 3), "Higher is a wider lens with stronger depth.");
}

Q camera(EmitCtx &c)
{
    c.helper("rayQuad");
    c.helper("rot3");
    const Q asp = c.aspect();
    const Q perspective = c.in("perspective");
    return QStringLiteral(R"(float asp = %1;
    float k = max(%2, 0.05);
    vec3 rd = vec3((uv - 0.5) * vec2(asp, 1.0) * k, 1.0);
    float D = 1.0 / k;)").arg(asp, perspective);
}

}

void registerThreeNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};
    const QList<Kind> transitionOnly = {Kind::Transition};

    NodeDef rotate3d = node("rotate_3d", "3D rotate", "three",
        "Tilt and spin the picture in 3D space like a card, with real perspective.");
    rotate3d.inputs = {imageIn(), angle("rx", "Tilt (X)", 0), angle("ry", "Turn (Y)", 25), angle("rz", "Spin (Z)", 0),
                       floatIn("distance", "Push back", 0.3, -0.5, 5), vecIn("offset", "Move", 0, 0, -1, 1), perspective()};
    rotate3d.options = {selectOpt("back", "Back side",
                                  {choice("show", "See through"), choice("dark", "Darker"), choice("hide", "Hidden")}, "show")};
    rotate3d.outputs = {out("image", "Image", SocketType::Color), out("facing", "Facing camera", SocketType::Float)};
    rotate3d.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q back = c.optString("back");
        const Q cam = camera(c);
        const Q rx = c.in("rx");
        const Q ry = c.in("ry");
        const Q rz = c.in("rz");
        const Q offset = c.in("offset");
        const Q distance = c.in("distance");
        const Q hit = cam + QStringLiteral(R"(
    mat3 R = rot3(vec3(%1, %2, %3));
    vec2 off = %4;
    vec3 ctr = vec3(off.x * asp, off.y, D + %5);
    vec4 h = rayQuad(rd, ctr, R * vec3(1.0, 0.0, 0.0), R * vec3(0.0, 1.0, 0.0), asp, 1.0);)").arg(rx, ry, rz, offset, distance);
        const Q image = c.in("image", "h.xy");
        Q backFace;
        if (back == QLatin1String("hide"))
            backFace = "if (h.w < 0.5) return vec4(0.0);";
        else if (back == QLatin1String("dark"))
            backFace = "if (h.w < 0.5) col.rgb *= 0.45;";
        return {{"image", hit + "\n    if (h.z < 0.0) return vec4(0.0);\n    vec4 col = " + image + ";\n    " + backFace
                              + "\n    return col;"},
                {"facing", hit + "\n    return h.z < 0.0 ? 0.0 : h.w;"}};
    };

    NodeDef sphere = node("sphere", "Sphere", "three", "Wrap the picture around a ball. Connect Time to Turn to make it spin.");
    sphere.inputs = {imageIn(), floatIn("radius", "Size", 0.42, 0.05, 1.5), widget(vecIn("center", "Centre", 0.5, 0.5, 0, 1), "point"),
                     angle("ry", "Turn", 0, -360, 360), angle("rx", "Tilt", 10), floatIn("shading", "Shading", 0.6, 0, 1)};
    sphere.outputs = {out("image", "Image", SocketType::Color), out("mask", "Ball mask", SocketType::Float)};
    sphere.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot3");
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q radius = c.in("radius");
        const Q d = QStringLiteral(R"(float asp = %1;
    vec2 d = (uv - %2) * vec2(asp, 1.0) / max(%3, 0.0001);
    float r2 = dot(d, d);)").arg(asp, center, radius);
        const Q rx = c.in("rx");
        const Q ry = c.in("ry");
        const Q image = c.in("image", "vec2(lon, lat)");
        const Q shading = c.in("shading");
        return {{"image", d + QStringLiteral(R"(
    if (r2 > 1.0) return vec4(0.0);
    vec3 n = vec3(d, -sqrt(1.0 - r2));
    vec3 s = rot3(vec3(%1, %2, 0.0)) * n;
    float lon = atan(s.x, -s.z) / 6.2831853 + 0.5;
    float lat = asin(clamp(s.y, -1.0, 1.0)) / 3.14159265 + 0.5;
    vec4 col = %3;
    float light = clamp(dot(n, normalize(vec3(-0.4, -0.5, -0.75))), 0.0, 1.0);
    col.rgb *= mix(1.0, 0.25 + 0.85 * light, %4);
    float edge = 1.0 - smoothstep(0.97, 1.0, r2);
    return vec4(col.rgb, col.a * edge);)").arg(rx, ry, image, shading)},
                {"mask", d + "\n    return 1.0 - smoothstep(0.97, 1.0, r2);"}};
    };

    NodeDef cardFlip = node("card_flip", "Card flip", "three", "The old clip flips over like a card to show the new one on its back.");
    cardFlip.kinds = transitionOnly;
    cardFlip.inputs = {fromIn(), toIn(), clockIn("p", "Progress"),
                       hint(floatIn("lift", "Lift", 0.4, 0, 2), "How far the card moves away mid-flip."), perspective()};
    cardFlip.options = {selectOpt("axis", "Flip", {choice("y", "Sideways"), choice("x", "Top over bottom")}, "y")};
    cardFlip.outputs = imageOut;
    cardFlip.emitFn = [](EmitCtx &c, Stage) -> Out {
        const bool y = c.optString("axis") != QLatin1String("x");
        const Q cam = camera(c);
        const Q p = c.in("p");
        const Q lift = c.in("lift");
        const Q from = c.in("from", "h.xy");
        const Q to = c.in("to", y ? "vec2(1.0 - h.x, h.y)" : "vec2(h.x, 1.0 - h.y)");
        return {{"image", cam + QStringLiteral(R"(
    float p = clamp(%1, 0.0, 1.0);
    float a = p * 3.14159265;
    mat3 R = %2;
    vec3 ctr = vec3(0.0, 0.0, D + %3 * sin(p * 3.14159265));
    vec4 h = rayQuad(rd, ctr, R * vec3(1.0, 0.0, 0.0), R * vec3(0.0, 1.0, 0.0), asp, 1.0);
    if (h.z < 0.0) return vec4(0.0);
    if (h.w > 0.5) return %4;
    return %5;)").arg(p, y ? "rotY(a)" : "rotX(-a)", lift, from, to)}};
    };

    NodeDef cube = node("cube", "Cube", "three", "Both clips sit on the sides of a turning cube.");
    cube.kinds = transitionOnly;
    cube.inputs = {fromIn(), toIn(), clockIn("p", "Progress"), floatIn("unzoom", "Pull back", 0.5, 0, 3), perspective()};
    cube.options = {selectOpt("direction", "Turn",
                              {choice("left", "To the left"), choice("right", "To the right"), choice("up", "Upwards"),
                               choice("down", "Downwards")},
                              "left")};
    cube.outputs = imageOut;
    cube.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q dir = c.optString("direction");
        const bool horiz = dir == QLatin1String("left") || dir == QLatin1String("right");
        const QMap<Q, Q> setup = {
            {"left", "mat3 R = rotY(a); vec3 offB = vec3(asp * 0.5, 0.0, 0.0); vec3 axB = vec3(0.0, 0.0, 1.0); vec3 ayB = vec3(0.0, 1.0, 0.0);"},
            {"right", "mat3 R = rotY(-a); vec3 offB = vec3(-asp * 0.5, 0.0, 0.0); vec3 axB = vec3(0.0, 0.0, -1.0); vec3 ayB = vec3(0.0, 1.0, 0.0);"},
            {"up", "mat3 R = rotX(-a); vec3 offB = vec3(0.0, 0.5, 0.0); vec3 axB = vec3(1.0, 0.0, 0.0); vec3 ayB = vec3(0.0, 0.0, 1.0);"},
            {"down", "mat3 R = rotX(a); vec3 offB = vec3(0.0, -0.5, 0.0); vec3 axB = vec3(1.0, 0.0, 0.0); vec3 ayB = vec3(0.0, 0.0, -1.0);"}};
        const Q cam = camera(c);
        const Q p = c.in("p");
        const Q unzoom = c.in("unzoom");
        const Q from = c.in("from", "ha.xy");
        const Q to = c.in("to", "hb.xy");
        return {{"image", cam + QStringLiteral(R"(
    float p = clamp(%1, 0.0, 1.0);
    float a = p * 1.5707963;
    float hd = %2;
    %3
    vec3 C = vec3(0.0, 0.0, D + hd + %4 * sin(p * 3.14159265));
    vec4 ha = rayQuad(rd, C + R * vec3(0.0, 0.0, -hd), R * vec3(1.0, 0.0, 0.0), R * vec3(0.0, 1.0, 0.0), asp, 1.0);
    vec4 hb = rayQuad(rd, C + R * offB, R * axB, R * ayB, asp, 1.0);
    bool okA = ha.z > 0.0 && ha.w > 0.5;
    bool okB = hb.z > 0.0 && hb.w > 0.5;
    if (okA && (!okB || ha.z <= hb.z)) return %5;
    if (okB) return %6;
    return vec4(0.0);)").arg(p, horiz ? "asp * 0.5" : "0.5", setup.contains(dir) ? setup.value(dir) : setup.value("left"), unzoom, from, to)}};
    };

    NodeDef doors = node("doors", "Doors", "three", "The old clip splits down the middle and swings open like double doors.");
    doors.kinds = transitionOnly;
    doors.inputs = {fromIn(), toIn(), clockIn("p", "Progress"), perspective()};
    doors.outputs = imageOut;
    doors.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q cam = camera(c);
        const Q p = c.in("p");
        const Q from1 = c.in("from", "vec2(hl.x * 0.5, hl.y)");
        const Q from2 = c.in("from", "vec2(0.5 + hr.x * 0.5, hr.y)");
        const Q to = c.in("to", "(uv - 0.5) / (0.85 + 0.15 * p) + 0.5");
        return {{"image", cam + QStringLiteral(R"(
    float p = clamp(%1, 0.0, 1.0);
    float a = p * 1.5707963;
    mat3 RL = rotY(a);
    mat3 RR = rotY(-a);
    vec3 hingeL = vec3(-asp * 0.5, 0.0, D);
    vec3 hingeR = vec3(asp * 0.5, 0.0, D);
    vec4 hl = rayQuad(rd, hingeL + RL * vec3(asp * 0.25, 0.0, 0.0), RL * vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), asp * 0.5, 1.0);
    vec4 hr = rayQuad(rd, hingeR + RR * vec3(-asp * 0.25, 0.0, 0.0), RR * vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), asp * 0.5, 1.0);
    bool okL = hl.z > 0.0;
    bool okR = hr.z > 0.0;
    float shade = 1.0 - 0.45 * sin(a);
    if (okL && (!okR || hl.z <= hr.z)) {
        vec4 col = %2;
        return vec4(col.rgb * shade, col.a);
    }
    if (okR) {
        vec4 col = %3;
        return vec4(col.rgb * shade, col.a);
    }
    return %4;)").arg(p, from1, from2, to)}};
    };

    NodeDef pageCurl = node("page_curl", "Page curl", "three",
        "The old clip peels away like a turning page, revealing the new one underneath.");
    pageCurl.kinds = transitionOnly;
    pageCurl.inputs = {fromIn(), toIn(), clockIn("p", "Progress"), angle("angle", "Direction", 200, 0, 360),
                       floatIn("radius", "Curl size", 0.12, 0.02, 0.4), floatIn("shadow", "Shadow", 0.5, 0, 1)};
    pageCurl.outputs = imageOut;
    pageCurl.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q angle = c.in("angle");
        const Q radius = c.in("radius");
        const Q p = c.in("p");
        const Q from1 = c.in("from", "pg");
        const Q from3 = c.in("from");
        const Q from4 = c.in("from", "pg");
        const Q to = c.in("to");
        const Q shadow = c.in("shadow");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 q = uv * vec2(asp, 1.0);
    float ang = radians(%2);
    // The curl travels along -d; paper beyond the axis (x > A) is rolled around a cylinder.
    vec2 d = -vec2(cos(ang), sin(ang));
    float r = %3;
    float x0 = dot(vec2(0.0), d);
    float x1 = dot(vec2(asp, 0.0), d);
    float x2 = dot(vec2(0.0, 1.0), d);
    float x3 = dot(vec2(asp, 1.0), d);
    float xmin = min(min(x0, x1), min(x2, x3));
    float xmax = max(max(x0, x1), max(x2, x3));
    float A = mix(xmax, xmin - 3.14159265 * r, clamp(%4, 0.0, 1.0));
    float x = dot(q, d);
    vec2 perp = q - d * x;
    vec2 pg;
    if (x <= A) {
        pg = (perp + d * (2.0 * A + 3.14159265 * r - x)) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            return vec4(mix(%5.rgb, vec3(0.92), 0.75), 1.0);
        }
        return %6;
    }
    if (x <= A + r) {
        float th = asin(clamp((x - A) / r, 0.0, 1.0));
        pg = (perp + d * (A + r * (3.14159265 - th))) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            return vec4(mix(%5.rgb, vec3(0.92), 0.75) * (0.75 + 0.25 * cos(th)), 1.0);
        }
        pg = (perp + d * (A + r * th)) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            vec4 col = %7;
            return vec4(col.rgb * (1.0 - 0.35 * sin(th)), col.a);
        }
    }
    vec4 under = %8;
    float sh = (1.0 - smoothstep(0.0, r * 1.5, x - A - r)) * %9 * step(A + r, x) * 0.6;
    return vec4(under.rgb * (1.0 - sh), under.a);)").arg(asp, angle, radius, p, from1, from3, from4, to, shadow);
        return {{"image", body}};
    };

    registerNodeDefs({rotate3d, sphere, cardFlip, cube, doors, pageCurl});
}

}
