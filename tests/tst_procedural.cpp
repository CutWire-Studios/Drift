#include <QtTest>

#include "engine/procedural/ProceduralDiagnostics.h"
#include "engine/procedural/ProceduralEvaluator.h"
#include "engine/procedural/ProceduralSceneCache.h"

#include <chrono>
#include <atomic>
#include <cmath>
#include <thread>

using namespace drift;

namespace {

ProceduralScene run(const char *source, ProceduralBudgets budgets = ProceduralBudgets::forPlatform(),
                    const ProceduralParams &params = {}, double t = 0.0, std::int64_t seed = 1,
                    const ProceduralCameraInput &camera = {})
{
    return ProceduralEvaluator::evaluate(source, params, t, camera, ProceduralCanvas{1280, 720}, budgets, 3, seed);
}

QString errorsOf(const ProceduralScene &s)
{
    QStringList l;
    for (const std::string &e : s.errors)
        l << QString::fromStdString(e);
    return l.join('\n');
}

bool sameMesh(const ProceduralMesh &a, const ProceduralMesh &b)
{
    return a.position == b.position && a.normal == b.normal && a.uv == b.uv && a.indices == b.indices
        && a.instances == b.instances;
}

ProceduralEvalRequest request(const char *source, double t = 0.0, float cameraX = 0.f)
{
    ProceduralEvalRequest r;
    r.source = source;
    r.sourceHash = "hash-of-" + std::to_string(std::hash<std::string>{}(r.source));
    r.time = t;
    r.canvas = {640, 360};
    r.camera.eye = {cameraX, 0.f, -500.f};
    return r;
}

constexpr const char *kBoxScript = "scene:add(geo.box(10 + time, 10, 10), 'flat')";
constexpr const char *kCameraScript = "local e = camera.eye\nscene:add(geo.box(10 + e[1], 10, 10), 'flat')";

} // namespace

class TstProcedural : public QObject
{
    Q_OBJECT
private slots:
    void sandboxHasNoEscapes()
    {
        const ProceduralScene s = run(R"(
            assert(os == nil and io == nil and require == nil and dofile == nil and loadfile == nil)
            assert(package == nil and debug == nil and collectgarbage == nil and coroutine == nil)
            assert(string.dump == nil and math.random == nil and math.randomseed == nil and warn == nil)
            assert(load("return os")() == nil)
            assert(load("\27Lua\84\0\25\147\13\10\26\10") == nil)
            assert(load(string.char(27) .. "Lua") == nil)
            assert(load("return 7", "c", "b")() == 7) -- the mode argument cannot enable binary chunks
            assert(not pcall(setmetatable, {}, {__gc = function() end}))
            assert(getmetatable(geo.box(1, 1, 1)) == false)
            assert(string.rep("ab", 3) == "ababab" and utf8.char(228) ~= nil and table.concat({1, 2}) == "12")
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
    }

    void printGoesToLog()
    {
        const ProceduralScene s = run("print('hello', 42)\nprint(nil)");
        QVERIFY(s.ok());
        QCOMPARE(s.log.size(), size_t(2));
        QCOMPARE(QString::fromStdString(s.log[0]), QString("hello\t42"));
    }

    void luaErrorsBecomeErrorScenes()
    {
        const ProceduralScene s = run("scene:add(geo.box(1,1,1), 'a')\nerror('boom')");
        QVERIFY(!s.ok());
        QVERIFY(s.meshes.empty());
        QVERIFY2(errorsOf(s).contains("main.lua:2: boom"), qPrintable(errorsOf(s)));
        QVERIFY(!run("this is not lua").ok());
        QVERIFY(!run("\x1b" "Lua").ok());
    }

    void memoryBombFailsCleanly()
    {
        ProceduralBudgets b = ProceduralBudgets::forPlatform();
        b.luaMemoryBytes = 8u * 1024u * 1024u;
        ProceduralScene s = run("local t = {} for i = 1, 1e9 do t[i] = i end", b);
        QVERIFY(!s.ok());
        QVERIFY2(errorsOf(s).contains("memory"), qPrintable(errorsOf(s)));
        s = run("local s = string.rep('x', 1e9)", b);
        QVERIFY(!s.ok());
        s = run("local s = 'x' for i = 1, 40 do s = s .. s end", b);
        QVERIFY(!s.ok());
        s = run("pcall(function() local t = {} for i = 1, 1e9 do t[i] = i end end) scene:add(geo.box(1,1,1), 'a')", b);
        QVERIFY2(!s.ok(), "pcall must not swallow the memory limit");
    }

    void infiniteLoopHitsDeadline()
    {
        ProceduralBudgets b = ProceduralBudgets::forPlatform();
        b.deadlineMs = 100;
        for (const char *src : {"while true do end",
                                "while true do pcall(function() while true do end end) end",
                                "local function f() return f() + 1 end f()"}) {
            const auto t0 = std::chrono::steady_clock::now();
            const ProceduralScene s = run(src, b);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            QVERIFY(!s.ok());
            QVERIFY2(ms < 3000, src);
        }
        const ProceduralScene s = run("while true do end", b);
        QVERIFY2(errorsOf(s).contains("time limit"), qPrintable(errorsOf(s)));
    }

    void gridBoxSphereCylinderTorusCounts()
    {
        const ProceduralScene s = run(R"(
            scene:add(geo.grid(100, 50, 4, 2), "a")
            scene:add(geo.box(2, 4, 6), "a")
            scene:add(geo.sphere(10, 16, 8), "a")
            scene:add(geo.cylinder(5, 10, 12), "a")
            scene:add(geo.torus(10, 2, 12, 6), "a")
            scene:add(geo.line({{0,0,0},{1,2,3},{4,5,6}}), "a")
            scene:add(geo.points(7), "a")
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QCOMPARE(s.meshes.size(), size_t(7));
        QCOMPARE(s.meshes[0].mesh->vertexCount(), size_t(15));
        QCOMPARE(s.meshes[0].mesh->indices.size(), size_t(48));
        QCOMPARE(s.meshes[1].mesh->vertexCount(), size_t(24));
        QCOMPARE(s.meshes[1].mesh->indices.size(), size_t(36));
        QCOMPARE(s.meshes[2].mesh->vertexCount(), size_t(17 * 9));
        QCOMPARE(s.meshes[2].mesh->indices.size(), size_t(16 * 8 * 6));
        QCOMPARE(s.meshes[3].mesh->vertexCount(), size_t(13 * 2 + 2 * 13));
        QCOMPARE(s.meshes[4].mesh->vertexCount(), size_t(13 * 7));
        QCOMPARE(s.meshes[5].mesh->vertexCount(), size_t(3));
        QCOMPARE(s.meshes[5].mesh->indices.size(), size_t(4));
        QVERIFY(s.meshes[5].mesh->topology == ProceduralMesh::Topology::Lines);
        QCOMPARE(s.meshes[6].mesh->vertexCount(), size_t(7));
        QCOMPARE(s.stats.meshCount, size_t(7));
        QCOMPARE(s.stats.vertexCount, size_t(15 + 24 + 153 + 52 + 91 + 3 + 7));
    }

    void closedShapesFaceOutward()
    {
        const ProceduralScene s = run(R"(
            scene:add(geo.box(2, 4, 6), "a")
            scene:add(geo.sphere(10, 16, 8), "a")
            scene:add(geo.cylinder(5, 10, 12), "a")
        )");
        QVERIFY(s.ok());
        for (const ProceduralMeshEntry &e : s.meshes) {
            const ProceduralMesh &m = *e.mesh;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                const float *p0 = &m.position[m.indices[t] * 3], *p1 = &m.position[m.indices[t + 1] * 3],
                            *p2 = &m.position[m.indices[t + 2] * 3];
                const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
                const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                const float c[3] = {(p0[0] + p1[0] + p2[0]) / 3, (p0[1] + p1[1] + p2[1]) / 3, (p0[2] + p1[2] + p2[2]) / 3};
                const float area2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
                if (area2 > 1e-12f)
                    QVERIFY(n[0] * c[0] + n[1] * c[1] + n[2] * c[2] > -1e-4f);
            }
        }
    }

    void boundsTransformMerge()
    {
        const ProceduralScene s = run(R"(
            local b = geo.box(2, 2, 2):transform(mat.translate(10, 0, 0))
            local mn, mx = b:bounds()
            assert(mn.x == 9 and mx.x == 11 and mn[2] == -1 and mx.z == 1)
            local s = geo.sphere(3, 8, 4):transform(mat.scale(2))
            local _, smx = s:bounds()
            assert(math.abs(smx.x - 6) < 1e-4 or math.abs(smx.z - 6) < 1e-4)
            local merged = geo.merge(b, geo.box(1, 1, 1))
            local nv, nt = merged:count()
            assert(nv == 48 and nt == 24)
            scene:add(merged, "a")
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QVERIFY(s.aabbValid);
        QCOMPARE(s.aabbMin[0], -0.5f);
        QCOMPARE(s.aabbMax[0], 11.f);
        QCOMPARE(s.stats.vertexCount, size_t(48));
    }

    void instancesAndAttributes()
    {
        const ProceduralScene s = run(R"(
            local m = geo.box(1, 1, 1)
            m:instances(buf.from({mat.translate(0, 0, 0), mat.translate(10, 0, 0), mat.translate(0, 20, 0)}))
            assert(m:instances() == 3)
            local w = buf.new("float", 24):fill(0.5)
            m:set("weight", w)
            assert(m:get("weight")[1] == 0.5 and m:get("weight"):kind() == "float")
            assert(m:get("missing") == nil and m:get("position"):kind() == "vec3")
            local pos = buf.from({{0,0,0},{1,0,0},{0,1,0}})
            assert(not pcall(m.set, m, "position", pos)) -- wrong vertex count
            local v4 = buf.from({{1,2,3,2}})
            local sm = geo.box(1,1,1):instances(v4)
            scene:add(m, "a", {tint = {1, 0, 0}, k = 2, on = true, m = mat.identity()})
            scene:add(sm, "b")
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QCOMPARE(s.meshes[0].instanceCount(), size_t(3));
        QCOMPARE(s.meshes[0].uniforms.at("tint").size(), size_t(3));
        QCOMPARE(s.meshes[0].uniforms.at("k")[0], 2.f);
        QCOMPARE(s.meshes[0].uniforms.at("m").size(), size_t(16));
        QCOMPARE(s.aabbMax[1], 20.5f);
        QCOMPARE(s.stats.instanceCount, size_t(3 + 1));
        QCOMPARE(s.meshes[0].mesh->custom.size(), size_t(1));
        QCOMPARE(s.meshes[1].mesh->instances[0], 2.f);
        QCOMPARE(s.meshes[1].mesh->instances[12], 1.f);
    }

    void bufferOps()
    {
        const ProceduralScene s = run(R"(
            local b = buf.from({1, 2, 3}):add(1):mul(2)
            assert(b[1] == 4 and b[2] == 6 and b[3] == 8 and #b == 3)
            local l = buf.from({0, 10}):lerp(buf.from({10, 20}), 0.5)
            assert(l[1] == 5 and l[2] == 15)
            local v = buf.from({{1,2,3},{4,5,6}}):mul(buf.from({2, 10}))
            assert(v[1][3] == 6 and v[2][1] == 40)
            local m = buf.from({1, 2, 3, 4}):map(function(x, i) return x * i end)
            assert(m[4] == 16)
            local sl = buf.from({1, 2, 3, 4}):slice(2, 3)
            assert(#sl == 2 and sl[1] == 2 and sl[2] == 3)
            local f = buf.from({{0,0,0},{1,2,3}}):noise(0.7, 3)
            assert(#f == 2 and f:kind() == "float")
            assert(#noise.field(buf.new("vec2", 5), 2) == 5)
            local p = buf.new("vec3", 2); p[2] = {1, 2, 3}
            assert(p[2][3] == 3)
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
    }

    void mapIsCountedAgainstBudget()
    {
        ProceduralBudgets b = ProceduralBudgets::forPlatform();
        b.maxWork = 500;
        const ProceduralScene s = run("local x = buf.new('float', 400):map(function(v) return v end)\n"
                                      "x:map(function(v) return v end)", b);
        QVERIFY(!s.ok());
        QVERIFY2(errorsOf(s).contains("work budget"), qPrintable(errorsOf(s)));
    }

    void vertexBudgetOverflowGivesErrorScene()
    {
        ProceduralBudgets b = ProceduralBudgets::forPlatform();
        b.maxVertices = 1000;
        ProceduralScene s = run("scene:add(geo.box(1,1,1), 'a')\nlocal m = geo.sphere(1, 64, 64)", b);
        QVERIFY(!s.ok());
        QVERIFY(s.meshes.empty());
        QVERIFY2(errorsOf(s).contains("vertex budget"), qPrintable(errorsOf(s)));
        s = run("pcall(geo.sphere, 1, 64, 64) scene:add(geo.box(1,1,1), 'a')", b);
        QVERIFY2(!s.ok(), "pcall must not swallow a budget error");
        s = run("for i = 1, 100 do scene:add(geo.sphere(1, 10, 10), 'a') end", b);
        QVERIFY(!s.ok());
        QVERIFY(errorsOf(s).contains("vertex budget"));
        s = run("scene:add(geo.merge(geo.box(1,1,1), geo.sphere(1, 20, 20)), 'a')", b);
        QVERIFY(s.ok());
    }

    void instanceBudgetOverflowGivesErrorScene()
    {
        ProceduralBudgets b = ProceduralBudgets::forPlatform();
        b.maxInstances = 10;
        const ProceduralScene s = run("geo.box(1,1,1):instances(buf.new('mat4', 100))", b);
        QVERIFY(!s.ok());
        QVERIFY2(errorsOf(s).contains("instance budget"), qPrintable(errorsOf(s)));
    }

    void platformBudgetsMatchPlan()
    {
        const ProceduralBudgets d = ProceduralBudgets::forPlatform();
        QVERIFY(d.maxVertices == 1000000 || d.maxVertices == 300000 || d.maxVertices == 150000);
        QCOMPARE(d.deadlineMs, 250);
        QCOMPARE(ProceduralBudgets::forPlatform(true).deadlineMs, 5000);
    }

    void determinism()
    {
        const char *src = R"(
            local r = rand.new(seed)
            local pts = buf.new("vec3", 200)
            for i = 1, 200 do pts[i] = {r:range(-50, 50), r:float() * 10, r:normal()} end
            local ball = geo.sphere(20, 24, 12):displace({freq = 0.05, octaves = 3}, 5 + time):recomputeNormals()
            ball:set("tint", pts:slice(1, 1):noise(1) and buf.new("float", ball:count()):fill(r:int(1, 6)))
            ball:instances(pts:add(buf.new("vec3", 200)):noise(0.1) and buf.from({mat.translate(r:unit()), mat.translate(1,2,3)}))
            scene:add(ball, "a", {n = noise.simplex(1.5, 2.5, 3.5), f = noise.fbm(1, 2, 3)})
        )";
        const ProceduralScene a = run(src, ProceduralBudgets::forPlatform(), {}, 0.25, 7);
        const ProceduralScene b = run(src, ProceduralBudgets::forPlatform(), {}, 0.25, 7);
        const ProceduralScene c = run(src, ProceduralBudgets::forPlatform(), {}, 0.25, 8);
        QVERIFY2(a.ok(), qPrintable(errorsOf(a)));
        QVERIFY(b.ok() && c.ok());
        QVERIFY(sameMesh(*a.meshes[0].mesh, *b.meshes[0].mesh));
        QVERIFY(a.meshes[0].mesh->custom[0].data == b.meshes[0].mesh->custom[0].data);
        QVERIFY(a.meshes[0].uniforms == b.meshes[0].uniforms);
        QVERIFY(!sameMesh(*a.meshes[0].mesh, *c.meshes[0].mesh));
        const ProceduralScene later = run(src, ProceduralBudgets::forPlatform(), {}, 0.5, 7);
        QVERIFY(!sameMesh(*a.meshes[0].mesh, *later.meshes[0].mesh));
    }

    void pairsOrderIsStable()
    {
#ifdef DRIFT_SYSTEM_LUA
        QSKIP("the system Lua seeds string hashing from the clock, so pairs() order varies there");
#endif
        const char *src = "local t = {} for _, k in ipairs{'a','b','c','d','e','f','g','h'} do t[k] = 1 end "
                          "local order = {} for k in pairs(t) do order[#order + 1] = k end print(table.concat(order))";
        const ProceduralScene a = run(src), b = run(src);
        QVERIFY(a.ok() && !a.log.empty());
        QCOMPARE(a.log, b.log);
    }

    void globalsAndLights()
    {
        ProceduralParams params;
        params["size"] = ProceduralParam::fromNumber(40);
        params["tint"] = ProceduralParam::fromVector({0.1, 0.2, 0.3});
        params["label"] = ProceduralParam::fromString("hi");
        params["flag"] = ProceduralParam::fromBool(true);
        const ProceduralScene s = run(R"(
            assert(frame == 3 and seed == 9 and time == 1.5 and canvas.w == 1280 and canvas.h == 720)
            assert(params.size == 40 and params.tint.y == 0.2 and params.tint[3] == 0.3 and params.label == "hi" and params.flag)
            scene:light{type = "point", pos = {1, 2, 3}, color = {1, 0.5, 0}, intensity = 2, range = 100}
            scene:light{type = "spot", pos = {0, 0, 0}, dir = {0, 0, 2}, cone = 0.4}
            scene:light{type = "directional", dir = {0, 1, 0}}
            scene:add(geo.grid(params.size, params.size), "a")
        )", ProceduralBudgets::forPlatform(), params, 1.5, 9);
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QCOMPARE(s.lights.size(), size_t(3));
        QVERIFY(s.lights[0].type == ProceduralLight::Type::Point);
        QCOMPARE(s.lights[0].intensity, 2.f);
        QCOMPARE(s.lights[1].dir[2], 1.f);
        QCOMPARE(s.lights[1].cone, 0.4f);
        QVERIFY(s.lights[2].type == ProceduralLight::Type::Directional);
        QCOMPARE(s.aabbMax[0], 20.f);
        QVERIFY(!run("scene:light{type = 'laser'}").ok());
    }

    void cameraReadFlag()
    {
        QVERIFY(!run("scene:add(geo.box(1,1,1), 'a')").cameraRead);
        QVERIFY(run("local f = camera.fov").cameraRead);
        QVERIFY(run("local e = camera.eye.x").cameraRead);
        QVERIFY(run("for k in pairs(camera) do end").cameraRead);
        QVERIFY(run("local v = camera.view:apply(0, 0, 0)").cameraRead);
        QVERIFY(run("error('x') -- after reading camera.fov\nlocal f = camera.fov").cameraRead == false);
        QVERIFY(!run("camera.fov = 3").ok());

        ProceduralCameraInput cam;
        cam.eye = {1, 2, 3};
        cam.fov = 0.75f;
        const ProceduralScene s = run("assert(camera.eye.z == 3 and camera.fov == 0.75 and camera.near == 1 and camera.far == 100000)"
                                      "assert(camera.right.x == 1 and camera.up.y == -1 and camera.forward.z == 1)",
                                      ProceduralBudgets::forPlatform(), {}, 0, 1, cam);
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QVERIFY(s.cameraRead);
    }

    void ownCameraIsRecorded()
    {
        const ProceduralScene s = run(R"(
            scene:camera{eye = {0, 0, -500}, target = {0, 0, 0}, fov = 0.9, near = 2, far = 5000}
            local view = mat.lookAt({0, 0, -500}, {0, 0, 0})
            local x, y, z = view:apply(0, 0, 0)
            assert(math.abs(z - 500) < 1e-3 and math.abs(x) < 1e-4)
            local proj = mat.perspective(0.9, 16 / 9, 2, 5000)
            local _, _, nz = proj:apply(0, 0, 2)
            assert(math.abs(nz + 1) < 1e-3)
            assert(math.abs(select(3, (proj * view):apply(0, 0, 0)) ) < 1)
            local inv = view:inverse()
            local ex, ey, ez = inv:apply(0, 0, 0)
            assert(math.abs(ez + 500) < 1e-2)
            scene:add(geo.box(1, 1, 1), "a")
        )");
        QVERIFY2(s.ok(), qPrintable(errorsOf(s)));
        QVERIFY(s.hasCamera);
        QCOMPARE(s.camera.eye[2], -500.f);
        QCOMPARE(s.camera.nearPlane, 2.f);
        QVERIFY(!s.cameraRead);
        QVERIFY(!run("scene:camera{eye = {0,0,0}, target = {0,0,1}, fov = 4}").ok());
    }

    void sceneCacheHitsAndMisses()
    {
        ProceduralSceneCache cache;
        const auto a = cache.evaluate(request(kBoxScript, 1.0));
        QVERIFY(a->ok());
        QCOMPARE(a->meshHashes.size(), a->meshes.size());
        QVERIFY(a->meshHashes[0] != 0);
        const auto again = cache.evaluate(request(kBoxScript, 1.0));
        QCOMPARE(again.get(), a.get());
        QCOMPARE(cache.stats().evaluations, size_t(1));
        QCOMPARE(cache.stats().hits, size_t(1));
        const auto later = cache.evaluate(request(kBoxScript, 2.0));
        QVERIFY(later.get() != a.get());
        QCOMPARE(cache.stats().evaluations, size_t(2));

        ProceduralEvalRequest seeded = request(kBoxScript, 1.0);
        seeded.seed = 9;
        cache.evaluate(seeded);
        ProceduralEvalRequest withParam = request(kBoxScript, 1.0);
        withParam.params["x"] = ProceduralParam::fromNumber(1.0);
        cache.evaluate(withParam);
        QCOMPARE(cache.stats().evaluations, size_t(4));
    }

    void sceneCacheKeysOnCameraOnlyWhenRead()
    {
        ProceduralSceneCache cache;
        const auto first = cache.evaluate(request(kBoxScript, 0.0, 0.f));
        const auto second = cache.evaluate(request(kBoxScript, 0.0, 50.f));
        QCOMPARE(second.get(), first.get()); // never touched `camera`
        QCOMPARE(cache.stats().evaluations, size_t(1));
        QVERIFY(!first->cameraRead);

        const auto c0 = cache.evaluate(request(kCameraScript, 0.0, 0.f));
        QVERIFY(c0->cameraRead);
        const auto c1 = cache.evaluate(request(kCameraScript, 0.0, 50.f));
        QVERIFY(c1.get() != c0.get());
        QCOMPARE(cache.stats().evaluations, size_t(3));
        QCOMPARE(cache.evaluate(request(kCameraScript, 0.0, 50.f)).get(), c1.get());
        QCOMPARE(cache.evaluate(request(kCameraScript, 0.0, 0.f)).get(), c0.get());
        QCOMPARE(cache.stats().evaluations, size_t(3));
        const auto width = [](const ProceduralScene &scene) {
            std::array<float, 3> mn, mx;
            scene.meshes[0].mesh->bounds(mn, mx);
            return mx[0] - mn[0];
        };
        QCOMPARE(width(*c0), 10.f);
        QCOMPARE(width(*c1), 60.f);
    }

    void sceneCacheEvictsOverTheByteCap()
    {
        ProceduralSceneCache cache(64 * 1024);
        const char *big = "scene:add(geo.grid(100, 100, 20, 20), 'flat')"; // ~ 24 kB a scene, so the cap holds two
        for (int i = 0; i < 12; ++i)
            QVERIFY(cache.evaluate(request(big, double(i)))->ok());
        const auto stats = cache.stats();
        QVERIFY(stats.bytes <= 64 * 1024);
        QVERIFY(stats.entries < 12);
        // The oldest is gone, the newest is still there.
        const size_t before = cache.stats().evaluations;
        cache.evaluate(request(big, 11.0));
        QCOMPARE(cache.stats().evaluations, before);
        cache.evaluate(request(big, 0.0));
        QCOMPARE(cache.stats().evaluations, before + 1);
        cache.setMaxBytes(0);
        QCOMPARE(cache.stats().entries, size_t(0));
    }

    void sceneCacheSharesAnInFlightEvaluation()
    {
        ProceduralSceneCache cache;
        const char *slow = "local x = 0\nfor i = 1, 4000000 do x = x + i end\nscene:add(geo.box(10, 10, 10), 'flat')";
        std::atomic<bool> go{false};
        ProceduralSceneCache::ScenePtr results[2];
        std::thread threads[2];
        for (int i = 0; i < 2; ++i) {
            threads[i] = std::thread([&, i] {
                while (!go.load()) {}
                results[i] = cache.evaluate(request(slow, 1.0));
            });
        }
        go = true;
        for (std::thread &t : threads)
            t.join();
        QVERIFY(results[0] && results[0]->ok());
        QCOMPARE(results[1].get(), results[0].get());
        QCOMPARE(cache.stats().evaluations, size_t(1));
        QCOMPARE(cache.stats().hits + cache.stats().joined, size_t(1));
    }

    void errorScenesAreCachedButTimeoutsAreNot()
    {
        ProceduralSceneCache cache;
        const auto bad = cache.evaluate(request("error('nope')"));
        QVERIFY(!bad->ok());
        cache.evaluate(request("error('nope')"));
        QCOMPARE(cache.stats().evaluations, size_t(1));

        ProceduralEvalRequest loop = request("while true do end");
        loop.budgets.deadlineMs = 20;
        QVERIFY(!cache.evaluate(loop)->ok());
        cache.evaluate(loop);
        QCOMPARE(cache.stats().evaluations, size_t(3));
    }

    void meshHashFollowsContent()
    {
        const ProceduralScene a = run("scene:add(geo.box(10, 10, 10), 'a')");
        const ProceduralScene b = run("scene:add(geo.box(10, 10, 10), 'b')");
        const ProceduralScene c = run("scene:add(geo.box(10, 10, 11), 'a')");
        QCOMPARE(proceduralMeshHash(*a.meshes[0].mesh), proceduralMeshHash(*b.meshes[0].mesh));
        QVERIFY(proceduralMeshHash(*a.meshes[0].mesh) != proceduralMeshHash(*c.meshes[0].mesh));
        const ProceduralScene inst = run("scene:add(geo.box(10, 10, 10):instances(buf.from({mat.translate(1,2,3)})), 'a')");
        QVERIFY(inst.ok());
        QVERIFY(proceduralMeshHash(*a.meshes[0].mesh) != proceduralMeshHash(*inst.meshes[0].mesh));
    }

    void diagnosticsRegistry()
    {
        ProceduralDiagnostics &d = ProceduralDiagnostics::shared();
        ProceduralClipReport report;
        report.packageId = "demo";
        report.error = "package 'demo' is not installed";
        d.setClip("clip-1", report);
        ProceduralClipReport out;
        QVERIFY(d.clip("clip-1", &out));
        QCOMPARE(out.error, report.error);
        QVERIFY(out.serial > 0);
        d.clearClip("clip-1");
        QVERIFY(!d.clip("clip-1", nullptr));

        d.setShader("h1", "flat", {false, "0:3: error"});
        d.setShader("h1", "glow", {true, ""});
        const auto shaders = d.shaders("h1");
        QCOMPARE(shaders.size(), size_t(2));
        QVERIFY(!shaders.at("flat").ok);
        QVERIFY(d.shaders("other").empty());
    }

    void noiseRanges()
    {
        for (int i = 0; i < 200; ++i) {
            const float x = float(i) * 0.37f, y = float(i) * 0.11f, z = float(i) * 0.23f;
            QVERIFY(std::abs(proceduralPerlin(x, y, z)) <= 1.01f);
            QVERIFY(std::abs(proceduralSimplex(x, y, z)) <= 1.05f);
            QVERIFY(std::abs(proceduralFbm(x, y, z, 4, 2.f, 0.5f)) <= 1.01f);
        }
        QCOMPARE(proceduralPerlin(1.f, 2.f, 3.f), 0.f); // lattice points are zero
    }
};

QTEST_APPLESS_MAIN(TstProcedural)
#include "tst_procedural.moc"
