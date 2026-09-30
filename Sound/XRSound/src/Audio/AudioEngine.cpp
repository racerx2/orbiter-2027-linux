// not upstream: XRSound's audio engine on PipeWire (one float32 stereo 48 kHz playback stream mixed here); replaces the closed-source irrKlang

#include "AudioEngine.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <new>
#include <pthread.h>
#include <stdexcept>
#include <thread>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace
{
const int ConnectTimeoutSeconds = 3;    // longest wait for the daemon to take the stream, so Orbiter never hangs on it
const int64_t Second = 1000000000;      // steady clock nanoseconds
const int64_t SilentAfter = Second / 4; // no process call for this long: there is no output, the silent clock keeps time
const int64_t MaxBackoff = 30 * Second;
const uint64_t RingFrames = 65536;      // decoded frames a stream keeps (a power of two)
const uint64_t History = 64;            // frames kept behind the play position for the filter
const uint64_t FeedChunk = 8192;        // frames the feeder decodes at a time
const uint64_t MinRoom = 1024;          // the feeder waits until the ring has this much room
const double MaxStep = 64;              // source frames per output frame at most
const int Half = 8;                     // filter zero crossings on each side
const int Taps = 2 * Half;
const int Phases = 256;
const double MaxScale = 4;              // the filter widens for steps up to this; above it some aliasing remains
const size_t MaxFailures = 1024;
enum { LoadPending, LoadDone, LoadFailed };

int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// libpipewire is opened at run time, so XRSound.so loads, and says why sound is off, where the library is missing
#define XRS_PW_FUNCTIONS(X) X(pw_init) X(pw_deinit) X(pw_get_library_version) X(pw_thread_loop_new) X(pw_thread_loop_get_loop) \
    X(pw_thread_loop_start) X(pw_thread_loop_stop) X(pw_thread_loop_destroy) X(pw_thread_loop_lock) X(pw_thread_loop_unlock) \
    X(pw_thread_loop_signal) X(pw_thread_loop_timed_wait) X(pw_context_new) X(pw_context_connect) X(pw_context_destroy) \
    X(pw_core_disconnect) X(pw_properties_new) X(pw_stream_new) X(pw_stream_add_listener) X(pw_stream_connect) \
    X(pw_stream_destroy) X(pw_stream_dequeue_buffer) X(pw_stream_queue_buffer)
#define XRS_PW_MEMBER(name) decltype(&::name) name = nullptr;
#define XRS_PW_RESOLVE(name) api.name = reinterpret_cast<decltype(api.name)>(dlsym(pLib, #name)); if (!api.name) missing = #name;

struct PipeWireApi
{
    XRS_PW_FUNCTIONS(XRS_PW_MEMBER)
};

PipeWireApi s_pw;
void *s_pwLibrary = nullptr;    // never closed: libpipewire is not made to be unloaded
std::mutex s_pwLock;

bool LoadPipeWire(const char *pLibrary, std::string &error)
{
    std::lock_guard<std::mutex> lock(s_pwLock);
    void *pLib = dlopen(pLibrary, RTLD_NOW | RTLD_LOCAL);   // a second open of a loaded library only counts up
    if (!pLib)
    {
        const char *pError = dlerror();
        error = pError ? pError : (std::string("can't load ") + pLibrary);
        return false;
    }
    if (s_pwLibrary)
        return true;    // the functions are known already
    PipeWireApi api;
    const char *missing = nullptr;
    XRS_PW_FUNCTIONS(XRS_PW_RESOLVE)
    if (missing)
    {
        error = std::string(pLibrary) + " has no " + missing;
        dlclose(pLib);
        return false;
    }
    s_pw = api;
    s_pwLibrary = pLib;
    return true;
}

double Bessel0(const double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 30; k++)
    {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}

// band-limited resampling: a Kaiser-windowed sinc (cutoff 0.45 of the source rate), 256 phases linearly interpolated
struct Kernel
{
    float taps[(Phases + 1) * Taps];   // row p holds the taps at offset t = (k - Half + 1) - p / Phases

    Kernel()
    {
        const double cutoff = 0.45, beta = 7.0;
        for (int p = 0; p <= Phases; p++)
        {
            float *row = taps + p * Taps;
            double sum = 0;
            for (int k = 0; k < Taps; k++)
            {
                const double t = (k - Half + 1) - static_cast<double>(p) / Phases;
                const double x = 2 * cutoff * t;
                const double sinc = (x == 0) ? 1 : sin(M_PI * x) / (M_PI * x);
                const double r = t / Half;
                const double window = (fabs(r) >= 1) ? 0 : Bessel0(beta * sqrt(1 - r * r)) / Bessel0(beta);
                row[k] = static_cast<float>(2 * cutoff * sinc * window);
                sum += row[k];
            }
            for (int k = 0; k < Taps; k++)
                row[k] = static_cast<float>(row[k] / sum);     // unity gain at every phase
        }
    }
};

const Kernel s_kernel;
}

// one file on its way to a voice; the worker threads fill it, voices and Update read it once state says so
struct AudioLoad
{
    std::string path;
    std::shared_ptr<AudioSource> source;    // loader only
    AudioProbe probe;
    bool bWhole = false;        // a WAV that fits the whole limit: a second Play of it joins this load
    bool bLoop = false;         // the voice's loop flag when a stream starts
    std::atomic<int> state{ LoadPending };
    std::shared_ptr<AudioBuffer> buffer;    // the result: a whole sound, or
    std::shared_ptr<AudioFeed> feed;        // a stream
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
    std::string error;
    bool bTransient = false;
};

// a stream's decoded frames: the feeder writes beyond head, the mixer reads below it
struct AudioFeed
{
    AudioFeed(std::unique_ptr<AudioStream> decoder, const bool bLoop) :
        stream(std::move(decoder)), inChannels(stream->channels), outChannels(AudioDecode::OutChannels(stream->channels)),
        ring(RingFrames * outChannels), bLoop(bLoop)
    {
    }

    std::unique_ptr<AudioStream> stream;    // feeder only
    const uint32_t inChannels;
    const uint32_t outChannels;
    std::vector<float> ring;
    uint64_t srcPos = 0;        // feeder only: source frame of the next decode
    uint64_t srcStart = 0;      // feeder only: where the current sequential run began

    std::mutex lock;            // guards the fields below
    uint64_t head = 0;          // feed frames decoded
    uint64_t tail = 0;          // oldest feed frame the mixer still reads
    uint32_t gen = 0;           // seek requests
    uint32_t doneGen = 0;       // the last seek the feeder did
    uint64_t seekTo = 0;        // source frame
    bool bLoop;
    bool bEnd = false;          // the source ended at endFrame and does not loop
    bool bBroken = false;       // the decoder can't go on
    uint64_t endFrame = 0;
    std::deque<uint64_t> seams; // feed frames where a loop pass begins again at source frame 0
    uint64_t length = 0;        // source frames, learned at a sequential end
};

struct AudioEngine::Workers
{
    std::mutex lock;
    std::condition_variable loaderWake;
    std::condition_variable feederWake;
    std::deque<std::shared_ptr<AudioLoad>> loads;
    std::vector<std::shared_ptr<AudioLoad>> completed;   // for Update: caches and log
    std::vector<std::weak_ptr<AudioFeed>> feeds;         // a feed ends with its voice
    std::vector<float> in, out;                          // feeder scratch
    std::thread loader;
    std::thread feeder;
};

// PipeWire objects; the callbacks run on the thread loop with its lock held
struct AudioEngine::PipeWire
{
    pw_thread_loop *pLoop = nullptr;
    pw_context *pContext = nullptr;
    pw_core *pCore = nullptr;
    pw_stream *pStream = nullptr;
    spa_hook coreListener{};
    spa_hook streamListener{};
    std::string error;          // written by the callbacks; read under the loop lock or with the loop stopped
    bool bInitialized = false;
    bool bRunning = false;

    static const pw_core_events coreEvents;
    static const pw_stream_events streamEvents;

    static void CoreError(void *pData, uint32_t id, int seq, int res, const char *pMessage)
    {
        AudioEngine *pEngine = static_cast<AudioEngine *>(pData);
        if ((id != PW_ID_CORE) || (res != -EPIPE))
            return;     // only a broken pipe ends the connection; other errors answer single requests
        if (!pEngine->m_bLost)
            pEngine->m_pw->error = std::string("PipeWire connection: ") + (pMessage ? pMessage : strerror(-res));   // res is a negative errno
        pEngine->m_bLost = true;
        s_pw.pw_thread_loop_signal(pEngine->m_pw->pLoop, false);
    }

    static void StreamStateChanged(void *pData, pw_stream_state oldState, pw_stream_state state, const char *pError)
    {
        AudioEngine *pEngine = static_cast<AudioEngine *>(pData);
        // a destroyed node or a dead daemon leave the stream UNCONNECTED without an error
        if ((state == PW_STREAM_STATE_ERROR) || ((state == PW_STREAM_STATE_UNCONNECTED) && (oldState != PW_STREAM_STATE_UNCONNECTED)))
        {
            if (!pEngine->m_bLost)
                pEngine->m_pw->error = std::string("PipeWire stream: ") + (pError ? pError : "disconnected");
            pEngine->m_bLost = true;
        }
        else if ((state == PW_STREAM_STATE_PAUSED) || (state == PW_STREAM_STATE_STREAMING))
            pEngine->m_bConnected = true;
        s_pw.pw_thread_loop_signal(pEngine->m_pw->pLoop, false);
    }

    static void StreamProcess(void *pData)
    {
        static_cast<AudioEngine *>(pData)->Process();
    }

    static pw_core_events MakeCoreEvents()
    {
        pw_core_events events{};
        events.version = PW_VERSION_CORE_EVENTS;
        events.error = CoreError;
        return events;
    }

    static pw_stream_events MakeStreamEvents()
    {
        pw_stream_events events{};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.state_changed = StreamStateChanged;
        events.process = StreamProcess;
        return events;
    }
};

const pw_core_events AudioEngine::PipeWire::coreEvents = AudioEngine::PipeWire::MakeCoreEvents();
const pw_stream_events AudioEngine::PipeWire::streamEvents = AudioEngine::PipeWire::MakeStreamEvents();

AudioEngine::AudioEngine() :
    m_pw(new PipeWire), m_workers(new Workers), m_cacheBytes(0), m_wholeLimit(WholeLimit), m_bStop(false), m_bLost(false),
    m_bConnected(false), m_lastProcess(Now()), m_periods(0), m_bOffline(false), m_bConnecting(false), m_bLostLogged(false), m_lastSilent(0),
    m_retryAt(0), m_connectDeadline(0), m_backoff(Second), m_scratch(1024 * Channels)
{
    m_workers->loader = std::thread(&AudioEngine::LoaderThread, this);
    m_workers->feeder = std::thread(&AudioEngine::FeederThread, this);
}

AudioEngine *AudioEngine::Create(const char *pAppName, std::string &error, const char *pLibrary)
{
    if (!LoadPipeWire(pLibrary, error))
        return nullptr;
    AudioEngine *pEngine = new AudioEngine();
    if (!pEngine->Start(pAppName, error))
    {
        delete pEngine;
        return nullptr;
    }
    return pEngine;
}

AudioEngine *AudioEngine::CreateOffline()
{
    AudioEngine *pEngine = new AudioEngine();
    pEngine->m_bOffline = true;
    pEngine->m_driverName = "offline";
    return pEngine;
}

bool AudioEngine::Start(const char *pAppName, std::string &error)
{
    m_appName = pAppName;
    s_pw.pw_init(nullptr, nullptr);
    m_pw->bInitialized = true;

    m_pw->pLoop = s_pw.pw_thread_loop_new("XRSound", nullptr);
    if (!m_pw->pLoop)
    {
        error = std::string("can't create the PipeWire thread loop: ") + strerror(errno);
        return false;
    }
    m_pw->pContext = s_pw.pw_context_new(s_pw.pw_thread_loop_get_loop(m_pw->pLoop), nullptr, 0);
    if (!m_pw->pContext)
    {
        error = std::string("can't create a PipeWire context: ") + strerror(errno);
        return false;
    }
    if (!Connect(error))
        return false;

    s_pw.pw_thread_loop_lock(m_pw->pLoop);
    auto fail = [this, &error](const std::string &text)
    {
        error = text;
        s_pw.pw_thread_loop_unlock(m_pw->pLoop);
        return false;
    };

    // PAUSED means the daemon made our node; STREAMING follows once the session manager links it to a sink
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(ConnectTimeoutSeconds);
    while (!m_bLost && !m_bConnected)
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return fail("the PipeWire daemon did not take the playback stream in time");
        s_pw.pw_thread_loop_timed_wait(m_pw->pLoop, 1);
    }
    if (m_bLost)
        return fail(m_pw->error);
    s_pw.pw_thread_loop_unlock(m_pw->pLoop);

    m_driverName = std::string("PipeWire ") + s_pw.pw_get_library_version() + " (" + std::to_string(SampleRate) + " Hz float32 stereo)";
    return true;
}

// a core and a playback stream on the context, then the loop thread; nothing waits here
bool AudioEngine::Connect(std::string &error)
{
    m_bLost = false;
    m_bConnected = false;
    m_pw->error.clear();

    // connect before the loop thread starts: stopping a just-started thread loop can hang in pw_thread_loop_stop
    m_pw->pCore = s_pw.pw_context_connect(m_pw->pContext, nullptr, 0);   // fails at once when no daemon listens on the socket
    if (!m_pw->pCore)
    {
        error = std::string("no PipeWire daemon: ") + strerror(errno);
        return false;
    }
    m_pw->coreListener = spa_hook{};
    pw_core_add_listener(m_pw->pCore, &m_pw->coreListener, &PipeWire::coreEvents, this);

    pw_properties *pProps = s_pw.pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Game",
        PW_KEY_APP_NAME, m_appName.c_str(),
        PW_KEY_NODE_NAME, "XRSound",
        PW_KEY_NODE_LATENCY, "1024/48000",
        nullptr);
    m_pw->pStream = s_pw.pw_stream_new(m_pw->pCore, "XRSound", pProps);   // takes pProps
    if (!m_pw->pStream)
    {
        error = std::string("can't create a PipeWire stream: ") + strerror(errno);
        return false;
    }
    m_pw->streamListener = spa_hook{};
    s_pw.pw_stream_add_listener(m_pw->pStream, &m_pw->streamListener, &PipeWire::streamEvents, this);

    uint8_t podBuffer[1024];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, podBuffer, sizeof(podBuffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = SampleRate;
    info.channels = Channels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = { spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info) };

    const pw_stream_flags flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
    if (s_pw.pw_stream_connect(m_pw->pStream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) < 0)
    {
        error = "can't connect the PipeWire playback stream";
        return false;
    }

    if (s_pw.pw_thread_loop_start(m_pw->pLoop) < 0)
    {
        error = "can't start the PipeWire thread loop";
        return false;
    }
    m_pw->bRunning = true;
    return true;
}

// stream and core go; the loop thread is stopped first, so the mixer is not called after this
void AudioEngine::Disconnect()
{
    if (m_pw->bRunning)
    {
        s_pw.pw_thread_loop_stop(m_pw->pLoop);
        m_pw->bRunning = false;
    }
    if (m_pw->pStream)
    {
        if (m_pw->streamListener.link.next)
            spa_hook_remove(&m_pw->streamListener);
        m_pw->streamListener = spa_hook{};
        s_pw.pw_stream_destroy(m_pw->pStream);
        m_pw->pStream = nullptr;
    }
    if (m_pw->pCore)
    {
        if (m_pw->coreListener.link.next)
            spa_hook_remove(&m_pw->coreListener);
        m_pw->coreListener = spa_hook{};
        s_pw.pw_core_disconnect(m_pw->pCore);
        m_pw->pCore = nullptr;
    }
}

// Update's part for a lost output: log it once, then rebuild core and stream with a growing pause between tries
void AudioEngine::Reconnect(std::vector<std::string> &log)
{
    const int64_t now = Now();
    if (m_bLost)
    {
        if (!m_bLostLogged)
        {
            std::string text;
            if (m_pw->bRunning)
            {
                s_pw.pw_thread_loop_lock(m_pw->pLoop);
                text = m_pw->error;
                s_pw.pw_thread_loop_unlock(m_pw->pLoop);
            }
            else
                text = m_pw->error;
            log.push_back("sound output lost (" + text + "); trying to reconnect");
            m_bLostLogged = true;
            m_bConnecting = false;
            m_retryAt = now + m_backoff;
            m_backoff = std::min(2 * m_backoff, MaxBackoff);    // the pauses go 1, 2, 4 s and on
        }
        if (now < m_retryAt)
            return;
        Disconnect();
        std::string error;
        if (Connect(error))
        {
            m_bConnecting = true;
            m_connectDeadline = now + ConnectTimeoutSeconds * Second;
        }
        else
        {
            Disconnect();
            m_pw->error = error;
            m_bLost = true;
        }
        m_retryAt = now + m_backoff;
        m_backoff = std::min(2 * m_backoff, MaxBackoff);
        return;
    }
    if (m_bConnecting)
    {
        if (m_bConnected)
        {
            m_bConnecting = false;
            m_bLostLogged = false;
            m_backoff = Second;
            log.push_back("sound output restored");
        }
        else if (now >= m_connectDeadline)
        {
            s_pw.pw_thread_loop_lock(m_pw->pLoop);
            m_pw->error = "the PipeWire daemon did not take the playback stream in time";
            m_bLost = true;
            s_pw.pw_thread_loop_unlock(m_pw->pLoop);
        }
    }
}

AudioEngine::~AudioEngine()
{
    Disconnect();   // the mixer is not called after this
    {
        std::lock_guard<std::mutex> lock(m_workers->lock);
        m_bStop = true;     // file reads fail from now on, so a long open ends quickly
    }
    m_workers->loaderWake.notify_all();
    m_workers->feederWake.notify_all();
    m_workers->loader.join();
    m_workers->feeder.join();

    for (AudioVoice *pVoice : m_voices)
        delete pVoice;
    m_workers.reset();
    if (m_pw->pContext)
        s_pw.pw_context_destroy(m_pw->pContext);
    if (m_pw->pLoop)
        s_pw.pw_thread_loop_destroy(m_pw->pLoop);
    if (m_pw->bInitialized)
        s_pw.pw_deinit();
}

AudioVoice *AudioEngine::Play(const char *pPath, const bool bLoop, const bool bStartPaused, std::string &error)
{
    if (!pPath || !*pPath)
    {
        error = "no file name";
        return nullptr;
    }

    // decoded sounds are cached by path, as irrKlang kept its sound sources; a file that failed stays failed
    const std::string key(pPath);
    auto itFail = m_failCache.find(key);
    if (itFail != m_failCache.end())
    {
        error = itFail->second;
        return nullptr;
    }
    AudioVoice *pVoice = nullptr;
    try     // a corrupt header can ask for any size: the caller gets an error text, as for an unreadable file
    {
        pVoice = new AudioVoice(this);
        auto itBuffer = m_bufferCache.find(key);
        std::shared_ptr<AudioLoad> pending;
        auto itPending = m_pending.find(key);
        if (itPending != m_pending.end())
            pending = itPending->second.lock();
        if (itBuffer != m_bufferCache.end())
        {
            pVoice->m_buffer = itBuffer->second;
            pVoice->m_channels = itBuffer->second->channels;
            pVoice->m_sampleRate = itBuffer->second->sampleRate;
            pVoice->m_frames = itBuffer->second->frames;
        }
        else
        {
            if (!pending)
            {
                // only the header is read here; opening the decoder and decoding run on the loader thread
                bool bTransient = false;
                std::shared_ptr<AudioSource> source = AudioSource::Open(pPath, &m_bStop, error, bTransient);
                AudioProbe probe;
                if (!source || !AudioDecode::Probe(*source, probe, error, bTransient))
                {
                    delete pVoice;
                    if (!bTransient)
                    {
                        if (m_failCache.size() >= MaxFailures)
                            m_failCache.clear();
                        m_failCache[key] = error;
                    }
                    return nullptr;
                }
                pending = std::make_shared<AudioLoad>();
                pending->path = key;
                pending->source = source;
                pending->probe = probe;
                pending->bLoop = bLoop;
                pending->bWhole = (probe.format == AudioFormat::Wav) && (probe.frames * AudioDecode::OutChannels(probe.channels) * sizeof(float) <= m_wholeLimit);
                if (pending->bWhole)
                    m_pending[key] = pending;
                {
                    std::lock_guard<std::mutex> lock(m_workers->lock);
                    m_workers->loads.push_back(pending);
                }
                m_workers->loaderWake.notify_one();
            }
            pVoice->m_load = pending;
            pVoice->m_bLoading = true;
            if (pending->probe.format == AudioFormat::Wav)
            {
                // a WAV header tells rate and length, so positions and GetLength work while it loads
                pVoice->m_channels = AudioDecode::OutChannels(pending->probe.channels);
                pVoice->m_sampleRate = pending->probe.sampleRate;
                pVoice->m_frames = pending->probe.frames;
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        delete pVoice;
        error = "not enough memory for this sound file";
        return nullptr;
    }
    catch (const std::length_error &)
    {
        delete pVoice;
        error = "sound file too large";
        return nullptr;
    }
    pVoice->m_bLoop = bLoop;
    pVoice->m_bPaused = bStartPaused;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_voices.push_back(pVoice);
    }
    TrimCache();
    return pVoice;
}

void AudioEngine::Update(std::vector<std::string> &log, const bool bLoadErrors)
{
    // finished loads fill the caches; taken before m_mutex (lock order), and a load no voice uses ends here
    std::vector<std::shared_ptr<AudioLoad>> completed;
    {
        std::lock_guard<std::mutex> lock(m_workers->lock);
        completed.swap(m_workers->completed);
    }
    for (const std::shared_ptr<AudioLoad> &load : completed)
    {
        auto it = m_pending.find(load->path);
        if ((it != m_pending.end()) && (it->second.expired() || (it->second.lock() == load)))
            m_pending.erase(it);
        if (load->state == LoadFailed)
        {
            if (!load->bTransient)
            {
                if (m_failCache.size() >= MaxFailures)
                    m_failCache.clear();
                m_failCache[load->path] = load->error;
            }
            if (bLoadErrors)
                log.push_back("could not play (" + load->error + "): " + load->path);   // the reason first: XRSound.log cuts lines at 256 bytes
        }
        else if (load->buffer && (m_bufferCache.find(load->path) == m_bufferCache.end()))
        {
            m_bufferCache[load->path] = load->buffer;
            m_cacheBytes += load->buffer->samples.size() * sizeof(float);
        }
    }
    completed.clear();
    if (m_pending.size() > 256)
    {
        for (auto it = m_pending.begin(); it != m_pending.end(); )
            it = it->second.expired() ? m_pending.erase(it) : std::next(it);
    }

    std::vector<AudioVoice *> done;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&done](AudioVoice *pVoice)
        {
            pVoice->Adopt();
            const bool bDone = pVoice->m_bReleased && pVoice->m_bFinished;
            if (bDone)
                done.push_back(pVoice);
            return bDone;
        }), m_voices.end());

        // no output (no sink, lost, reconnecting): mix into nothing, so sounds still take their time and end
        const int64_t now = Now(), last = m_lastProcess;    // one read: Process may store a newer time meanwhile
        if (!m_bOffline && (now - last > SilentAfter))
        {
            const int64_t from = std::max<int64_t>(last, m_lastSilent);
            uint64_t frames = static_cast<uint64_t>(std::clamp<int64_t>(now - from, 0, Second)) * SampleRate / Second;
            while (frames > 0)
            {
                const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(frames, m_scratch.size() / Channels));
                std::fill(m_scratch.begin(), m_scratch.end(), 0.0f);
                for (AudioVoice *pVoice : m_voices)
                    pVoice->Mix(m_scratch.data(), n);
                frames -= n;
            }
            m_lastSilent = now;
        }
    }
    for (AudioVoice *pVoice : done)
        delete pVoice;      // outside the lock: freeing a decoder or a sound takes time
    TrimCache();
    if (!m_bOffline)
        Reconnect(log);
}

// drops cached sounds no voice plays until the cache fits its budget again (Orbiter thread only)
void AudioEngine::TrimCache()
{
    for (auto it = m_bufferCache.begin(); (it != m_bufferCache.end()) && (m_cacheBytes > CacheBudget); )
    {
        if (it->second.use_count() == 1)
        {
            m_cacheBytes -= it->second->samples.size() * sizeof(float);
            it = m_bufferCache.erase(it);
        }
        else
            ++it;
    }
}

void AudioEngine::Render(float *pOut, const uint32_t frames)
{
    // waits (10 s at most) until every load is done and every stream ring holds this period
    const int64_t deadline = Now() + 10 * Second;
    for (;;)
    {
        bool bReady = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (AudioVoice *pVoice : m_voices)
            {
                pVoice->Adopt();
                if (pVoice->m_bLoading)
                    bReady = false;
                else if (pVoice->m_feed && !pVoice->m_bFinished && !pVoice->m_bPaused)
                {
                    const double step = std::min(static_cast<double>(pVoice->m_speed) * pVoice->m_sampleRate / SampleRate, MaxStep);
                    const uint64_t need = static_cast<uint64_t>(pVoice->m_pos + step * frames) + static_cast<uint64_t>(Half * MaxScale) + 2;
                    AudioFeed &feed = *pVoice->m_feed;
                    std::lock_guard<std::mutex> feedLock(feed.lock);
                    const uint64_t room = RingFrames - (feed.head - feed.tail);
                    if (!feed.bBroken && ((feed.gen != feed.doneGen) || (!feed.bEnd && (feed.head < need) && (room >= MinRoom))))
                        bReady = false;
                }
            }
        }
        if (bReady || (Now() > deadline))
            break;
        WakeFeeder();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    Mix(pOut, frames);
}

void AudioEngine::Process()
{
    m_lastProcess = Now();
    m_periods++;
    pw_buffer *pBuffer = s_pw.pw_stream_dequeue_buffer(m_pw->pStream);
    if (!pBuffer)
        return;     // no free buffer this time
    spa_data &data = pBuffer->buffer->datas[0];
    float *pOut = static_cast<float *>(data.data);
    if (!pOut)
    {
        if (data.chunk)
            data.chunk->size = 0;
        s_pw.pw_stream_queue_buffer(m_pw->pStream, pBuffer);     // a dequeued buffer must go back, or the pool runs dry
        return;
    }

    const uint32_t stride = sizeof(float) * Channels;
    uint32_t frames = data.maxsize / stride;
    if ((pBuffer->requested > 0) && (pBuffer->requested < frames))
        frames = static_cast<uint32_t>(pBuffer->requested);
    Mix(pOut, frames);

    data.chunk->offset = 0;
    data.chunk->stride = stride;
    data.chunk->size = frames * stride;
    s_pw.pw_stream_queue_buffer(m_pw->pStream, pBuffer);
}

void AudioEngine::Mix(float *pOut, const uint32_t frames)
{
    std::fill(pOut, pOut + frames * Channels, 0.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (AudioVoice *pVoice : m_voices)
    {
        pVoice->Adopt();
        pVoice->Mix(pOut, frames);
    }
    for (uint32_t i = 0; i < frames * Channels; i++)
        pOut[i] = std::isfinite(pOut[i]) ? std::clamp(pOut[i], -1.0f, 1.0f) : 0.0f;    // many loud sounds at once must not wrap around; NaN/Inf never reach the device
}

void AudioEngine::WakeFeeder()
{
    m_workers->feederWake.notify_one();
}

// opens decoders and decodes whole sounds, one file at a time; never takes m_mutex
void AudioEngine::LoaderThread()
{
    pthread_setname_np(pthread_self(), "XRSound-load");
    for (;;)
    {
        std::shared_ptr<AudioLoad> load;
        {
            std::unique_lock<std::mutex> lock(m_workers->lock);
            m_workers->loaderWake.wait(lock, [this] { return m_bStop || !m_workers->loads.empty(); });
            if (m_bStop)
                return;
            load = std::move(m_workers->loads.front());
            m_workers->loads.pop_front();
        }
        if ((load.use_count() == 1) && !load->bWhole)
            continue;   // every voice of it is gone already (a whole load runs: a later Play may join it through m_pending)
        try
        {
            RunLoad(*load);
        }
        catch (const std::bad_alloc &)
        {
            load->error = "not enough memory for this sound file";
            load->bTransient = true;
            load->state = LoadFailed;
        }
        catch (const std::length_error &)
        {
            load->error = "sound file too large";
            load->state = LoadFailed;
        }
        load->source.reset();   // the file stays open only for a stream, which holds it itself
        std::lock_guard<std::mutex> lock(m_workers->lock);
        m_workers->completed.push_back(load);
    }
}

void AudioEngine::RunLoad(AudioLoad &load)
{
    std::unique_ptr<AudioStream> stream = AudioDecode::OpenStream(load.source, load.probe, load.error);
    if (!stream)
    {
        load.bTransient = m_bStop || load.source->ReadFailed();    // a read error may be gone next time: not remembered
        if (load.source->ReadFailed())
            load.error = "can't read the file";
        load.state = LoadFailed;
        return;
    }
    load.channels = AudioDecode::OutChannels(stream->channels);
    load.sampleRate = stream->sampleRate;
    load.frames = stream->frames;

    // whole when it fits the limit, whatever its header said (the decode stops at the limit); modules always stream
    const size_t limit = m_wholeLimit;
    const bool bWhole = load.bWhole || ((load.probe.format != AudioFormat::Module) && (stream->frames > 0) && (stream->frames * load.channels * sizeof(float) <= limit));
    if (bWhole)
    {
        bool bTooLong = false;
        load.buffer = AudioDecode::Decode(*stream, limit, load.error, bTooLong);
        if (load.source->ReadFailed())
        {
            load.buffer.reset();    // cut short by a read error: never cached as the whole sound
            load.error = "can't read the file";
            bTooLong = false;
        }
        if (load.buffer)
        {
            load.frames = load.buffer->frames;
            load.state = LoadDone;
            return;
        }
        if (!bTooLong || !stream->Seek(0))
        {
            load.bTransient = m_bStop || load.source->ReadFailed();
            load.state = LoadFailed;
            return;
        }
        load.error.clear();     // longer than its header said: it streams
        load.frames = 0;
    }
    load.feed = std::make_shared<AudioFeed>(std::move(stream), load.bLoop);
    {
        std::lock_guard<std::mutex> lock(m_workers->lock);
        m_workers->feeds.push_back(load.feed);
    }
    WakeFeeder();
    load.state = LoadDone;
}

// keeps every stream ring full, the one with the least decoded first; never takes m_mutex
void AudioEngine::FeederThread()
{
    pthread_setname_np(pthread_self(), "XRSound-feed");
    std::vector<std::shared_ptr<AudioFeed>> live;
    while (!m_bStop)
    {
        {
            std::lock_guard<std::mutex> lock(m_workers->lock);
            std::vector<std::weak_ptr<AudioFeed>> &feeds = m_workers->feeds;
            feeds.erase(std::remove_if(feeds.begin(), feeds.end(), [](const std::weak_ptr<AudioFeed> &feed) { return feed.expired(); }), feeds.end());
            for (const std::weak_ptr<AudioFeed> &feed : feeds)
            {
                std::shared_ptr<AudioFeed> p = feed.lock();
                if (p)
                    live.push_back(p);
            }
        }
        AudioFeed *pBest = nullptr;
        uint64_t best = UINT64_MAX;
        for (const std::shared_ptr<AudioFeed> &feed : live)
        {
            std::lock_guard<std::mutex> lock(feed->lock);
            uint64_t ahead = feed->head - feed->tail;
            if (feed->bBroken)
                continue;
            if (feed->gen != feed->doneGen)
                ahead = 0;      // a seek first
            else if (feed->bEnd)
            {
                if (!feed->bLoop)
                    continue;
                ahead = 0;      // looping switched on after the end
            }
            else if (RingFrames - ahead < MinRoom)
                continue;
            if (ahead < best)
            {
                best = ahead;
                pBest = feed.get();
            }
        }
        if (pBest)
            TopUp(*pBest);
        const bool bNone = live.empty();
        live.clear();   // a feed whose voice is gone ends here, on this thread
        if (!pBest)
        {
            std::unique_lock<std::mutex> lock(m_workers->lock);
            if (bNone)
                m_workers->feederWake.wait(lock, [this] { return m_bStop || !m_workers->feeds.empty(); });    // no streams: sleep until RunLoad adds one
            else if (!m_bStop)
                m_workers->feederWake.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}

// one step for a feed: a pending seek, a restart at the end of a loop, or one chunk of frames
bool AudioEngine::TopUp(AudioFeed &f)
{
    uint32_t gen;
    uint64_t seekTo, head, room;
    bool bSeek, bLoop, bEnd;
    {
        std::lock_guard<std::mutex> lock(f.lock);
        gen = f.gen;
        bSeek = (f.gen != f.doneGen);
        seekTo = f.seekTo;
        head = f.head;
        room = RingFrames - (f.head - f.tail);
        bLoop = f.bLoop;
        bEnd = f.bEnd;
    }
    if (bSeek)
    {
        const bool bOk = f.stream->Seek(seekTo);
        const bool bRestart = !bOk && bLoop && f.stream->Seek(0);   // past the end of a loop: from the start (the length stays unknown)
        std::lock_guard<std::mutex> lock(f.lock);
        if (f.gen == gen)
        {
            f.doneGen = gen;
            f.srcPos = f.srcStart = bRestart ? 0 : seekTo;
            if (bRestart)
                f.seams.push_back(f.head);
            else if (!bOk)
            {
                f.bEnd = true;
                f.endFrame = f.head;
            }
        }
        return true;
    }
    if (bEnd)
    {
        const bool bOk = f.stream->Seek(0);
        std::lock_guard<std::mutex> lock(f.lock);
        if (f.gen == gen)
        {
            if (bOk)
            {
                f.bEnd = false;
                f.seams.push_back(f.head);
                f.srcPos = f.srcStart = 0;
            }
            else
                f.bBroken = true;
        }
        return true;
    }

    const uint64_t want = std::min(room, FeedChunk);
    std::vector<float> &in = m_workers->in;
    std::vector<float> &out = m_workers->out;
    in.resize(want * f.inChannels);
    out.resize(want * f.outChannels);
    const uint64_t n = f.stream->Read(in.data(), want);
    if (n == 0)
    {
        // the end: a sequential run gives the real length (a stream shorter than its header, or one of unknown length)
        const bool bRan = (f.srcPos > f.srcStart);
        const bool bRestart = bLoop && (bRan || (f.srcStart > 0)) && f.stream->Seek(0);
        std::lock_guard<std::mutex> lock(f.lock);
        if (f.gen == gen)
        {
            if (bRan)
                f.length = f.srcPos;
            if (bRestart)
            {
                f.seams.push_back(f.head);
                f.srcPos = f.srcStart = 0;
            }
            else
            {
                f.bEnd = true;
                f.endFrame = f.head;
                if (bLoop && !bRan && (f.srcStart == 0))
                    f.bBroken = true;   // a pass from the start gave nothing: restarting it would spin forever
            }
        }
        return true;
    }
    AudioDecode::Fold(in.data(), f.inChannels, n, out.data());
    // the frames go beyond head, where the mixer does not read; published below if no seek came in between
    const uint64_t at = head & (RingFrames - 1);
    const uint64_t first = std::min(n, RingFrames - at);
    memcpy(f.ring.data() + at * f.outChannels, out.data(), first * f.outChannels * sizeof(float));
    if (n > first)
        memcpy(f.ring.data(), out.data() + first * f.outChannels, (n - first) * f.outChannels * sizeof(float));
    std::lock_guard<std::mutex> lock(f.lock);
    if (f.gen == gen)
    {
        f.head += n;
        f.srcPos += n;
    }
    return true;
}

AudioVoice::AudioVoice(AudioEngine *pEngine) :
    m_pEngine(pEngine), m_channels(0), m_sampleRate(0), m_frames(0), m_pos(0), m_srcOrigin(0), m_passStart(0), m_ringHead(0), m_ringTail(0), m_pendingMillis(-1),
    m_volume(1.0f), m_pan(0), m_speed(1.0f), m_bLoading(false), m_bLoop(false), m_bPaused(false), m_bFinished(false), m_bReleased(false),
    m_bWrapped(false), m_bGainSet(false), m_gainL(0), m_gainR(0)
{
}

AudioVoice::~AudioVoice()
{
}

bool AudioVoice::IsFinished() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_bFinished;
}

void AudioVoice::Stop()
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_bFinished = true;
}

void AudioVoice::Release()
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_bReleased = true;
}

void AudioVoice::SetPaused(const bool bPaused)
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_bPaused = bPaused;
}

bool AudioVoice::IsPaused() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_bPaused;
}

void AudioVoice::SetVolume(const float volume)
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
}

float AudioVoice::GetVolume() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_volume;
}

void AudioVoice::SetLooped(const bool bLoop)
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_bLoop = bLoop;
    if (m_feed)
    {
        {
            std::lock_guard<std::mutex> feedLock(m_feed->lock);
            m_feed->bLoop = bLoop;
        }
        m_pEngine->WakeFeeder();
    }
}

bool AudioVoice::IsLooped() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_bLoop;
}

void AudioVoice::SetPan(const float pan)
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
}

float AudioVoice::GetPan() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_pan;
}

bool AudioVoice::SetPlaybackSpeed(const float speed)
{
    if (!(speed > 0) || !std::isfinite(speed))
        return false;
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    m_speed = speed;
    return true;
}

float AudioVoice::GetPlaybackSpeed() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return m_speed;
}

bool AudioVoice::SetPlayPosition(const uint32_t positionMillis)
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    const double frame = static_cast<double>(positionMillis) * m_sampleRate / 1000.0;
    if ((m_frames > 0) && (frame > static_cast<double>(m_frames)))
        return false;
    if (m_bLoading)
        m_pendingMillis = positionMillis;   // applied once the sound is loaded
    else if (m_feed)
        SeekFeed(static_cast<uint64_t>(frame));
    else
    {
        m_pos = frame;
        m_bWrapped = false;
    }
    return true;
}

int AudioVoice::GetPlayPosition() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    if (m_bLoading && (m_pendingMillis >= 0))
        return static_cast<int>(m_pendingMillis);
    if (m_bLoading || (m_sampleRate == 0))
        return 0;
    return static_cast<int>(std::min(static_cast<double>(SourcePosition()) * 1000.0 / m_sampleRate, static_cast<double>(INT_MAX)));
}

int AudioVoice::GetLength() const
{
    std::lock_guard<std::mutex> lock(m_pEngine->m_mutex);
    return ((m_frames > 0) && (m_sampleRate > 0)) ? static_cast<int>(std::min(static_cast<double>(m_frames) * 1000.0 / m_sampleRate, static_cast<double>(INT_MAX))) : -1;   // a header can claim hours at 1 Hz
}

uint64_t AudioVoice::SourcePosition() const
{
    if (m_feed)
        return m_srcOrigin + static_cast<uint64_t>(m_pos - static_cast<double>(m_passStart));
    return static_cast<uint64_t>(m_pos);
}

// a stream starts again at a source frame: the ring is emptied and the feeder seeks
void AudioVoice::SeekFeed(const uint64_t frame)
{
    m_pos = 0;
    m_passStart = 0;
    m_srcOrigin = frame;
    {
        std::lock_guard<std::mutex> lock(m_feed->lock);
        m_feed->gen++;
        m_feed->seekTo = frame;
        m_feed->head = 0;
        m_feed->tail = 0;
        m_feed->seams.clear();
        m_feed->bEnd = false;
        m_feed->endFrame = 0;
    }
    m_pEngine->WakeFeeder();
}

void AudioVoice::Adopt()
{
    if (!m_bLoading)
        return;
    const int state = m_load->state.load(std::memory_order_acquire);
    if (state == LoadPending)
        return;
    m_bLoading = false;
    if (state == LoadFailed)
    {
        m_bFinished = true;     // the caller sees it end at once; Update logs why and remembers the file
        return;
    }
    m_channels = m_load->channels;
    m_sampleRate = m_load->sampleRate;
    if (m_load->buffer)
    {
        m_buffer = m_load->buffer;      // shared with the cache and other voices: copied, never moved
        m_frames = m_buffer->frames;
    }
    else
    {
        m_feed = m_load->feed;
        m_frames = m_load->frames;
        std::lock_guard<std::mutex> lock(m_feed->lock);
        m_feed->bLoop = m_bLoop;
    }
    if (m_pendingMillis >= 0)
    {
        double frame = static_cast<double>(m_pendingMillis) * m_sampleRate / 1000.0;
        m_pendingMillis = -1;
        if ((m_frames > 0) && (frame > static_cast<double>(m_frames)))
        {
            if (!m_bLoop)
            {
                m_bFinished = true;
                return;
            }
            frame = std::fmod(frame, static_cast<double>(m_frames));
        }
        if (m_feed)
            SeekFeed(static_cast<uint64_t>(frame));
        else
            m_pos = frame;
    }
}

// one source frame for the filter: 0 outside the sound, the other pass of a loop across its seam
void AudioVoice::Frame(const int64_t index, float &left, float &right) const
{
    const float *p;
    if (m_buffer)
    {
        const int64_t n = static_cast<int64_t>(m_frames);
        int64_t i = index;
        if (i >= n)
        {
            if (!m_bLoop)
            {
                left = right = 0;
                return;
            }
            i %= n;
        }
        else if (i < 0)
        {
            if (!m_bLoop || !m_bWrapped)
            {
                left = right = 0;
                return;
            }
            i = n - 1 - ((-i - 1) % n);
        }
        p = m_buffer->samples.data() + i * m_channels;
    }
    else
    {
        const AudioFeed &f = *m_feed;   // head and tail were read under its lock at the start of this period
        if ((index < 0) || (static_cast<uint64_t>(index) < m_ringTail) || (static_cast<uint64_t>(index) >= m_ringHead))
        {
            left = right = 0;
            return;
        }
        p = f.ring.data() + (static_cast<uint64_t>(index) & (RingFrames - 1)) * m_channels;
    }
    left = p[0];
    right = (m_channels > 1) ? p[1] : p[0];     // mono plays on both sides
}

void AudioVoice::Mix(float *pOut, const uint32_t frames)
{
    if (m_bLoading || m_bFinished || m_bPaused || (frames == 0))
        return;

    // a stream: what the feeder has decoded, where its passes begin and where it ends
    uint64_t seams[8];
    size_t seamCount = 0, crossed = 0;
    bool bEnd = false;
    uint64_t endFrame = 0;
    if (m_feed)
    {
        std::lock_guard<std::mutex> lock(m_feed->lock);
        if ((m_feed->gen != m_feed->doneGen) && !m_feed->bBroken)
            return;     // a seek is on its way (a broken feed takes none: it ends)
        m_ringHead = m_feed->head;
        m_ringTail = m_feed->tail;
        bEnd = m_feed->bEnd || m_feed->bBroken;
        endFrame = m_feed->bEnd ? m_feed->endFrame : m_feed->head;
        for (uint64_t seam : m_feed->seams)
        {
            if (seamCount == 8)
                break;
            seams[seamCount++] = seam;
        }
        if (m_feed->length > 0)
            m_frames = m_feed->length;  // the real length, learned at the end
    }

    // balance pan: the centre plays both sides at full volume, the far side fades out towards the ends
    const float targetL = m_volume * ((m_pan > 0) ? (1.0f - m_pan) : 1.0f);
    const float targetR = m_volume * ((m_pan < 0) ? (1.0f + m_pan) : 1.0f);
    if (!m_bGainSet)
    {
        m_gainL = targetL;
        m_gainR = targetR;
        m_bGainSet = true;
    }
    const float rampL = (targetL - m_gainL) / frames;
    const float rampR = (targetR - m_gainR) / frames;
    float gainL = m_gainL;
    float gainR = m_gainR;

    // source frames per output frame: sample rate conversion and playback speed in one step
    const double step = std::min(static_cast<double>(m_speed) * m_sampleRate / AudioEngine::SampleRate, MaxStep);
    const bool bCopy = (step == 1.0) && (m_pos == std::floor(m_pos));   // same rate at a whole position: the samples as they are
    const double scale = std::clamp(step, 1.0, MaxScale);
    const int span = bCopy ? 0 : ((scale > 1.0) ? static_cast<int>(std::ceil(Half * scale)) : Half);
    float gather[2 * Taps];
    for (uint32_t i = 0; i < frames; i++)
    {
        if (m_feed)
        {
            // a loop pass ends at a seam: the next one starts at source frame 0, or the sound ends
            while ((crossed < seamCount) && (m_pos >= static_cast<double>(seams[crossed])))
            {
                if (!m_bLoop)
                {
                    m_bFinished = true;
                    break;
                }
                m_srcOrigin = 0;
                m_passStart = seams[crossed++];
            }
            if (m_bFinished)
                break;
            if (bEnd && (m_pos >= static_cast<double>(endFrame)))
            {
                m_bFinished = true;
                break;
            }
            if (!bEnd && (static_cast<uint64_t>(m_pos) + span >= m_ringHead))
                break;  // not decoded yet: silence, and the position waits (never a skip)
        }
        else if (m_pos >= static_cast<double>(m_frames))
        {
            if (!m_bLoop)
            {
                m_bFinished = true;
                break;
            }
            m_pos = std::fmod(m_pos, static_cast<double>(m_frames));
            m_bWrapped = true;
        }

        const int64_t i0 = static_cast<int64_t>(m_pos);
        float left, right;
        if (bCopy)
            Frame(i0, left, right);
        else if (scale == 1.0)
        {
            // 16 taps around the position, their weights interpolated between two of the 256 phases
            const float phase = static_cast<float>(m_pos - static_cast<double>(i0)) * Phases;
            const int p0 = std::min(static_cast<int>(phase), Phases - 1);
            const float fraction = phase - static_cast<float>(p0);
            const float *c0 = s_kernel.taps + p0 * Taps;
            const float *c1 = c0 + Taps;
            const int64_t first = i0 - Half + 1;
            const float *p = nullptr;
            if (m_buffer && (first >= 0) && (first + Taps <= static_cast<int64_t>(m_frames)))
                p = m_buffer->samples.data() + first * m_channels;
            else if (m_feed && (first >= 0) && (static_cast<uint64_t>(first) >= m_ringTail) && (static_cast<uint64_t>(first) + Taps <= m_ringHead) &&
                ((static_cast<uint64_t>(first) & (RingFrames - 1)) + Taps <= RingFrames))
                p = m_feed->ring.data() + (static_cast<uint64_t>(first) & (RingFrames - 1)) * m_channels;
            else
            {
                for (int k = 0; k < Taps; k++)
                {
                    float l, r;
                    Frame(first + k, l, r);
                    gather[k * m_channels] = l;
                    if (m_channels > 1)
                        gather[k * m_channels + 1] = r;
                }
                p = gather;
            }
            if (m_channels == 1)
            {
                float sum = 0;
                for (int k = 0; k < Taps; k++)
                    sum += (c0[k] + (c1[k] - c0[k]) * fraction) * p[k];
                left = right = sum;
            }
            else
            {
                float sumL = 0, sumR = 0;
                for (int k = 0; k < Taps; k++)
                {
                    const float c = c0[k] + (c1[k] - c0[k]) * fraction;
                    sumL += c * p[2 * k];
                    sumR += c * p[2 * k + 1];
                }
                left = sumL;
                right = sumR;
            }
        }
        else
        {
            // faster than the output: the filter's cutoff and width follow the step (up to MaxScale), so nothing folds back
            double sumL = 0, sumR = 0, sumC = 0;
            for (int64_t j = i0 - span + 1; j <= i0 + span; j++)
            {
                const double t = (static_cast<double>(j) - m_pos) / scale;
                if ((t <= -Half) || (t >= Half))
                    continue;
                double k = std::floor(t) + Half;
                double phase = (k - Half + 1 - t) * Phases;
                if (phase >= Phases)
                {
                    phase -= Phases;
                    k -= 1;
                }
                if ((k < 0) || (k >= Taps))
                    continue;
                const int p0 = static_cast<int>(phase);
                const float *row = s_kernel.taps + p0 * Taps + static_cast<int>(k);
                const double c = row[0] + (row[Taps] - row[0]) * (phase - p0);
                float l, r;
                Frame(j, l, r);
                sumL += c * l;
                sumR += c * r;
                sumC += c;
            }
            left = (sumC != 0) ? static_cast<float>(sumL / sumC) : 0.0f;
            right = (sumC != 0) ? static_cast<float>(sumR / sumC) : 0.0f;
        }

        gainL += rampL;
        gainR += rampR;
        pOut[2 * i] += left * gainL;
        pOut[2 * i + 1] += right * gainR;
        m_pos += step;
    }
    m_gainL = targetL;
    m_gainR = targetR;

    if (m_feed)
    {
        // what the filter no longer needs goes back to the feeder
        std::lock_guard<std::mutex> lock(m_feed->lock);
        const uint64_t keep = (static_cast<uint64_t>(m_pos) > History) ? static_cast<uint64_t>(m_pos) - History : 0;
        m_feed->tail = std::clamp(keep, m_feed->tail, m_feed->head);
        for (size_t k = 0; (k < crossed) && !m_feed->seams.empty(); k++)
            m_feed->seams.pop_front();
    }
}
