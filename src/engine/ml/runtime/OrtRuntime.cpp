#include "engine/ml/runtime/OrtRuntime.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/ml/runtime/OrtSupport.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QSettings>
#include <QVersionNumber>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#    include <windows.h>

#    include <thread>
#else
#    include <dlfcn.h>
#endif

namespace drift::ort {
namespace {

// The oldest OrtApi Drift will drive. Below this the table is missing too much of what the engine
// uses to be worth a compatibility story; above it, ONNX Runtime only ever appends to the table,
// so a runtime slightly older than the headers is safe as long as nothing past its own version is
// called — hence g_apiVersion being checked before the plugin EP calls.
constexpr int kMinApiVersion = 22;

// Plugin EPs arrived with OrtApi 23. Calling RegisterExecutionProviderLibrary on an older table
// reads past the end of the struct.
constexpr int kPluginEpApiVersion = 23;

QMutex g_mutex;
enum class State { Unloaded, Loaded, Failed };
State g_state = State::Unloaded;
QString g_error;
QString g_variant;
QString g_version;
QString g_libPath;
int g_apiVersion = 0;
QList<PluginEp> g_registeredEps;
QSet<QString> g_autoFailed;
ProviderFailureHandler g_failureHandler;
// Held across the whole question, so loads failing together share one answer instead of
// stacking dialogs.
QMutex g_askMutex;

QStringList libraryNames()
{
#if defined(_WIN32)
    return {QStringLiteral("onnxruntime.dll")};
#elif defined(__APPLE__)
    return {QStringLiteral("libonnxruntime.dylib")};
#else
    return {QStringLiteral("libonnxruntime.so"), QStringLiteral("libonnxruntime.so.1")};
#endif
}

QString cudaProviderGlob()
{
#if defined(_WIN32)
    return QStringLiteral("*onnxruntime_providers_cuda*.dll");
#else
    return QStringLiteral("*onnxruntime_providers_cuda*");
#endif
}

QString migraphxProviderGlob()
{
    return QStringLiteral("*onnxruntime_providers_migraphx*");
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

struct Candidate
{
    QString dir;
    QString libPath;
    QString variant;
    QString version;
};

QString findLibrary(const QString &dir)
{
    for (const QString &name : libraryNames()) {
        const QString path = QDir(dir).filePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

// A runtime root is a directory with the library either in lib/ (how every upstream release is
// laid out, and how the addon packages are built) or loose at the top.
std::optional<Candidate> inspect(const QString &root)
{
    QString libDir = QDir(root).filePath(QStringLiteral("lib"));
    QString lib = findLibrary(libDir);
    if (lib.isEmpty()) {
        libDir = root;
        lib = findLibrary(libDir);
    }
    if (lib.isEmpty())
        return std::nullopt;

    Candidate candidate;
    candidate.dir = root;
    candidate.libPath = lib;

    // runtime.json is what an addon uses to declare itself, and is deliberately optional: an
    // upstream release extracted by hand and pointed at with DRIFT_ONNXRUNTIME_DIR has no such
    // file and still has to work, so the variant is derived from what is in the directory.
    const QJsonObject meta = readJson(QDir(root).filePath(QStringLiteral("runtime.json")));
    candidate.variant = meta.value(QStringLiteral("variant")).toString().trimmed().toLower();
    candidate.version = meta.value(QStringLiteral("ortVersion")).toString();
    if (candidate.variant.isEmpty()) {
        const QDir dir(libDir);
        if (!dir.entryList({cudaProviderGlob()}, QDir::Files).isEmpty())
            candidate.variant = QStringLiteral("cuda");
        else if (!dir.entryList({migraphxProviderGlob()}, QDir::Files).isEmpty())
            candidate.variant = QStringLiteral("migraphx");
        else
            candidate.variant = QStringLiteral("cpu");
    }
    return candidate;
}

QList<Candidate> candidates()
{
    QList<Candidate> found;
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_ONNXRUNTIME_DIR"), QStringLiteral("onnxruntime"),
        QString::fromLatin1(kRuntimeKind));
    for (const QString &root : roots) {
        if (const std::optional<Candidate> candidate = inspect(root))
            found.append(*candidate);
    }
    return found;
}

QSet<QString> epDeviceNames(Ort::Env &env)
{
    QSet<QString> names;
    for (const Ort::ConstEpDevice &device : env.GetEpDevices())
        names.insert(QString::fromUtf8(device.EpName()));
    return names;
}

void registerEpLibrary(Ort::Env &env, PluginEp ep)
{
    const QSet<QString> before = epDeviceNames(env);
    try {
        env.RegisterExecutionProviderLibrary(ep.variant.toUtf8().constData(), ortPath(ep.library));
    } catch (const Ort::Exception &e) {
        qWarning("[ort] the %s provider could not be registered (%s).",
                 qUtf8Printable(ep.variant), e.what());
        return;
    }

    // The library names its own EPs. Diffing the device list is more reliable than making the
    // package guess the exact string ONNX Runtime will report, which is what has to be matched
    // later when a session asks for this provider.
    QSet<QString> added = epDeviceNames(env);
    added.subtract(before);
    if (added.isEmpty()) {
        qWarning("[ort] the %s provider registered but offers no device here; ignoring it.",
                 qUtf8Printable(ep.variant));
        return;
    }

    ep.epName = *added.constBegin();
    g_registeredEps.append(ep);
    qInfo("[ort] registered execution provider %s (%s).", qUtf8Printable(ep.epName),
          qUtf8Printable(ep.variant));
}

void registerPluginEps(Ort::Env &env)
{
    if (g_apiVersion < kPluginEpApiVersion)
        return;

    for (const PluginEp &ep : installedPluginEps()) {
        if (g_apiVersion < ep.minApiVersion) {
            qWarning("[ort] the %s provider needs OrtApi %d but the loaded runtime provides %d; "
                     "skipping it.",
                     qUtf8Printable(ep.variant), ep.minApiVersion, g_apiVersion);
            continue;
        }
        registerEpLibrary(env, ep);
    }
}

#ifdef _WIN32
// The flat C catalog API of Microsoft.Windows.AI.MachineLearning.dll, as declared in
// WinMLEpCatalog.h from the Microsoft.Windows.AI.MachineLearning NuGet. Declared here rather than
// included because that DLL ships in the "winml" core addon and is resolved at run time, like
// onnxruntime.dll itself.
enum WinMLEpReadyState {
    WinMLEpReadyState_Ready = 0,
    WinMLEpReadyState_NotReady = 1,
    WinMLEpReadyState_NotPresent = 2,
};
enum WinMLEpCertification {
    WinMLEpCertification_Unknown = 0,
    WinMLEpCertification_Certified = 1,
    WinMLEpCertification_Uncertified = 2,
};
struct WinMLEpInfo
{
    const char *name;
    const char *version;
    const char *packageFamilyName;
    const char *libraryPath;
    const char *packageRootPath;
    WinMLEpReadyState readyState;
    WinMLEpCertification certification;
};
struct WinMLEpCatalog;
struct WinMLEp;
using WinMLEpEnumCallback = BOOL(CALLBACK *)(WinMLEp *, const WinMLEpInfo *, void *);

struct WinMLCatalogApi
{
    HRESULT(STDAPICALLTYPE *create)(WinMLEpCatalog **) = nullptr;
    void(STDAPICALLTYPE *release)(WinMLEpCatalog *) = nullptr;
    HRESULT(STDAPICALLTYPE *enumerate)(WinMLEpCatalog *, WinMLEpEnumCallback, void *) = nullptr;
    HRESULT(STDAPICALLTYPE *find)(WinMLEpCatalog *, const char *, const char *, WinMLEp **) = nullptr;
    HRESULT(STDAPICALLTYPE *ensureReady)(WinMLEp *) = nullptr;
    HRESULT(STDAPICALLTYPE *libraryPathSize)(WinMLEp *, size_t *) = nullptr;
    HRESULT(STDAPICALLTYPE *libraryPath)(WinMLEp *, size_t, char *, size_t *) = nullptr;
};

template <typename Fn>
bool resolve(HMODULE module, const char *name, Fn &fn)
{
    fn = reinterpret_cast<Fn>(GetProcAddress(module, name));
    return fn != nullptr;
}

std::optional<WinMLCatalogApi> loadCatalogApi(const QString &libDir)
{
    const QString path = QDir(libDir).filePath(QStringLiteral("Microsoft.Windows.AI.MachineLearning.dll"));
    HMODULE module = LoadLibraryW(QDir::toNativeSeparators(path).toStdWString().c_str());
    if (!module) {
        qWarning("[ort] cannot load %s (error %lu); vendor providers from Windows ML are "
                 "unavailable.",
                 qUtf8Printable(path), GetLastError());
        return std::nullopt;
    }
    WinMLCatalogApi api;
    if (!resolve(module, "WinMLEpCatalogCreate", api.create)
        || !resolve(module, "WinMLEpCatalogRelease", api.release)
        || !resolve(module, "WinMLEpCatalogEnumProviders", api.enumerate)
        || !resolve(module, "WinMLEpCatalogFindProvider", api.find)
        || !resolve(module, "WinMLEpEnsureReady", api.ensureReady)
        || !resolve(module, "WinMLEpGetLibraryPathSize", api.libraryPathSize)
        || !resolve(module, "WinMLEpGetLibraryPath", api.libraryPath)) {
        qWarning("[ort] %s lacks the Windows ML catalog API.", qUtf8Printable(path));
        return std::nullopt;
    }
    return api;
}

QString catalogLibraryPath(const WinMLCatalogApi &api, WinMLEp *ep)
{
    size_t size = 0;
    if (FAILED(api.libraryPathSize(ep, &size)) || size == 0)
        return {};
    std::string path(size, '\0');
    size_t used = 0;
    if (FAILED(api.libraryPath(ep, size, path.data(), &used)) || used == 0)
        return {};
    path.resize(used - 1); // the count includes the terminator
    return QString::fromUtf8(path);
}

// "migraphx" is the token the AMD choice already has on Linux, so the picker label and a saved
// preference carry across.
QString variantForCatalogEp(const QString &name)
{
    QString token = name.toLower();
    if (token.contains(QLatin1String("migraphx")))
        return QStringLiteral("migraphx");
    if (token.contains(QLatin1String("tensorrt")))
        return QStringLiteral("tensorrt");
    token.remove(QLatin1String("executionprovider"));
    return token;
}

struct CatalogScan
{
    const WinMLCatalogApi *api;
    Ort::Env *env;
    QList<std::pair<std::string, std::string>> pending; // name, package family
};

BOOL CALLBACK registerCatalogEp(WinMLEp *ep, const WinMLEpInfo *info, void *context)
{
    auto *scan = static_cast<CatalogScan *>(context);
    if (!info || !info->name || info->certification != WinMLEpCertification_Certified)
        return TRUE;
    if (info->readyState != WinMLEpReadyState_Ready) {
        scan->pending.emplaceBack(info->name, info->packageFamilyName ? info->packageFamilyName : "");
        return TRUE;
    }
    // Microsoft's catalog sample calls EnsureReady on ready providers too before asking for the
    // library path, so this does the same rather than trusting info->libraryPath.
    if (const HRESULT hr = scan->api->ensureReady(ep); FAILED(hr)) {
        qWarning("[ort] Windows ML could not prepare %s (0x%08lx).", info->name,
                 static_cast<unsigned long>(hr));
        return TRUE;
    }
    const QString library = catalogLibraryPath(*scan->api, ep);
    if (library.isEmpty())
        return TRUE;
    PluginEp plugin;
    plugin.variant = variantForCatalogEp(QString::fromUtf8(info->name));
    plugin.library = library;
    plugin.minApiVersion = kPluginEpApiVersion;
    registerEpLibrary(*scan->env, plugin);
    return TRUE;
}

// Registers the vendor providers Windows ML has for this machine (AMD MIGraphX, NVIDIA TensorRT
// for RTX, Intel OpenVINO, …). Windows installs them on demand, which can mean a download of
// hundreds of MB, so providers that are not on disk yet are fetched on a background thread and
// picked up on the next launch; meanwhile DirectML from the same core carries the work.
void registerCatalogEps(Ort::Env &env)
{
    static const std::optional<WinMLCatalogApi> api =
        loadCatalogApi(QFileInfo(g_libPath).absolutePath());
    if (!api)
        return;

    WinMLEpCatalog *catalog = nullptr;
    if (const HRESULT hr = api->create(&catalog); FAILED(hr)) {
        // Below Windows 11 24H2 there is no catalog; DirectML still works.
        qInfo("[ort] no Windows ML provider catalog here (0x%08lx).",
              static_cast<unsigned long>(hr));
        return;
    }
    CatalogScan scan{&*api, &env, {}};
    api->enumerate(catalog, registerCatalogEp, &scan);
    api->release(catalog);

    if (scan.pending.isEmpty())
        return;
    std::thread([pending = scan.pending] {
        WinMLEpCatalog *catalog = nullptr;
        if (FAILED(api->create(&catalog)))
            return;
        for (const auto &[name, family] : pending) {
            WinMLEp *ep = nullptr;
            const char *familyArg = family.empty() ? nullptr : family.c_str();
            if (FAILED(api->find(catalog, name.c_str(), familyArg, &ep)) || !ep)
                continue;
            qInfo("[ort] Windows is installing the %s provider; it is used from the next launch.",
                  name.c_str());
            if (const HRESULT hr = api->ensureReady(ep); FAILED(hr))
                qWarning("[ort] Windows ML could not install %s (0x%08lx).", name.c_str(),
                         static_cast<unsigned long>(hr));
        }
        api->release(catalog);
    }).detach();
}
#endif

} // namespace

bool ensureLoaded(QString *error)
{
    QMutexLocker lock(&g_mutex);
    if (g_state != State::Unloaded) {
        if (error)
            *error = g_error;
        return g_state == State::Loaded;
    }

    const auto fail = [&](const QString &message) {
        g_state = State::Failed;
        g_error = message;
        if (error)
            *error = message;
        return false;
    };

    const QList<Candidate> found = candidates();
    if (found.isEmpty()) {
        return fail(QStringLiteral(
            "No ONNX Runtime installed. Install one from the Addon Manager, or point "
            "DRIFT_ONNXRUNTIME_DIR at an extracted onnxruntime release."));
    }

    const Candidate *chosen = nullptr;
    const QString wanted = preferredVariant();
    if (variantExplicit()) {
        const QString core = coreVariantFor(wanted);
        for (const Candidate &candidate : found) {
            if (candidate.variant == core) {
                chosen = &candidate;
                break;
            }
        }
    }
    // A preference naming a plugin EP ("webgpu") matches no core, and a preference naming a core
    // that is not installed is not worth refusing to start over — the search order already ranks
    // an installed addon above the development copy.
    if (!chosen) {
        chosen = &found.first();
        // The Windows ML core is the CPU core plus DirectML and the vendor catalog, so when both
        // are installed it is never the worse pick.
        if (chosen->variant == QLatin1String("cpu")) {
            for (const Candidate &candidate : found) {
                if (candidate.variant == QLatin1String("winml")) {
                    chosen = &candidate;
                    break;
                }
            }
        }
    }

#ifdef _WIN32
    // onnxruntime.dll lives in the addon lib/ folder, not next to Drift.exe. LoadLibrary of a
    // full path finds that DLL's own static imports beside it, but the CUDA EP is loaded later
    // by basename (onnxruntime_providers_cuda.dll) and then hard-imports cublas64_13.dll etc.
    // Those lookups start at the application directory unless this directory is on the process
    // search path. AddDllDirectory only affects LoadLibrary after SetDefaultDllDirectories
    // includes LOAD_LIBRARY_SEARCH_USER_DIRS — which LOAD_LIBRARY_SEARCH_DEFAULT_DIRS does,
    // while still searching the app dir and System32 (so Qt/FFmpeg next to the exe keep working).
    {
        const std::wstring libDir =
            QDir::toNativeSeparators(QFileInfo(chosen->libPath).absolutePath()).toStdWString();
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
            return fail(QStringLiteral("Cannot configure DLL directories (error %1)")
                            .arg(GetLastError()));
        }
        if (!AddDllDirectory(libDir.c_str())) {
            return fail(QStringLiteral("Cannot add %1 to the DLL search path (error %2)")
                            .arg(QFileInfo(chosen->libPath).absolutePath())
                            .arg(GetLastError()));
        }
    }
    HMODULE handle = LoadLibraryW(chosen->libPath.toStdWString().c_str());
    if (!handle)
        return fail(QStringLiteral("Cannot load %1 (error %2)").arg(chosen->libPath).arg(GetLastError()));
    auto getApiBase = reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(
        GetProcAddress(handle, "OrtGetApiBase"));
#else
#ifdef Q_OS_WASM
    return fail(QStringLiteral("ONNX Runtime is not available in the browser build"));
#endif
    void *handle = dlopen(chosen->libPath.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return fail(QStringLiteral("Cannot load %1 (%2)")
                        .arg(chosen->libPath, QString::fromLocal8Bit(dlerror())));
    auto getApiBase =
        reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(dlsym(handle, "OrtGetApiBase"));
#endif
    if (!getApiBase)
        return fail(QStringLiteral("%1 is not an ONNX Runtime library").arg(chosen->libPath));

    const OrtApiBase *base = getApiBase();
    const OrtApi *api = nullptr;
    // Walk down rather than demanding ORT_API_VERSION: an installed runtime one or two releases
    // behind the headers still exposes everything the engine uses, and refusing it would strand
    // anyone whose addon has not been updated yet.
    for (int version = ORT_API_VERSION; version >= kMinApiVersion && !api; --version) {
        api = base->GetApi(version);
        if (api)
            g_apiVersion = version;
    }
    if (!api) {
        return fail(QStringLiteral("%1 is ONNX Runtime %2, which is older than this build of Drift "
                                   "can drive. Update it from the Addon Manager.")
                        .arg(chosen->libPath, QString::fromUtf8(base->GetVersionString())));
    }

    Ort::InitApi(api);
    g_variant = chosen->variant;
    g_libPath = chosen->libPath;
    g_version = chosen->version.isEmpty() ? QString::fromUtf8(base->GetVersionString())
                                          : chosen->version;
    g_state = State::Loaded;
    qInfo("[ort] loaded %s runtime %s (OrtApi %d) from %s", qUtf8Printable(g_variant),
          qUtf8Printable(g_version), g_apiVersion, qUtf8Printable(chosen->dir));
    return true;
}

bool available()
{
    QMutexLocker lock(&g_mutex);
    if (g_state == State::Loaded)
        return true;
    return !candidates().isEmpty();
}

Ort::Env &env()
{
    // Function-local so it is constructed on first use — which, by the contract above, is after
    // ensureLoaded() succeeded. Plugin EPs are registered against it here because registering the
    // same library twice is an error, and this is the only place that can happen once.
    // Leaked on purpose: destroying the env at exit unloads plugin EP libraries while their own
    // threads (Dawn's, for WebGPU EP 0.5.0) may still be running, which segfaults on quit.
    static Ort::Env *instance = [] {
        auto *created = new Ort::Env{ORT_LOGGING_LEVEL_ERROR, "drift"};
        registerPluginEps(*created);
#ifdef _WIN32
        if (g_variant == QLatin1String("winml") && g_apiVersion >= kPluginEpApiVersion)
            registerCatalogEps(*created);
#endif
        return created;
    }();
    return *instance;
}

const Ort::MemoryInfo &cpuMemory()
{
    static Ort::MemoryInfo instance = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    return instance;
}

QList<RuntimeInfo> installedRuntimes()
{
    QList<RuntimeInfo> runtimes;
    for (const Candidate &candidate : candidates())
        runtimes.append({candidate.variant, candidate.version, candidate.dir});
    return runtimes;
}

QList<PluginEp> installedPluginEps()
{
    QList<PluginEp> eps;
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_ONNXRUNTIME_EP_DIR"), QStringLiteral("onnxruntime-ep"),
        QString::fromLatin1(kPluginEpKind));
    for (const QString &root : roots) {
        const QJsonObject meta = readJson(QDir(root).filePath(QStringLiteral("ep.json")));
        if (meta.isEmpty())
            continue;

        PluginEp ep;
        ep.variant = meta.value(QStringLiteral("variant")).toString().trimmed().toLower();
        ep.minApiVersion = meta.value(QStringLiteral("minApiVersion")).toInt(kPluginEpApiVersion);

        const QString name = meta.value(QStringLiteral("library")).toString();
        QString path = QDir(root).filePath(QStringLiteral("lib/") + name);
        if (!QFileInfo::exists(path))
            path = QDir(root).filePath(name);
        if (ep.variant.isEmpty() || name.isEmpty() || !QFileInfo::exists(path))
            continue;

        ep.library = path;
        eps.append(ep);
    }
    return eps;
}

QList<PluginEp> registeredPluginEps()
{
    return g_registeredEps;
}

QString activeVariant()
{
    QMutexLocker lock(&g_mutex);
    return g_state == State::Loaded ? g_variant : QString();
}

QString activeVersion()
{
    QMutexLocker lock(&g_mutex);
    return g_state == State::Loaded ? g_version : QString();
}

QStringList selectableVariants()
{
    // Derived from what is on disk rather than from a loaded runtime, so the picker can be shown
    // and changed before anything has been loaded and without loading anything.
    QStringList variants{QStringLiteral("cpu")};
    for (const RuntimeInfo &runtime : installedRuntimes()) {
        // The Windows ML core is chosen by the provider it carries, not by its own name.
        const QString token = runtime.variant == QLatin1String("winml") ? QStringLiteral("directml")
                                                                        : runtime.variant;
        if (!variants.contains(token))
            variants.append(token);
    }
    for (const PluginEp &ep : installedPluginEps()) {
        if (!ep.variant.isEmpty() && !variants.contains(ep.variant))
            variants.append(ep.variant);
    }
    // What the Windows ML catalog contributed is only known once the env exists.
    for (const PluginEp &ep : registeredPluginEps()) {
        if (!variants.contains(ep.variant))
            variants.append(ep.variant);
    }
    return variants;
}

QString preferredVariant()
{
    const QString override = QString::fromLocal8Bit(qgetenv("DRIFT_ORT_EP")).trimmed().toLower();
    if (!override.isEmpty())
        return override;
    return QSettings()
        .value(QStringLiteral("ort/variant"), QStringLiteral("auto"))
        .toString()
        .trimmed()
        .toLower();
}

void setPreferredVariant(const QString &variant)
{
    QSettings().setValue(QStringLiteral("ort/variant"), variant.trimmed().toLower());
}

bool variantExplicit()
{
    const QString variant = preferredVariant();
    return !variant.isEmpty() && variant != QLatin1String("auto");
}

QString coreVariantFor(const QString &variant)
{
#ifdef _WIN32
    if (variant != QLatin1String("auto") && variant != QLatin1String("cpu")
        && variant != QLatin1String("cuda") && variant != QLatin1String("webgpu"))
        return QStringLiteral("winml");
#endif
    return variant;
}

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
namespace {

// What the MIGraphX EP links. Everything else ROCm needs comes in through these two, by their own
// $ORIGIN RUNPATHs.
struct RocmLibraries
{
    QString hip;      // libamdhip64.so.7
    QString migraphx; // libmigraphx_c.so.3
};

bool inFlatpak()
{
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

QString flatpakId()
{
    return qEnvironmentVariable("FLATPAK_ID", QStringLiteral("org.cutwire.Drift"));
}

// AMD's packages install to /opt/rocm-X.Y.Z, with /opt/rocm a symlink through /etc/alternatives.
// A Flatpak granted /opt cannot follow that link, since /etc is the runtime's, so the versioned
// directories are searched as well, newest first. Both libraries are taken from one root so a
// leftover older ROCm is never mixed with a newer one.
RocmLibraries findRocm()
{
    QStringList roots;
    if (const QString rocmPath = qEnvironmentVariable("ROCM_PATH"); !rocmPath.isEmpty())
        roots.append(rocmPath);
    roots.append(QStringLiteral("/opt/rocm"));
    QStringList versioned = QDir(QStringLiteral("/opt"))
                                .entryList({QStringLiteral("rocm-7*")},
                                           QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versioned.begin(), versioned.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a.mid(5)) > QVersionNumber::fromString(b.mid(5));
    });
    for (const QString &name : std::as_const(versioned))
        roots.append(QStringLiteral("/opt/") + name);

    const auto in = [](const QStringList &dirs, const QString &name) -> QString {
        for (const QString &dir : dirs) {
            const QString path = QDir(dir).filePath(name);
            if (QFileInfo::exists(path))
                return path;
        }
        return {};
    };
    const QString hipName = QStringLiteral("libamdhip64.so.7");
    const QString migraphxName = QStringLiteral("libmigraphx_c.so.3");

    for (const QString &root : std::as_const(roots)) {
        const QStringList dirs{root + QStringLiteral("/lib"),
                               root + QStringLiteral("/lib/migraphx/lib")};
        RocmLibraries found{in(dirs, hipName), in(dirs, migraphxName)};
        if (!found.hip.isEmpty() && !found.migraphx.isEmpty())
            return found;
    }

    // Distribution packages that put ROCm on the ordinary library path. Not exhaustive; where it
    // misses, the EP's own load is still the authority.
    QStringList dirs = qEnvironmentVariable("LD_LIBRARY_PATH").split(u':', Qt::SkipEmptyParts);
    dirs += {QStringLiteral("/usr/lib64"), QStringLiteral("/usr/lib/x86_64-linux-gnu"),
             QStringLiteral("/usr/lib"), QStringLiteral("/usr/local/lib")};
    return {in(dirs, hipName), in(dirs, migraphxName)};
}

} // namespace
#endif

QString providerSetupProblem(const QString &variant)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (variant != QLatin1String("migraphx"))
        return {};

    const RocmLibraries rocm = findRocm();
    QStringList missing;
    if (rocm.hip.isEmpty())
        missing.append(QStringLiteral("ROCm 7 (libamdhip64.so.7)"));
    if (rocm.migraphx.isEmpty())
        missing.append(QStringLiteral("MIGraphX (libmigraphx_c.so.3)"));
    if (!missing.isEmpty()) {
        if (inFlatpak()) {
            return QStringLiteral(
                       "AMD graphics needs ROCm 7 with MIGraphX, and %1 could not be found from "
                       "inside Drift's Flatpak sandbox. Install ROCm and MIGraphX from AMD "
                       "(https://rocm.docs.amd.com/projects/install-on-linux/), which puts them "
                       "under /opt, then let Drift read them and reach the GPU with "
                       "\"flatpak override --user --filesystem=/opt:ro --device=all %2\" and "
                       "restart Drift. ROCm installed under /usr by distribution packages cannot "
                       "be reached from the sandbox.")
                .arg(missing.join(QStringLiteral(" and ")), flatpakId());
        }
        return QStringLiteral(
                   "AMD graphics needs ROCm 7 with MIGraphX installed on this computer, and %1 "
                   "could not be found. Install ROCm and MIGraphX from AMD "
                   "(https://rocm.docs.amd.com/projects/install-on-linux/) or your "
                   "distribution's rocm and migraphx packages, then restart Drift.")
            .arg(missing.join(QStringLiteral(" and ")));
    }

    const QFileInfo kfd(QStringLiteral("/dev/kfd"));
    if (!kfd.exists()) {
        if (inFlatpak()) {
            return QStringLiteral(
                       "Drift's Flatpak sandbox cannot see the AMD compute device (/dev/kfd). "
                       "Allow it with \"flatpak override --user --device=all %1\" and restart "
                       "Drift. If that is already done, the amdgpu driver is not loaded or this "
                       "graphics card is not supported by ROCm.")
                .arg(flatpakId());
        }
        return QStringLiteral(
            "No AMD compute device was found (/dev/kfd is missing). The amdgpu driver is not "
            "loaded, or this graphics card is not supported by ROCm.");
    }
    if (!kfd.isReadable() || !kfd.isWritable()) {
        return QStringLiteral(
            "Your user cannot open the AMD compute device (/dev/kfd). Add yourself to the render "
            "and video groups (sudo usermod -aG render,video $USER), then sign out and back in.");
    }
#else
    Q_UNUSED(variant);
#endif
    return {};
}

QString prepareProvider(const QString &variant)
{
    if (const QString problem = providerSetupProblem(variant); !problem.isEmpty())
        return problem;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (variant != QLatin1String("migraphx"))
        return {};

    // The EP's RUNPATH only names /opt/rocm, which misses versioned installs and anything reached
    // through a Flatpak override. Loading the two libraries by the path found above first means
    // the EP's DT_NEEDED entries match by SONAME and the loader never has to search. Never
    // unloaded: the EP keeps using them for the life of the process.
    static QMutex mutex;
    static QString loadError;
    static bool loaded = false;
    QMutexLocker lock(&mutex);
    if (loaded)
        return loadError;
    loaded = true;
    const RocmLibraries rocm = findRocm();
    for (const QString &path : {rocm.hip, rocm.migraphx}) {
        if (!dlopen(path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL)) {
            loadError = QStringLiteral(
                            "ROCm was found at %1 but could not be loaded (%2). Reinstall ROCm "
                            "and MIGraphX, or check that the version is ROCm 7.")
                            .arg(QFileInfo(path).absolutePath(),
                                 QString::fromLocal8Bit(dlerror()));
            break;
        }
    }
    return loadError;
#else
    return {};
#endif
}

QString pluginEpNameForVariant(const QString &variant)
{
    for (const PluginEp &ep : std::as_const(g_registeredEps)) {
        if (ep.variant == variant)
            return ep.epName;
    }
    return {};
}

void markAutoProviderFailed(const char *tag, const QString &variant)
{
    if (variant.isEmpty())
        return;
    QMutexLocker lock(&g_mutex);
    g_autoFailed.insert(tag ? QString::fromLatin1(tag) + u'/' + variant : variant);
}

bool autoProviderFailed(const char *tag, const QString &variant)
{
    QMutexLocker lock(&g_mutex);
    return g_autoFailed.contains(variant)
        || (tag && g_autoFailed.contains(QString::fromLatin1(tag) + u'/' + variant));
}

void setProviderFailureHandler(ProviderFailureHandler handler)
{
    QMutexLocker lock(&g_askMutex);
    g_failureHandler = std::move(handler);
}

QString askForOtherProvider(const QString &failedVariant, const QString &error)
{
    if (!qEnvironmentVariableIsEmpty("DRIFT_ORT_EP"))
        return {};
    QMutexLocker lock(&g_askMutex);
    const QString current = preferredVariant();
    if (current != failedVariant)
        return current; // answered for another load while this one waited
    if (!g_failureHandler)
        return {};
    return g_failureHandler(failedVariant, error);
}

} // namespace drift::ort
