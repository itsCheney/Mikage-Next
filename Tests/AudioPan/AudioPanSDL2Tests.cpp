// Plays a tone through the production SDL2 sound buffer (sdl2-compat over
// SDL3, or SDL2) into SDL's disk audio driver and checks the left/right
// levels for each pan, for stereo and mono sources, and that a volume or pan
// change reaches audio that was already queued.
#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "WaveFormatConverter.h"
#include <SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The engine's critical section lives in the platform thread code with TJS
// deps; the buffer only needs a recursive lock.
struct tTJSCriticalSectionImpl { std::recursive_mutex mutex; };
tTJSCriticalSection::tTJSCriticalSection() : _impl(new tTJSCriticalSectionImpl) {}
tTJSCriticalSection::~tTJSCriticalSection() { delete _impl; }
bool tTJSCriticalSection::lock() { _impl->mutex.lock(); return true; }
void tTJSCriticalSection::unlock() { _impl->mutex.unlock(); }
tTJSCriticalSectionHolder::tTJSCriticalSectionHolder(tTJSCriticalSection& cs) : _cs(&cs) { cs.lock(); }
tTJSCriticalSectionHolder::~tTJSCriticalSectionHolder() { _cs->unlock(); }

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount);

static int failures = 0;
static const double kAmplitude = 16000 / 32768.0;

struct Levels { double left = 0, right = 0; };

struct Case
{
    int channels = 2;
    float pan = 0;
    float laterPan = NAN; // switched to half way when set
    float laterVolume = NAN;
    bool stream = false; // 1 s in short units, appended as buffers free up
};

// Renders the tone, queued up front in 125 ms units as the engine does (or
// streamed through a short queue), and measures the whole tone, or its last
// quarter when something changes half way.
static Levels Render(const std::string& dir, const Case& c)
{
    const std::string file = dir + "/pan-sdl2.raw";
    std::remove(file.c_str());
    // Names for SDL3 (under sdl2-compat) and for SDL2. The device is fixed
    // to S16 stereo 48 kHz, which is also what SDL2 opens for this buffer.
    setenv("SDL_AUDIO_DRIVER", "disk", 1);
    setenv("SDL_AUDIODRIVER", "disk", 1);
    setenv("SDL_AUDIO_DISK_OUTPUT_FILE", file.c_str(), 1);
    setenv("SDL_DISKAUDIOFILE", file.c_str(), 1);
    setenv("SDL_AUDIO_FORMAT", "S16", 1);
    setenv("SDL_AUDIO_CHANNELS", "2", 1);
    setenv("SDL_AUDIO_FREQUENCY", "48000", 1);
    setenv("SDL_AUDIO_DEVICE_SAMPLE_FRAMES", "512", 1);
    if (SDL_Init(SDL_INIT_AUDIO) != 0)
    {
        std::printf("FAIL SDL_Init: %s\n", SDL_GetError());
        std::exit(1);
    }
    tTVPWaveFormat fmt{};
    fmt.SamplesPerSec = 48000;
    fmt.Channels = c.channels;
    fmt.BitsPerSample = 16;
    fmt.BytesPerSample = 2;
    iTVPSoundBuffer* buffer = TVPCreateSoundBuffer(fmt, c.stream ? 4 : 8);
    if (!buffer)
    {
        std::printf("FAIL no sound buffer: %s\n", SDL_GetError());
        std::exit(1);
    }
    const int frames = c.stream ? 48000 : 24000, unit = c.stream ? 3000 : 6000;
    std::vector<int16_t> tone(frames * c.channels);
    for (int i = 0; i < frames; ++i)
        for (int ch = 0; ch < c.channels; ++ch)
            tone[i * c.channels + ch] = (int16_t)(16000 * std::sin(i * 2 * M_PI * 440 / 48000));
    buffer->SetPan(c.pan);
    buffer->Play();
    for (int i = 0; i < frames; i += unit)
    {
        while (!buffer->IsBufferValid())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        buffer->AppendBuffer(tone.data() + i * c.channels, (unsigned)(unit * c.channels * 2));
    }
    const bool changes = !std::isnan(c.laterPan) || !std::isnan(c.laterVolume);
    if (changes)
    {
        while (buffer->GetCurrentPlaySamples() < (tjs_uint)frames / 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (!std::isnan(c.laterPan))
            buffer->SetPan(c.laterPan);
        if (!std::isnan(c.laterVolume))
            buffer->SetVolume(c.laterVolume);
    }
    for (int i = 0; i < 400 && buffer->GetCurrentPlaySamples() < (tjs_uint)frames; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // Let the device write out what it has already taken.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const int remain = buffer->GetRemainBuffers();
    if (remain != 0)
    {
        std::printf("FAIL %d buffer(s) still remain after the tone played\n", remain);
        failures++;
    }
    buffer->Release();
    SDL_Quit();

    std::ifstream in(file, std::ios::binary | std::ios::ate);
    std::vector<char> raw(in ? (size_t)in.tellg() : 0);
    in.seekg(0);
    in.read(raw.data(), raw.size());
    std::vector<float> out(raw.size() / 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = reinterpret_cast<const int16_t*>(raw.data())[i] / 32768.0f;
    std::vector<size_t> loud;
    for (size_t f = 0; f + 1 < out.size(); f += 2)
        if (std::fabs(out[f]) + std::fabs(out[f + 1]) > 1e-4f)
            loud.push_back(f);
    Levels levels;
    if (loud.empty())
        return levels;
    size_t first = 0, n = loud.size();
    if (changes)
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

static std::string Describe(const Case& c)
{
    char text[128];
    std::snprintf(text, sizeof text, "%s pan %.2f", c.channels == 1 ? "mono" : "stereo", c.pan);
    std::string s = text;
    if (!std::isnan(c.laterPan))
        s += " -> pan " + std::to_string(c.laterPan);
    if (!std::isnan(c.laterVolume))
        s += " -> volume " + std::to_string(c.laterVolume);
    if (c.stream)
        s += " (streamed)";
    return s;
}

// Checks the left/right ratio.
static void Check(const Case& c, const std::string& dir, double left, double right)
{
    Levels got = Render(dir, c);
    const double full = std::max(got.left, got.right);
    const bool ok = full > 0.1 && std::fabs(got.left / full - left) < 0.03 &&
                    std::fabs(got.right / full - right) < 0.03;
    std::printf("%s %s: left %.4f right %.4f (expected ratio %.2f/%.2f)\n", ok ? "PASS" : "FAIL",
                Describe(c).c_str(), got.left, got.right, left, right);
    failures += !ok;
}

// Checks the absolute level against the tone's RMS times `gain`.
static void CheckLevel(const Case& c, const std::string& dir, double gain)
{
    Levels got = Render(dir, c);
    const double want = kAmplitude / std::sqrt(2.0) * gain;
    const bool ok = std::fabs(got.left - want) < want * 0.05 && std::fabs(got.right - want) < want * 0.05;
    std::printf("%s %s: left %.4f right %.4f (expected %.4f)\n", ok ? "PASS" : "FAIL",
                Describe(c).c_str(), got.left, got.right, want);
    failures += !ok;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    Check({2, 0.0f}, dir, 1.0, 1.0);
    Check({2, 1.0f}, dir, 0.0, 1.0);
    Check({2, -1.0f}, dir, 1.0, 0.0);
    Check({2, 0.5f}, dir, 0.5, 1.0);
    Check({2, -0.25f}, dir, 1.0, 0.75);
    // A mono source is spread to both sides, so pan reaches it too.
    Check({1, 0.0f}, dir, 1.0, 1.0);
    Check({1, 0.5f}, dir, 0.5, 1.0);
    Check({1, -1.0f}, dir, 1.0, 0.0);
    // Changes while playing reach the audio already queued, not only new
    // buffers.
    Check({2, 0.0f, -1.0f}, dir, 1.0, 0.0);
    Check({1, 0.0f, 1.0f}, dir, 0.0, 1.0);
    CheckLevel({2, 0.0f}, dir, 1.0);
    CheckLevel({2, 0.0f, NAN, 0.25f}, dir, 0.25);
    // Streaming wraps the buffer's internal queue.
    Case streamed{2, 0.5f};
    streamed.stream = true;
    Check(streamed, dir, 0.5, 1.0);
    CheckLevel({2, 0.0f, NAN, NAN, true}, dir, 1.0);
    return failures ? 1 : 0;
}
