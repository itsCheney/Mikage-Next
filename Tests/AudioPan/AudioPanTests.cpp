// Plays a stereo tone through the production SDL3 sound buffer into SDL's
// disk audio driver and checks the left/right levels for each pan.
#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "WaveFormatConverter.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The engine's critical section lives in sdl3_thread.cpp with TJS deps; the
// buffer only needs a recursive lock.
struct tTJSCriticalSectionImpl { std::recursive_mutex mutex; };
tTJSCriticalSection::tTJSCriticalSection() : _impl(new tTJSCriticalSectionImpl) {}
tTJSCriticalSection::~tTJSCriticalSection() { delete _impl; }
bool tTJSCriticalSection::lock() { _impl->mutex.lock(); return true; }
void tTJSCriticalSection::unlock() { _impl->mutex.unlock(); }
tTJSCriticalSectionHolder::tTJSCriticalSectionHolder(tTJSCriticalSection& cs) : _cs(&cs) { cs.lock(); }
tTJSCriticalSectionHolder::~tTJSCriticalSectionHolder() { _cs->unlock(); }

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount);

static int failures = 0;

struct Levels { double left = 0, right = 0; };

// Renders the tone with pan set before playback, then switches to
// `later` (when not NAN) half way and measures the last quarter instead.
static Levels Render(const std::string& dir, float pan, float later)
{
    const std::string file = dir + "/pan.raw";
    std::remove(file.c_str());
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
    SDL_SetHint("SDL_AUDIO_DISK_OUTPUT_FILE", file.c_str());
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    if (!SDL_Init(SDL_INIT_AUDIO))
    {
        std::printf("FAIL SDL_Init: %s\n", SDL_GetError());
        std::exit(1);
    }
    // The disk driver writes in its own device format.
    SDL_AudioSpec device{};
    SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, nullptr);
    tTVPWaveFormat fmt{};
    fmt.SamplesPerSec = 48000;
    fmt.Channels = 2;
    fmt.BitsPerSample = 16;
    fmt.BytesPerSample = 2;
    iTVPSoundBuffer* buffer = TVPCreateSoundBuffer(fmt, 8);
    if (!buffer)
    {
        std::printf("FAIL no sound buffer: %s\n", SDL_GetError());
        std::exit(1);
    }
    const int frames = 24000;
    std::vector<int16_t> tone(frames * 2);
    for (int i = 0; i < frames; ++i)
        tone[i * 2] = tone[i * 2 + 1] = (int16_t)(16000 * std::sin(i * 2 * M_PI * 440 / 48000));
    buffer->SetPan(pan);
    buffer->Play();
    buffer->AppendBuffer(tone.data(), (unsigned)(tone.size() * 2));
    if (!std::isnan(later))
    {
        while (buffer->GetCurrentPlaySamples() < (tjs_uint)frames / 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        buffer->SetPan(later);
    }
    // The resampler keeps a few frames back, so "played" stops just short.
    for (int i = 0; i < 400 && buffer->GetCurrentPlaySamples() + 64 < (tjs_uint)frames; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    buffer->Release();
    SDL_Quit();

    std::ifstream in(file, std::ios::binary | std::ios::ate);
    std::vector<char> raw(in ? (size_t)in.tellg() : 0);
    in.seekg(0);
    in.read(raw.data(), raw.size());
    std::vector<float> out;
    if (device.channels != 2 || (device.format != SDL_AUDIO_S16 && device.format != SDL_AUDIO_F32))
    {
        std::printf("FAIL unexpected disk format %d/%d\n", (int)device.format, device.channels);
        std::exit(1);
    }
    if (device.format == SDL_AUDIO_S16)
    {
        out.resize(raw.size() / 2);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = reinterpret_cast<const int16_t*>(raw.data())[i] / 32768.0f;
    }
    else
    {
        out.resize(raw.size() / 4);
        std::memcpy(out.data(), raw.data(), out.size() * 4);
    }
    // Only frames with signal count; the measured part is the tone's
    // second half when the pan changes mid-way.
    std::vector<size_t> loud;
    for (size_t f = 0; f + 1 < out.size(); f += 2)
        if (std::fabs(out[f]) + std::fabs(out[f + 1]) > 1e-4f)
            loud.push_back(f);
    Levels levels;
    if (loud.empty())
        return levels;
    size_t first = 0, n = loud.size();
    if (!std::isnan(later))
        first = n * 3 / 4;
    for (size_t i = first; i < n; ++i)
    {
        levels.left += out[loud[i]] * out[loud[i]];
        levels.right += out[loud[i] + 1] * out[loud[i] + 1];
    }
    levels.left = std::sqrt(levels.left / (n - first));
    levels.right = std::sqrt(levels.right / (n - first));
    return levels;
}

static void Check(const std::string& dir, float pan, float later, double left, double right)
{
    Levels got = Render(dir, pan, later);
    const double full = std::max(got.left, got.right);
    const bool ok = full > 0.1 && std::fabs(got.left / full - left) < 0.03 &&
                    std::fabs(got.right / full - right) < 0.03;
    std::printf("%s pan %.2f%s: left %.4f right %.4f (expected ratio %.2f/%.2f)\n",
                ok ? "PASS" : "FAIL", pan,
                std::isnan(later) ? "" : (" -> " + std::to_string(later)).c_str(),
                got.left, got.right, left, right);
    failures += !ok;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    Check(dir, 0.0f, NAN, 1.0, 1.0);
    Check(dir, 1.0f, NAN, 0.0, 1.0);
    Check(dir, -1.0f, NAN, 1.0, 0.0);
    Check(dir, 0.5f, NAN, 0.5, 1.0);
    Check(dir, -0.25f, NAN, 1.0, 0.75);
    // A change while playing reaches the queued audio, not only new buffers.
    Check(dir, 0.0f, -1.0f, 1.0, 0.0);
    return failures ? 1 : 0;
}
