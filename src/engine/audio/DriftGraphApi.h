#pragma once

#include <stdint.h>

// A C interface to the audio graph: one audio-effect.json in, interleaved stereo through it. Drift
// Forge's WebAssembly preview exports exactly these functions, the native golden renderer
// (tools/audiograph_render) links them, and the engine tests drive the DSP through them — three
// callers of one entry point, which is what keeps the preview honest.
//
// Not thread-safe; a graph belongs to whoever created it. Error strings stay valid until the next
// call that can fail.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DriftGraph DriftGraph;

// Files a manifest refers to ("ir": "ir/plate.wav") are staged by path before dg_create, which
// reads them; the stage is not cleared, so several graphs can share it.
void dg_stage_file(const char *path, const uint8_t *data, int size);
void dg_clear_files(void);

// The whole audio-effect.json. Null on failure, with dg_last_error() saying why.
DriftGraph *dg_create(const char *manifestJson, int sampleRate);
const char *dg_last_error(void);
void dg_destroy(DriftGraph *graph);

int dg_param_index(DriftGraph *graph, const char *identifier);
void dg_set_param(DriftGraph *graph, int index, float value);

// Knob indices follow the pedal's (or modulator's) knob order in the pedal catalog.
int dg_node_index(DriftGraph *graph, const char *nodeId);
void dg_set_knob(DriftGraph *graph, int node, int knob, float value);
int dg_modulator_index(DriftGraph *graph, const char *modulatorId);
int dg_modulator_count(DriftGraph *graph);
void dg_set_modulator_knob(DriftGraph *graph, int modulator, int knob, float value);
void dg_set_modulator_step(DriftGraph *graph, int modulator, int step, float value);
const float *dg_modulator_values(DriftGraph *graph);

// Clears all state, as after a seek, with LFO and step phases placed at clipSeconds.
void dg_reset(DriftGraph *graph, double clipSeconds);
int dg_latency(DriftGraph *graph);
// Frames of earlier audio to push through and discard after a reset so tails and latency line up
// — the same rule Drift's rack uses.
int dg_prime_frames(DriftGraph *graph);

// Interleaved stereo, processed in place; at most dg_io_capacity() frames per call.
float *dg_io(DriftGraph *graph);
int dg_io_capacity(void);
void dg_process(DriftGraph *graph, int frames);

void dg_enable_taps(DriftGraph *graph, int enabled);
int dg_tap_slots(DriftGraph *graph);
int dg_tap_stride(void);
const float *dg_collect_taps(DriftGraph *graph);

// The pedal and modulator catalog as JSON.
const char *dg_pedal_catalog(void);

#ifdef __cplusplus
}
#endif
