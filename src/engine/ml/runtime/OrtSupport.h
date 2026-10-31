#pragma once

#include "engine/ml/runtime/OrtRuntime.h"

#include <QDebug>
#include <QString>
#include <QThread>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Boilerplate every ONNX Runtime user in the engine needs: path conversion, session name
// enumeration, and execution-provider selection. Shared so the acceleration choice behaves
// identically everywhere rather than drifting per model.
//
// Loading the library itself is OrtRuntime.h's job, and nothing here may be called before
// ensureLoaded() has succeeded.
namespace drift::ort {

inline std::basic_string<ORTCHAR_T> ortPath(const QString &path)
{
#ifdef _WIN32
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}

inline std::vector<std::string> sessionNames(Ort::Session &s, bool inputs)
{
    Ort::AllocatorWithDefaultOptions alloc;
    std::vector<std::string> out;
    const size_t n = inputs ? s.GetInputCount() : s.GetOutputCount();
    for (size_t i = 0; i < n; ++i) {
        auto name = inputs ? s.GetInputNameAllocated(i, alloc) : s.GetOutputNameAllocated(i, alloc);
        out.emplace_back(name.get());
    }
    return out;
}

inline std::vector<const char *> cstrs(const std::vector<std::string> &v)
{
    std::vector<const char *> out;
    out.reserve(v.size());
    for (const std::string &s : v)
        out.push_back(s.c_str());
    return out;
}

inline int64_t elementCount(const std::vector<int64_t> &shape)
{
    int64_t n = 1;
    for (const int64_t d : shape)
        n *= d;
    return n;
}

// Which provider to attempt for this session; empty means plain CPU.
//
// On "auto", a registered plugin EP wins over a provider baked into the core: the plugin is only
// present because someone installed the addon on purpose, whereas CUDA comes along inside every
// GPU build whether or not the machine has a usable card. Providers that already failed for this
// model (or for everything) are passed over, so a broken one is tried once rather than on every
// load.
inline QString providerToTry(const char *tag)
{
    const QString requested = preferredVariant();
    if (requested == QLatin1String("cpu"))
        return {};
    if (variantExplicit())
        return requested;

    for (const PluginEp &ep : registeredPluginEps()) {
        if (!autoProviderFailed(tag, ep.variant))
            return ep.variant;
    }

    // A core carries at most one of these, so the order does not rank them.
    const std::vector<std::string> providers = Ort::GetAvailableProviders();
    for (const auto &[provider, variant] : {std::pair{"CUDAExecutionProvider", "cuda"},
                                            std::pair{"MIGraphXExecutionProvider", "migraphx"}}) {
        if (std::find(providers.begin(), providers.end(), provider) == providers.end())
            continue;
        if (autoProviderFailed(tag, QLatin1String(variant)))
            return {};
        return QLatin1String(variant);
    }
    return {}; // CPU-only core: nothing to try, and nothing worth saying about it
}

// Applies that choice. The acceleration preference (or DRIFT_ORT_EP) selects between the CUDA or
// MIGraphX provider carried by a GPU core, a plugin EP registered on the env, and plain CPU.
//
// Whether a failure is worth complaining about depends on who asked. An explicit choice that
// cannot be honoured must be loud, or identical timings look like "the GPU did not help" when in
// fact nothing was accelerated. The automatic attempt failing is ordinary — most machines have no
// CUDA — so that stays at info level.
//
// `sharedArena` registers one env-wide CUDA allocator grown by exactly what is asked for. Only
// pipelines that spread huge allocations across several sessions need it (see Sam2Segmenter);
// for a single small session it is pure overhead.
//
// Returns why an explicitly chosen provider could not be set up, empty otherwise. opts is left on
// CPU in that case, so callers that only warn can carry on.
inline QString appendRequestedProvider(Ort::SessionOptions &opts, Ort::Env &env, const char *tag,
                                       bool sharedArena = false)
{
    const QString ep = providerToTry(tag);
    if (ep.isEmpty())
        return {};
    const bool explicitEp = variantExplicit();

    try {
        if (const QString epName = pluginEpNameForVariant(ep); !epName.isEmpty()) {
            std::vector<Ort::ConstEpDevice> devices;
            for (const Ort::ConstEpDevice &device : env.GetEpDevices()) {
                if (epName == QLatin1String(device.EpName()))
                    devices.push_back(device);
            }
            if (devices.empty()) {
                qWarning("[%s] %s is registered but reports no usable device; using CPU.", tag,
                         qUtf8Printable(epName));
                if (!explicitEp)
                    markAutoProviderFailed(nullptr, ep);
                return explicitEp ? QStringLiteral("%1 reports no usable device.").arg(epName)
                                  : QString();
            }
            opts.AppendExecutionProvider_V2(env, devices,
                                            std::unordered_map<std::string, std::string>{});
        } else if (ep == QLatin1String("openvino")) {
            opts.AppendExecutionProvider("OpenVINO", {{"device_type", "GPU"}});
        } else if (ep == QLatin1String("migraphx")) {
            // ROCm is the user's install, not the addon's, so say what is missing instead of
            // passing on the loader's "cannot open shared object file".
            if (const QString problem = prepareProvider(ep); !problem.isEmpty()) {
                if (!explicitEp) {
                    qInfo("[%s] %s Using CPU.", tag, qUtf8Printable(problem));
                    markAutoProviderFailed(nullptr, ep);
                    return {};
                }
                qWarning("[%s] %s", tag, qUtf8Printable(problem));
                return problem;
            }
            // Zero is not the default for every field: the memory limit means "no limit" only
            // as SIZE_MAX.
            OrtMIGraphXProviderOptions migraphx{};
            migraphx.migraphx_mem_limit = SIZE_MAX;
            migraphx.migraphx_arena_extend_strategy = 1; // kSameAsRequested
            opts.AppendExecutionProvider_MIGraphX(migraphx);
        } else if (ep == QLatin1String("cuda")) {
            OrtCUDAProviderOptions cuda;
            cuda.arena_extend_strategy = 1; // kSameAsRequested
            opts.AppendExecutionProvider_CUDA(cuda);

            if (sharedArena) {
                Ort::MemoryInfo cudaMem("Cuda", OrtArenaAllocator, 0, OrtMemTypeDefault);
                Ort::ArenaCfg arena(0 /*max_mem: no cap*/, 1 /*kSameAsRequested*/,
                                    -1 /*initial chunk: default*/, -1 /*max dead bytes: default*/);
                env.CreateAndRegisterAllocatorV2("CUDAExecutionProvider", cudaMem, {}, arena);
                opts.AddConfigEntry("session.use_env_allocators", "1");
            }
        } else {
            qWarning("[%s] %s is not a provider this runtime offers; using CPU. Install the "
                     "matching Acceleration addon, or set the preference back to Auto.",
                     tag, qUtf8Printable(ep));
            return QStringLiteral("%1 is not available in the installed AI engine.").arg(ep);
        }
        qInfo("[%s] execution provider %s enabled.", tag, qUtf8Printable(ep));
    } catch (const Ort::Exception &e) {
        if (!explicitEp) {
            qInfo("[%s] %s is present but could not be initialised (%s); using CPU.", tag,
                  qUtf8Printable(ep), e.what());
            markAutoProviderFailed(nullptr, ep);
        } else {
            qWarning("[%s] %s could not be set up (%s). Falling back to CPU; if the message "
                     "mentions loading a shared library, the installed ONNX Runtime has no %s "
                     "provider.",
                     tag, qUtf8Printable(ep), e.what(), qUtf8Printable(ep));
            return QString::fromUtf8(e.what());
        }
    }
    return {};
}

// Builds a model's sessions, falling back to CPU when an automatically chosen provider turns out
// not to work.
//
// The fallback is not optional. AppendExecutionProvider_CUDA succeeds on any build that ships the
// provider — the device is only touched when a session is actually constructed, which is where
// "no CUDA-capable device is detected" surfaces. So a CUDA-enabled ONNX Runtime on a machine with
// no usable GPU passes every up-front check and then fails every model load. Retrying on CPU is
// what makes that machine merely slower instead of missing the feature entirely.
//
// An explicit acceleration choice is never silently overridden: asking for a provider and quietly
// getting CPU would hide exactly what the setting was changed to investigate. Instead the user is
// asked for a replacement (askForOtherProvider), and the build is retried with their answer.
//
// `build` takes a configured Ort::SessionOptions and creates the sessions; it may be called
// several times, so it must overwrite rather than append to whatever it fills.
template <typename BuildFn>
bool buildSessions(Ort::Env &env, const char *tag, bool sharedArena, QString *errorOut,
                   BuildFn &&build)
{
    for (;;) {
        const bool explicitEp = variantExplicit();
        const QString ep = providerToTry(tag);
        QString failure;
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
            failure = appendRequestedProvider(opts, env, tag, sharedArena);
            if (failure.isEmpty()) {
                build(opts);
                return true;
            }
        } catch (const Ort::Exception &e) {
            failure = QString::fromUtf8(e.what());
        }
        if (!explicitEp) {
            qInfo("[%s] the auto-selected execution provider could not create a session (%s); "
                  "retrying on CPU.",
                  tag, qUtf8Printable(failure));
            markAutoProviderFailed(tag, ep);
            break;
        }
        // The user picked this provider, so the user picks what replaces it.
        const QString next = askForOtherProvider(ep, failure);
        if (next.isEmpty() || next == ep) {
            if (errorOut)
                *errorOut = failure;
            return false;
        }
        qInfo("[%s] %s failed; retrying with %s.", tag, qUtf8Printable(ep), qUtf8Printable(next));
    }

    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount()));
        build(opts);
        return true;
    } catch (const Ort::Exception &e) {
        if (errorOut)
            *errorOut = QString::fromUtf8(e.what());
        return false;
    }
}

} // namespace drift::ort
