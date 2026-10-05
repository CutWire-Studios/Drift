// JUCE leaves these undefined on wasm. The AudioWorklet build has no pthreads, so a thread never
// starts; juce::dsp::Convolution then installs its impulse response synchronously in prepare()
// instead of on a loader thread, which is what the graph relies on anyway.
#include <juce_core/juce_core.h>

namespace juce {
bool Thread::createNativeThread(Priority) { return false; }
void Thread::killThread() {}
}
