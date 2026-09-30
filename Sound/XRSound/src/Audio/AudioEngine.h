// not upstream: XRSound's audio engine on PipeWire (one float32 stereo 48 kHz playback stream mixed here); replaces the closed-source irrKlang

#pragma once

#include "AudioDecode.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class AudioEngine;
struct AudioLoad;
struct AudioFeed;

// one playing sound: the caller holds it until Release, the engine frees it once finished; methods lock the engine
class AudioVoice
{
public:
    bool IsFinished() const;            // played to the end, stopped, or its file failed to decode
    void Stop();
    void Release();                     // the caller is done with the handle

    void SetPaused(const bool bPaused);
    bool IsPaused() const;
    void SetVolume(const float volume); // 0 (muted) .. 1 (full)
    float GetVolume() const;
    void SetLooped(const bool bLoop);
    bool IsLooped() const;
    void SetPan(const float pan);       // -1 full left .. 0 centre .. 1 full right
    float GetPan() const;
    bool SetPlaybackSpeed(const float speed);   // 1 = normal, any finite speed > 0; resampled, so the pitch follows the speed
    float GetPlaybackSpeed() const;
    bool SetPlayPosition(const uint32_t positionMillis);
    int GetPlayPosition() const;        // milliseconds
    int GetLength() const;              // milliseconds, -1 while not known (a compressed file still loading, or no length in its header)

private:
    friend class AudioEngine;
    AudioVoice(AudioEngine *pEngine);
    ~AudioVoice();

    void Adopt();                                   // takes the result of a finished load; engine lock held
    void Mix(float *pOut, const uint32_t frames);   // mixer thread or silent clock, engine lock held
    void SeekFeed(const uint64_t frame);            // engine lock held
    void Frame(const int64_t index, float &left, float &right) const;
    uint64_t SourcePosition() const;

    AudioEngine *m_pEngine;
    std::shared_ptr<AudioLoad> m_load;      // the load that makes this sound (kept, so the mixer never frees anything)
    std::shared_ptr<AudioBuffer> m_buffer;  // the whole sound, or
    std::shared_ptr<AudioFeed> m_feed;      // a ring the feeder thread decodes into
    uint32_t m_channels;        // 1 or 2
    uint32_t m_sampleRate;      // 0 while not known
    uint64_t m_frames;          // 0 while not known
    double m_pos;               // buffer: source frame; feed: feed frame
    uint64_t m_srcOrigin;       // feed: source frame at m_passStart
    uint64_t m_passStart;       // feed: feed frame where the current pass began
    uint64_t m_ringHead;        // feed: the decoded range, read under its lock at the start of a period
    uint64_t m_ringTail;
    int64_t m_pendingMillis;    // a position asked for while loading
    float m_volume;
    float m_pan;
    float m_speed;
    bool m_bLoading;
    bool m_bLoop;
    bool m_bPaused;
    bool m_bFinished;
    bool m_bReleased;
    bool m_bWrapped;            // buffer: a loop pass ended, so frames before 0 come from the previous pass
    bool m_bGainSet;
    float m_gainL;              // gains of the last mixed frame; changes ramp over one period to avoid clicks
    float m_gainR;
};

class AudioEngine
{
public:
    // loads libpipewire and connects a playback stream to the daemon; nullptr and an error text if either fails (sound is then disabled)
    static AudioEngine *Create(const char *pAppName, std::string &error, const char *pLibrary = "libpipewire-0.3.so.0");
    // an engine without PipeWire: the caller pulls the mix with Render (tests)
    static AudioEngine *CreateOffline();
    ~AudioEngine();

    const char *GetDriverName() const { return m_driverName.c_str(); }

    // starts a sound (header read here, decoding on the worker); nullptr and an error if unreadable, unknown or failed before
    AudioVoice *Play(const char *pPath, const bool bLoop, const bool bStartPaused, std::string &error);

    // frees finished voices, trims the cache, keeps time without output, reconnects; appends XRSound.log lines
    void Update(std::vector<std::string> &log, const bool bLoadErrors);

    // offline engine only: mixes the next frames, waiting until loads and stream rings are ready (deterministic tests)
    void Render(float *pOut, const uint32_t frames);

    size_t GetCacheBytes() const { return m_cacheBytes; }
    uint64_t GetPeriods() const { return m_periods; }                   // process calls so far (tests)
    void SetWholeLimit(const size_t bytes) { m_wholeLimit = bytes; }    // tests: decoded size above which a sound streams

    static constexpr uint32_t SampleRate = 48000;
    static constexpr uint32_t Channels = 2;
    static constexpr size_t WholeLimit = 8 * 1024 * 1024;       // sounds whose floats fit in this are decoded whole and cached
    static constexpr size_t CacheBudget = 256 * 1024 * 1024;    // bytes of cached sounds kept when no voice uses them

private:
    friend class AudioVoice;
    struct PipeWire;    // PipeWire objects and callbacks, kept out of this header
    struct Workers;     // loader and feeder threads

    AudioEngine();
    bool Start(const char *pAppName, std::string &error);
    bool Connect(std::string &error);
    void Disconnect();
    void Reconnect(std::vector<std::string> &log);
    void Process();     // PipeWire asks for the next period
    void Mix(float *pOut, const uint32_t frames);
    void TrimCache();
    void LoaderThread();
    void FeederThread();
    void RunLoad(AudioLoad &load);
    bool TopUp(AudioFeed &feed);
    void WakeFeeder();

    std::unique_ptr<PipeWire> m_pw;
    std::unique_ptr<Workers> m_workers;
    mutable std::mutex m_mutex;         // guards the voices between the Orbiter thread and the mixer thread
    std::vector<AudioVoice *> m_voices;
    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> m_bufferCache;  // key = file path
    std::unordered_map<std::string, std::weak_ptr<AudioLoad>> m_pending;          // whole loads a second Play can join
    std::unordered_map<std::string, std::string> m_failCache;                     // files that failed, with the reason
    size_t m_cacheBytes;
    std::atomic<size_t> m_wholeLimit;
    std::atomic<bool> m_bStop;          // the worker threads end; every file read fails from then on
    std::atomic<bool> m_bLost;          // the stream or the daemon connection broke
    std::atomic<bool> m_bConnected;     // the stream reached PAUSED or STREAMING since the last (re)connect
    std::atomic<int64_t> m_lastProcess; // steady clock nanoseconds of the last process call
    std::atomic<uint64_t> m_periods;
    bool m_bOffline;
    bool m_bConnecting;
    bool m_bLostLogged;
    int64_t m_lastSilent;               // the silent clock's last tick
    int64_t m_retryAt;                  // next reconnect attempt
    int64_t m_connectDeadline;
    int64_t m_backoff;                  // nanoseconds until the next attempt after this one
    std::vector<float> m_scratch;       // the silent clock's output, thrown away
    std::string m_appName;
    std::string m_driverName;
};
