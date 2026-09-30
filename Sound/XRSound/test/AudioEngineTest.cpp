// not upstream: XRSound mixer tests, all silent: an offline engine renders into memory and PipeWire is kept away; --daemon runs a private daemon without devices (DaemonTest.sh)

#include "Audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <random>
#include <spawn.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace
{
typedef std::chrono::steady_clock Clock;

int s_failures = 0;
std::string s_tmp;      // this run's files

void Check(const bool bOk, const std::string &what)
{
    printf("%s: %s\n", bOk ? "ok  " : "FAIL", what.c_str());
    fflush(stdout);
    if (!bOk)
        s_failures++;
}

std::string Format(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char *pFormat, ...)
{
    char text[512];
    va_list args;
    va_start(args, pFormat);
    vsnprintf(text, sizeof(text), pFormat, args);
    va_end(args);
    return text;
}

double Elapsed(const Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string Temp(const std::string &name)
{
    return s_tmp + "/" + name;
}

void WriteBytes(const std::string &path, const std::vector<uint8_t> &bytes)
{
    FILE *pFile = fopen(path.c_str(), "wb");
    if (pFile)
    {
        fwrite(bytes.data(), 1, bytes.size(), pFile);
        fclose(pFile);
    }
}

std::vector<uint8_t> ReadBytes(const std::string &path, const size_t maxBytes = SIZE_MAX)
{
    std::vector<uint8_t> bytes;
    FILE *pFile = fopen(path.c_str(), "rb");
    if (!pFile)
        return bytes;
    uint8_t buffer[65536];
    size_t n;
    while ((bytes.size() < maxBytes) && ((n = fread(buffer, 1, std::min(sizeof(buffer), maxBytes - bytes.size()), pFile)) > 0))
        bytes.insert(bytes.end(), buffer, buffer + n);
    fclose(pFile);
    return bytes;
}

void Put16(std::vector<uint8_t> &b, const uint32_t v)
{
    b.push_back(v & 0xFF);
    b.push_back((v >> 8) & 0xFF);
}

void Put32(std::vector<uint8_t> &b, const uint32_t v)
{
    Put16(b, v & 0xFFFF);
    Put16(b, v >> 16);
}

void PutText(std::vector<uint8_t> &b, const char *p)
{
    b.insert(b.end(), p, p + strlen(p));
}

// a WAV file of 8, 16, 24-bit integer or 32-bit float samples; the overrides make broken headers
std::vector<uint8_t> MakeWav(const uint32_t rate, const uint16_t channels, const uint16_t bits, const std::vector<float> &samples,
    const uint16_t formatTag = 0, const uint16_t headerChannels = 0, const uint32_t dataSize = 0)
{
    std::vector<uint8_t> data;
    for (float s : samples)
    {
        switch (bits)
        {
        case 8: data.push_back(static_cast<uint8_t>(lround(s * 127) + 128)); break;
        case 16: Put16(data, static_cast<uint16_t>(static_cast<int16_t>(lround(s * 32767)))); break;
        case 24:
        {
            const int32_t v = static_cast<int32_t>(lround(s * 8388607));
            data.push_back(v & 0xFF);
            data.push_back((v >> 8) & 0xFF);
            data.push_back((v >> 16) & 0xFF);
            break;
        }
        default:
        {
            uint32_t u;
            memcpy(&u, &s, 4);
            Put32(data, u);
            break;
        }
        }
    }
    const uint16_t tag = formatTag ? formatTag : ((bits == 32) ? 3 : 1);
    const uint16_t hc = headerChannels ? headerChannels : channels;
    const uint16_t bytes = bits / 8;
    std::vector<uint8_t> w;
    PutText(w, "RIFF");
    Put32(w, static_cast<uint32_t>(36 + data.size()));
    PutText(w, "WAVEfmt ");
    Put32(w, 16);
    Put16(w, tag);
    Put16(w, hc);
    Put32(w, rate);
    Put32(w, rate * hc * bytes);
    Put16(w, static_cast<uint16_t>(hc * bytes));
    Put16(w, bits);
    PutText(w, "data");
    Put32(w, dataSize ? dataSize : static_cast<uint32_t>(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    return w;
}

std::vector<float> Tone(const double freq, const double rate, const uint64_t frames, const float amp)
{
    std::vector<float> v(frames);
    for (uint64_t i = 0; i < frames; i++)
        v[i] = amp * static_cast<float>(sin(2 * M_PI * freq * static_cast<double>(i) / rate));
    return v;
}

std::vector<float> Ramp(const uint64_t frames)
{
    std::vector<float> v(frames);
    for (uint64_t i = 0; i < frames; i++)
        v[i] = -0.5f + static_cast<float>(i % 997) / 997.0f;    // distinct values that don't repeat with the length
    return v;
}

std::string MakeFile(const std::string &name, const std::vector<uint8_t> &bytes)
{
    const std::string path = Temp(name);
    WriteBytes(path, bytes);
    return path;
}

// the offline engine pulled period by period, as PipeWire would
std::vector<float> Render(AudioEngine &engine, const uint64_t frames)
{
    std::vector<float> out(frames * 2);
    for (uint64_t done = 0; done < frames; )
    {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(1024, frames - done));
        engine.Render(out.data() + 2 * done, n);
        done += n;
    }
    return out;
}

void Update(AudioEngine &engine, std::vector<std::string> *pLog = nullptr)
{
    std::vector<std::string> log;
    engine.Update(log, true);
    for (const std::string &line : log)
    {
        printf("      log: %s\n", line.c_str());
        if (pLog)
            pLog->push_back(line);
    }
}

// Goertzel amplitude of one output channel over a Hann window
double Level(const std::vector<float> &stereo, const uint64_t first, const uint64_t count, const double freq)
{
    double s1 = 0, s2 = 0, wsum = 0;
    const double c = 2 * cos(2 * M_PI * freq / AudioEngine::SampleRate);
    for (uint64_t i = 0; i < count; i++)
    {
        const double w = 0.5 - 0.5 * cos(2 * M_PI * static_cast<double>(i) / static_cast<double>(count - 1));
        wsum += w;
        const double s = stereo[2 * (first + i)] * w + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) * 2 / wsum;
}

double Db(const double level, const double reference)
{
    return 20 * log10(std::max(level, 1e-12) / reference);
}

std::shared_ptr<AudioBuffer> DecodeFile(const std::string &path, std::string &error)
{
    bool bTransient = false;
    std::shared_ptr<AudioSource> source = AudioSource::Open(path.c_str(), nullptr, error, bTransient);
    AudioProbe probe;
    if (!source || !AudioDecode::Probe(*source, probe, error, bTransient))
        return nullptr;
    std::unique_ptr<AudioStream> stream = AudioDecode::OpenStream(source, probe, error);
    if (!stream)
        return nullptr;
    bool bTooLong = false;
    return AudioDecode::Decode(*stream, size_t(1) << 30, error, bTooLong);
}

std::unique_ptr<AudioStream> OpenFile(const std::string &path, AudioProbe &probe, std::string &error)
{
    bool bTransient = false;
    std::shared_ptr<AudioSource> source = AudioSource::Open(path.c_str(), nullptr, error, bTransient);
    if (!source || !AudioDecode::Probe(*source, probe, error, bTransient))
        return nullptr;
    return AudioDecode::OpenStream(source, probe, error);
}

bool AllFinite(const std::vector<float> &v)
{
    for (float s : v)
        if (!std::isfinite(s) || (fabs(s) > 1.0f))
            return false;
    return true;
}

const std::string s_assets = XRSOUND_TEST_SOUNDS;
const std::string s_data = XRSOUND_TEST_DATA;

void TestDecoders()
{
    std::string error;
    const std::string wav = s_assets + "/Welcome Aboard All Systems Nominal.wav";
    std::shared_ptr<AudioBuffer> buffer = DecodeFile(wav, error);
    Check(buffer != nullptr, "decode WAV " + error);
    if (buffer)
    {
        float peak = 0;
        for (float s : buffer->samples)
            peak = std::max(peak, std::fabs(s));
        Check((buffer->frames > 0) && (buffer->samples.size() == buffer->frames * buffer->channels) && (peak > 0.01f) && (peak <= 1.0f),
            Format("WAV %u ch %u Hz %llu frames, samples in -1..1 and not silent", buffer->channels, buffer->sampleRate, static_cast<unsigned long long>(buffer->frames)));
        AudioProbe probe;
        std::unique_ptr<AudioStream> stream = OpenFile(wav, probe, error);
        std::vector<float> all;
        std::vector<float> chunk(1000 * 8), folded(1000 * 2);
        uint64_t n;
        while (stream && ((n = stream->Read(chunk.data(), 1000)) > 0))
        {
            AudioDecode::Fold(chunk.data(), stream->channels, n, folded.data());
            all.insert(all.end(), folded.begin(), folded.begin() + n * AudioDecode::OutChannels(stream->channels));
        }
        Check(all == buffer->samples, "WAV stream equals whole decode");
    }

    AudioProbe probe;
    std::unique_ptr<AudioStream> ogg = OpenFile(s_assets + "/Music/Solar Serenity.ogg", probe, error);
    Check(ogg != nullptr, "open OGG stream " + error);
    if (ogg)
    {
        std::vector<float> second(ogg->sampleRate * ogg->channels);
        Check(ogg->Read(second.data(), ogg->sampleRate) == ogg->sampleRate, Format("OGG %u ch %u Hz %.1f s: read 1 s", ogg->channels, ogg->sampleRate, static_cast<double>(ogg->frames) / ogg->sampleRate));
        Check(ogg->Seek(ogg->frames / 2) && (ogg->Read(second.data(), 100) == 100), "OGG seek to the middle");
    }

    // the committed test files (ffmpeg, 1 s at 48 kHz): lengths and formats
    struct { const char *pName; AudioFormat format; uint64_t minFrames, maxFrames; } files[] = {
        { "sine.flac", AudioFormat::Flac, 48000, 48000 },
        { "sine_lame.mp3", AudioFormat::Mp3, 48000, 48000 },            // the LAME tag gives the exact length
        { "sine.mp3", AudioFormat::Mp3, 48000, 48000 + 4 * 1152 } };    // no tag: whole frames, encoder delay included
    for (const auto &file : files)
    {
        std::unique_ptr<AudioStream> stream = OpenFile(s_data + "/" + file.pName, probe, error);
        Check(stream && (probe.format == file.format) && (stream->frames >= file.minFrames) && (stream->frames <= file.maxFrames),
            Format("%s: format and length (%llu frames) %s", file.pName, stream ? static_cast<unsigned long long>(stream->frames) : 0ULL, error.c_str()));
    }
}

// a decoder's seek lands where a read from the start gets to (the seek table of dr_mp3 counts the encoder delay)
void TestSeekAccuracy()
{
    const std::string wav = MakeFile("seek48.wav", MakeWav(48000, 1, 32, Ramp(96000)));
    struct { std::string path; float tolerance; } files[] = {
        { wav, 0 }, { s_data + "/sine.flac", 0 }, { s_data + "/sine_lame.mp3", 1e-3f }, { s_data + "/sine.mp3", 1e-3f },
        { s_assets + "/Music/Solar Serenity.ogg", 1e-3f } };
    for (const auto &file : files)
    {
        std::string error;
        AudioProbe probe;
        const uint64_t first = 20000, count = 1000;
        std::unique_ptr<AudioStream> a = OpenFile(file.path, probe, error);
        std::unique_ptr<AudioStream> b = OpenFile(file.path, probe, error);
        if (!a || !b)
        {
            Check(false, "open " + file.path + " " + error);
            continue;
        }
        std::vector<float> seq((first + count) * a->channels), sought(count * b->channels);
        uint64_t got = 0, n;
        while ((got < first + count) && ((n = a->Read(seq.data() + got * a->channels, first + count - got)) > 0))
            got += n;
        const bool bSeek = b->Seek(first);
        const uint64_t gotB = b->Read(sought.data(), count);
        float diff = (got == first + count) && bSeek && (gotB == count) ? 0.0f : 1e9f;
        for (uint64_t i = 0; (i < count * a->channels) && (diff < 1e9f); i++)
            diff = std::max(diff, std::fabs(seq[first * a->channels + i] - sought[i]));
        Check(diff <= file.tolerance, Format("seek to frame %llu of %s: largest difference %g", static_cast<unsigned long long>(first), file.path.substr(file.path.rfind('/') + 1).c_str(), diff));
    }
}

void TestLoop()
{
    // 0.1 s at 48 kHz in float: the copy path gives the samples back exactly, buffer and stream alike
    const std::vector<float> ramp = Ramp(4800);
    const std::string path = MakeFile("ramp48.wav", MakeWav(48000, 1, 32, ramp));
    std::vector<float> outputs[2];
    for (int bStream = 0; bStream < 2; bStream++)
    {
        const char *pKind = bStream ? "stream" : "buffer";
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(bStream ? 0 : AudioEngine::WholeLimit);
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), true, false, error);
        Check(pVoice != nullptr, Format("loop %s: play %s", pKind, error.c_str()));
        if (!pVoice)
            continue;
        std::vector<float> out = Render(*engine, 48000);
        bool bExact = true;
        for (uint64_t i = 0; (i < 48000) && bExact; i++)
            bExact = (out[2 * i] == ramp[i % 4800]) && (out[2 * i + 1] == ramp[i % 4800]);
        Check(bExact, Format("loop %s: 10 passes of a 0.1 s WAV, every sample exact", pKind));
        const int pos = pVoice->GetPlayPosition();
        Check(!pVoice->IsFinished() && ((pos == 0) || (pos == 100)), Format("loop %s: still playing, at the end of the 10th pass (%d ms)", pKind, pos));
        Render(*engine, 2400);
        pVoice->SetLooped(false);
        Render(*engine, 2000);
        const bool bPlaying = !pVoice->IsFinished();
        Render(*engine, 800);
        Check(bPlaying && pVoice->IsFinished(), Format("loop %s: loop switched off mid-pass ends at the end of that pass", pKind));
        pVoice->Release();
        Update(*engine);
    }

    // 22.05 kHz: resampled, and the stream's next pass in its ring gives what the buffer's wrap gives
    const std::string path22 = MakeFile("ramp22.wav", MakeWav(22050, 1, 32, Ramp(2205)));
    for (int bStream = 0; bStream < 2; bStream++)
    {
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(bStream ? 0 : AudioEngine::WholeLimit);
        std::string error;
        AudioVoice *pVoice = engine->Play(path22.c_str(), true, false, error);
        if (pVoice)
            outputs[bStream] = Render(*engine, 48000);
    }
    float diff = (outputs[0].size() == outputs[1].size()) && !outputs[0].empty() ? 0.0f : 1e9f;
    for (size_t i = 0; (i < outputs[0].size()) && (diff < 1e9f); i++)
        diff = std::max(diff, std::fabs(outputs[0][i] - outputs[1][i]));
    Check(diff <= 1e-5f, Format("loop at 22.05 kHz: stream equals buffer (largest difference %g)", diff));
}

void TestSeek()
{
    const std::vector<float> ramp = Ramp(96000);
    const std::string path = MakeFile("ramp48s.wav", MakeWav(48000, 1, 32, ramp));
    for (int bStream = 0; bStream < 2; bStream++)
    {
        const char *pKind = bStream ? "stream" : "buffer";
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(bStream ? 0 : AudioEngine::WholeLimit);
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), false, true, error);
        if (!pVoice)
        {
            Check(false, "seek: play " + error);
            continue;
        }
        Check((pVoice->GetLength() == 2000) && pVoice->SetPlayPosition(1000), Format("seek %s: a WAV's length is known while it loads", pKind));
        Render(*engine, 16);
        Check(pVoice->SetPlayPosition(500) && (pVoice->GetPlayPosition() == 500), Format("seek %s: position 500 ms", pKind));
        pVoice->SetPaused(false);
        std::vector<float> out = Render(*engine, 64);
        Check((out[0] == ramp[24000]) && (out[126] == ramp[24063]), Format("seek %s: the samples at 500 ms", pKind));
        Check(!pVoice->SetPlayPosition(2001), Format("seek %s: past the end is refused", pKind));
        pVoice->Release();
    }

    // compressed files: FLAC decoded whole; MP3 streamed through its seek table (LAME delay) and with no Xing header
    struct { const char *pName; size_t limit; float tolerance; } files[] = {
        { "sine.flac", AudioEngine::WholeLimit, 1e-6f }, { "sine_lame.mp3", 0, 1e-3f }, { "sine.mp3", 0, 1e-3f } };
    for (const auto &file : files)
    {
        const std::string name = s_data + "/" + file.pName;
        std::string error;
        std::shared_ptr<AudioBuffer> whole = DecodeFile(name, error);
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(file.limit);
        AudioVoice *pVoice = engine->Play(name.c_str(), false, false, error);
        if (!whole || !pVoice)
        {
            Check(false, std::string("seek: ") + file.pName + " " + error);
            continue;
        }
        Check(pVoice->SetPlayPosition(500), std::string("seek ") + file.pName + ": position set while loading");
        std::vector<float> out = Render(*engine, 1000);
        float diff = 0;
        for (int i = 0; i < 1000; i++)
            diff = std::max(diff, std::fabs(out[2 * i] - whole->samples[24000 + i]));
        const int length = pVoice->GetLength();
        Check(diff <= file.tolerance, Format("seek %s: the samples at 500 ms (largest difference %g)", file.pName, diff));
        Check((length >= 1000) && (length <= 1100) && !pVoice->SetPlayPosition(static_cast<uint32_t>(length) + 50), Format("seek %s: length %d ms, past the end refused", file.pName, length));
        pVoice->Release();
    }
}

void TestSpeed()
{
    const std::string path = MakeFile("tone10s.wav", MakeWav(48000, 1, 32, Tone(440, 48000, 480000, 0.5f)));
    for (int bStream = 0; bStream < 2; bStream++)
    {
        const char *pKind = bStream ? "stream" : "buffer";
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(bStream ? 0 : AudioEngine::WholeLimit);
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), false, false, error);
        if (!pVoice)
        {
            Check(false, "speed: play " + error);
            continue;
        }
        Check(pVoice->SetPlaybackSpeed(2) && (pVoice->GetPlaybackSpeed() == 2), Format("speed %s: 2 accepted", pKind));
        Render(*engine, 48000);
        const int pos2 = pVoice->GetPlayPosition();
        Check(abs(pos2 - 2000) <= 1, Format("speed %s: 1 s at speed 2 plays 2000 ms (%d)", pKind, pos2));
        pVoice->SetPlaybackSpeed(8);
        Render(*engine, 24000);
        const int pos8 = pVoice->GetPlayPosition();
        Check(abs(pos8 - 6000) <= 2, Format("speed %s: then 0.5 s at speed 8 plays 4000 ms more (%d)", pKind, pos8));
        Check(pVoice->SetPlaybackSpeed(100) && (pVoice->GetPlaybackSpeed() == 100), Format("speed %s: 100 accepted (no upper bound)", pKind));
        Render(*engine, 1024 * 16);
        Check(pVoice->IsFinished(), Format("speed %s: at speed 100 the last 4 s end at once", pKind));
        Check(!pVoice->SetPlaybackSpeed(0) && !pVoice->SetPlaybackSpeed(-1) && !pVoice->SetPlaybackSpeed(NAN) && !pVoice->SetPlaybackSpeed(INFINITY),
            Format("speed %s: 0, -1, NaN and infinity refused", pKind));
        pVoice->Release();
    }
}

void TestResample()
{
    struct Case { const char *pWhat; uint32_t rate; double tone; double speed; double at; double reference; bool bPass; double limit; };
    const Case cases[] = {
        { "5 kHz at 22.05 kHz: the tone", 22050, 5000, 1, 5000, 0.5, true, 0.1 },
        { "5 kHz at 22.05 kHz: its image at 17.05 kHz", 22050, 5000, 1, 17050, 0.5, false, -60 },
        { "16 kHz at 48 kHz, speed 2 (32 kHz): its alias at 16 kHz", 48000, 16000, 2, 16000, 0.5, false, -60 },
        { "1 kHz at 48 kHz, speed 8: the tone at 8 kHz", 48000, 1000, 8, 8000, 0.5, true, 0.5 },
        { "10 kHz at 48 kHz, speed 8 (80 kHz): its alias at 16 kHz", 48000, 10000, 8, 16000, 0.5, false, -50 } };
    for (const Case &c : cases)
    {
        const uint64_t frames = static_cast<uint64_t>(c.rate * (0.5 * c.speed + 0.2));
        const std::string path = MakeFile("resample.wav", MakeWav(c.rate, 1, 32, Tone(c.tone, c.rate, frames, 0.5f)));
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), false, false, error);
        if (!pVoice)
        {
            Check(false, "resample: play " + error);
            continue;
        }
        pVoice->SetPlaybackSpeed(static_cast<float>(c.speed));
        std::vector<float> out = Render(*engine, 24000);
        const double db = Db(Level(out, 2048, 16384, c.at), c.reference);
        Check(c.bPass ? (fabs(db) <= c.limit) : (db <= c.limit), Format("resample %s: %.2f dB", c.pWhat, db));
        pVoice->Release();
    }

    // cost of the filter, 16 voices: normal width (22.05 kHz), then widened (48 kHz at speeds 1.5 and 4)
    const std::string path22 = MakeFile("cost22.wav", MakeWav(22050, 2, 32, Tone(440, 22050, 2 * 22050 * 3, 0.1f)));
    const std::string path48 = MakeFile("cost48.wav", MakeWav(48000, 2, 32, Tone(440, 48000, 2 * 48000 * 3, 0.1f)));
    const struct { const std::string &path; const char *pRate; float speed; } runs[] = { { path22, "22.05", 1.0f }, { path48, "48", 1.5f }, { path48, "48", 4.0f } };
    for (const auto &run : runs)
    {
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        std::string error;
        for (int i = 0; i < 16; i++)
        {
            AudioVoice *pVoice = engine->Play(run.path.c_str(), true, false, error);
            if (pVoice)
                pVoice->SetPlaybackSpeed(run.speed);
        }
        Render(*engine, 1024);
        const Clock::time_point t0 = Clock::now();
        Render(*engine, 48000);
        const double ns = Elapsed(t0) * 1e9 / (48000.0 * 16);
        printf("      filter cost at speed %.1f: %.1f ns per voice and output frame (stereo %s kHz)\n", run.speed, ns, run.pRate);
    }
}

void TestCorrupt()
{
    std::mt19937 random(12345);
    auto noise = [&random](const size_t n) { std::vector<uint8_t> b(n); for (uint8_t &x : b) x = static_cast<uint8_t>(random()); return b; };
    auto withMagic = [&noise](const char *pMagic, const size_t n) { std::vector<uint8_t> b; PutText(b, pMagic); std::vector<uint8_t> r = noise(n); b.insert(b.end(), r.begin(), r.end()); return b; };
    std::vector<uint8_t> id3 = { 'I', 'D', '3', 3, 0, 0, 0, 0, 0, 100 };
    id3.resize(110, 0);
    std::vector<uint8_t> r = noise(4096);
    id3.insert(id3.end(), r.begin(), r.end());
    std::vector<float> nan = Tone(440, 48000, 4800, 0.5f);
    for (size_t i = 0; i < nan.size(); i += 7)
        nan[i] = (i % 2) ? NAN : INFINITY;

    struct Case { const char *pName; std::vector<uint8_t> bytes; bool bRefused; const char *pError; };
    const Case cases[] = {
        { "empty file", {}, true, "unknown" },
        { "random bytes", noise(65536), true, "unknown" },
        { "OggS and garbage", withMagic("OggS", 4096), false, nullptr },
        { "fLaC and garbage", withMagic("fLaC", 4096), false, nullptr },
        { "ID3 tag and garbage", id3, false, nullptr },
        { "OGG cut at 64 KB", ReadBytes(s_assets + "/Music/Solar Serenity.ogg", 65536), false, nullptr },
        { "MP3 cut at 3 KB", ReadBytes(s_data + "/sine.mp3", 3000), false, nullptr },
        { "WAV with 40 channels", MakeWav(48000, 1, 16, Tone(440, 48000, 4000, 0.5f), 0, 40), true, "32 channels" },
        { "WAV with 65535 channels", MakeWav(48000, 1, 8, Tone(440, 48000, 65535, 0.5f), 0, 65535), true, "32 channels" },
        { "WAV in ADPCM", MakeWav(48000, 1, 16, Tone(440, 48000, 4000, 0.5f), 2), true, "unsupported WAV" },
        { "WAV data size beyond the file", MakeWav(48000, 1, 16, Tone(440, 48000, 1000, 0.5f), 0, 0, 1000000), false, nullptr },
        { "WAV with NaN and infinity", MakeWav(48000, 1, 32, nan), false, nullptr } };
    for (const Case &c : cases)
    {
        const std::string path = MakeFile("corrupt.bin", c.bytes);
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), false, false, error);
        if (!pVoice)
        {
            Check(c.bRefused && (error.find(c.pError) != std::string::npos), Format("corrupt %s: refused (%s)", c.pName, error.c_str()));
            continue;
        }
        bool bFinite = true;
        for (int i = 0; (i < 250) && !pVoice->IsFinished(); i++)
            bFinite = AllFinite(Render(*engine, 1024)) && bFinite;
        std::vector<std::string> log;
        Update(*engine, &log);
        Check(!c.bRefused && bFinite && pVoice->IsFinished(), Format("corrupt %s: plays what it can and ends, output finite%s", c.pName, log.empty() ? "" : " (load failed)"));
        if (!log.empty())
        {
            // remembered: the next Play fails at once, even once the file is good
            WriteBytes(path, MakeWav(48000, 1, 16, Tone(440, 48000, 4800, 0.5f)));
            std::string again;
            const bool bRefused = !engine->Play(path.c_str(), false, false, again);
            Check(bRefused && !again.empty() && (log[0].find(again) != std::string::npos), Format("corrupt %s: a second Play fails from the cache (%s)", c.pName, again.c_str()));
        }
        pVoice->Release();
        Update(*engine);
    }

    // a looping stream cut to its header while it plays: the next pass gives nothing, so it ends instead of restarting forever
    {
        const std::string path = MakeFile("cutloop.wav", MakeWav(48000, 1, 32, Tone(440, 48000, 4800, 0.5f)));
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        engine->SetWholeLimit(0);
        std::string error;
        AudioVoice *pVoice = engine->Play(path.c_str(), true, false, error);
        Render(*engine, 9600);
        const bool bLooping = pVoice && !pVoice->IsFinished();
        const bool bCut = (truncate(path.c_str(), 44) == 0);
        const Clock::time_point t0 = Clock::now();
        bool bFinite = true;
        int periods = 0;
        for (; (periods < 200) && (Elapsed(t0) < 5) && pVoice && !pVoice->IsFinished(); periods++)
            bFinite = AllFinite(Render(*engine, 1024)) && bFinite;
        Check(bLooping && bCut && bFinite && pVoice && pVoice->IsFinished(), Format("corrupt looping stream cut to its header: ends %d periods later (%.3f s)", periods, Elapsed(t0)));
        auto cpu = [] { rusage u; getrusage(RUSAGE_SELF, &u); return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3; };
        const double before = cpu();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const double busy = cpu() - before;
        Check(busy < 50, Format("corrupt looping stream cut to its header: the feeder rests while the voice is held (%.0f ms CPU in 200 ms)", busy));
        if (pVoice)
            pVoice->Release();
        Update(*engine);
    }
}

void TestCache()
{
    const std::string path = MakeFile("cache.wav", MakeWav(48000, 1, 32, Tone(440, 48000, 48000, 0.5f)));
    const std::string big = MakeFile("cachebig.wav", MakeWav(48000, 1, 32, Tone(440, 48000, 96000, 0.5f)));
    std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
    std::string error;
    AudioVoice *pA = engine->Play(path.c_str(), false, false, error);
    AudioVoice *pB = engine->Play(path.c_str(), false, false, error);    // joins the first one's load
    Render(*engine, 1024);
    Update(*engine);
    Check(pA && pB && (engine->GetCacheBytes() == 48000 * sizeof(float)), Format("cache: two voices of one WAV cache it once (%zu bytes)", engine->GetCacheBytes()));
    engine->SetWholeLimit(100000);
    AudioVoice *pC = engine->Play(big.c_str(), false, false, error);
    Render(*engine, 1024);
    Update(*engine);
    Check(pC && (engine->GetCacheBytes() == 48000 * sizeof(float)), "cache: a stream adds nothing to the cache");

    // out of file handles: an error for now, not remembered
    const int lowest = open("/dev/null", O_RDONLY);
    close(lowest);
    rlimit limit, low;
    getrlimit(RLIMIT_NOFILE, &limit);
    low = limit;
    low.rlim_cur = static_cast<rlim_t>(lowest);
    const std::string other = MakeFile("cache2.wav", MakeWav(48000, 1, 16, Tone(440, 48000, 4800, 0.5f)));
    setrlimit(RLIMIT_NOFILE, &low);
    AudioVoice *pD = engine->Play(other.c_str(), false, false, error);
    setrlimit(RLIMIT_NOFILE, &limit);
    AudioVoice *pE = engine->Play(other.c_str(), false, false, error);
    Check(!pD && pE, "cache: out of file handles is not remembered as a bad file");
    for (AudioVoice *pVoice : { pA, pB, pC, pE })
        if (pVoice)
            pVoice->Release();
}

void TestId3()
{
    std::vector<uint8_t> tag = { 'I', 'D', '3', 3, 0, 0, 0, 0, 0x17, 0x38 };     // syncsafe 3000
    tag.resize(3010, 0);
    struct { const char *pName; std::vector<uint8_t> plain; AudioFormat format; } files[] = {
        { "OGG", ReadBytes(s_assets + "/Music/Solar Serenity.ogg", 262144), AudioFormat::Ogg },
        { "FLAC", ReadBytes(s_data + "/sine.flac"), AudioFormat::Flac } };
    for (const auto &file : files)
    {
        std::vector<uint8_t> tagged = tag;
        tagged.insert(tagged.end(), file.plain.begin(), file.plain.end());
        const std::string plainPath = MakeFile("id3plain.bin", file.plain);
        const std::string taggedPath = MakeFile("id3tagged.bin", tagged);
        std::string error;
        AudioProbe probe;
        std::unique_ptr<AudioStream> stream = OpenFile(taggedPath, probe, error);
        Check(stream && (probe.format == file.format) && (probe.base == 3010), Format("ID3v2 + %s: detected behind the tag %s", file.pName, error.c_str()));
        std::shared_ptr<AudioBuffer> a = DecodeFile(plainPath, error);
        std::shared_ptr<AudioBuffer> b = DecodeFile(taggedPath, error);
        Check(a && b && (a->samples == b->samples), Format("ID3v2 + %s: decodes the same as without the tag", file.pName));
    }
}

void TestAsync()
{
    // a 3 MB MP3 without a length header: its frame scan runs on the loader, not in Play
    const std::vector<uint8_t> one = ReadBytes(s_data + "/sine.mp3");
    std::vector<uint8_t> many;
    for (int i = 0; i < 250; i++)
        many.insert(many.end(), one.begin(), one.end());
    const std::string mp3 = MakeFile("big.mp3", many);
    std::string error;
    std::shared_ptr<AudioBuffer> single = DecodeFile(s_data + "/sine.mp3", error);
    for (const std::string &path : { mp3, s_assets + "/Music/Solar Serenity.ogg" })
    {
        std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
        const Clock::time_point t0 = Clock::now();
        AudioVoice *pVoice = engine->Play(path.c_str(), false, false, error);
        const double seconds = Elapsed(t0);
        const std::string name = path.substr(path.rfind('/') + 1);
        Check(pVoice && !pVoice->IsFinished() && (pVoice->GetLength() == -1) && (seconds < 0.05), Format("async %s: Play returns in %.1f ms, the voice loads", name.c_str(), seconds * 1000));
        if (!pVoice)
            continue;
        Render(*engine, 1024);
        const int length = pVoice->GetLength();
        const int expected = (path == mp3) ? (single ? static_cast<int>(250 * single->frames / 48) : 0) : 0;
        Check((length > 60000) && ((expected == 0) || (abs(length - expected) <= 50)), Format("async %s: length %d ms once loaded", name.c_str(), length));
        Check(engine->GetCacheBytes() == 0, Format("async %s: streamed, nothing cached", name.c_str()));
        pVoice->Release();
    }
}

void TestMisc()
{
    std::unique_ptr<AudioEngine> engine(AudioEngine::CreateOffline());
    std::string error;
    const std::string path = MakeFile("misc.wav", MakeWav(48000, 1, 16, Tone(440, 48000, 4800, 0.5f)));
    AudioVoice *pVoice = engine->Play(path.c_str(), false, true, error);
    if (pVoice)
    {
        pVoice->SetVolume(NAN);
        pVoice->SetPan(NAN);
        Check((pVoice->GetVolume() == 0) && (pVoice->GetPan() == 0), "NaN volume and pan become 0");
        pVoice->Release();
    }

    Clock::time_point t0 = Clock::now();
    AudioEngine *pEngine = AudioEngine::Create("XRSound test", error, "libpipewire-missing.so.0");
    Check(!pEngine && (error.find("libpipewire-missing.so.0") != std::string::npos) && (Elapsed(t0) < 1), "no libpipewire: no engine, the error names the library (" + error + ")");
    delete pEngine;

    t0 = Clock::now();
    pEngine = AudioEngine::Create("XRSound test", error);
    Check(!pEngine && (Elapsed(t0) < 2), Format("no daemon (PIPEWIRE_REMOTE=%s): no engine in %.3f s (%s)", getenv("PIPEWIRE_REMOTE"), Elapsed(t0), error.c_str()));
    delete pEngine;
}

// --daemon: a private daemon (DaemonTest.sh sets the environment inside bwrap); every voice plays at volume 0
pid_t Spawn(const std::vector<std::string> &args, const std::string &logPath)
{
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    std::vector<char *> argv;
    for (const std::string &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = -1;
    if (posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) != 0)
        pid = -1;
    posix_spawn_file_actions_destroy(&actions);
    return pid;
}

void Stop(pid_t &pid)
{
    if (pid > 0)
    {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
    pid = -1;
}

std::string Run(const std::string &command)
{
    std::string out;
    FILE *pPipe = popen(command.c_str(), "r");
    if (!pPipe)
        return out;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), pPipe)) > 0)
        out.append(buffer, n);
    pclose(pPipe);
    return out;
}

int NodeId(const std::string &name)
{
    const std::string list = Run("pw-cli ls Node 2>/dev/null");
    int id = -1;
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t end = list.find('\n', pos);
        if (end == std::string::npos)
            end = list.size();
        const std::string line = list.substr(pos, end - pos);
        int n;
        if (sscanf(line.c_str(), " id %d,", &n) == 1)
            id = n;
        else if (line.find("node.name = \"" + name + "\"") != std::string::npos)
            return id;
        pos = end + 1;
    }
    return -1;
}

int DaemonTests(const std::string &dir)
{
    const char *pRun = getenv("PIPEWIRE_RUNTIME_DIR");
    const char *pRemote = getenv("PIPEWIRE_REMOTE");
    if (!pRun || !pRemote)
    {
        printf("skip: run through DaemonTest.sh\n");
        return 77;
    }
    const std::string run = pRun;
    const std::string socket = run + "/" + pRemote;
    const std::string conf = dir + "/xrsound-test-daemon.conf";
    struct stat st;
    auto startDaemon = [&]()
    {
        pid_t pid = Spawn({ "pipewire", "-c", conf }, run + "/daemon.log");
        for (int i = 0; (i < 250) && (stat(socket.c_str(), &st) != 0); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return pid;
    };
    unlink(socket.c_str());
    pid_t daemon = startDaemon();
    Check((daemon > 0) && (stat(socket.c_str(), &st) == 0), "daemon: private pipewire is up");

    const std::string one = MakeFile("one1s.wav", MakeWav(48000, 1, 16, Tone(440, 48000, 48000, 0.5f)));
    const std::string three = MakeFile("three3s.wav", MakeWav(48000, 1, 16, Tone(440, 48000, 144000, 0.5f)));
    std::string error;
    AudioEngine *pEngine = AudioEngine::Create("XRSound test", error);
    Check(pEngine != nullptr, "daemon: engine created " + error);
    if (!pEngine)
    {
        Stop(daemon);
        return 1;
    }
    std::vector<std::string> log;
    auto waitFor = [&](const std::function<bool()> &condition, const double seconds)
    {
        const Clock::time_point t0 = Clock::now();
        while (!condition() && (Elapsed(t0) < seconds))
        {
            Update(*pEngine, &log);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    };
    auto logged = [&log](const char *pText) { for (const std::string &line : log) if (line.find(pText) != std::string::npos) return true; return false; };
    auto play = [&](const std::string &path, const bool bLoop)
    {
        AudioVoice *pVoice = pEngine->Play(path.c_str(), bLoop, true, error);
        if (pVoice)
        {
            pVoice->SetVolume(0);
            pVoice->SetPaused(false);
        }
        return pVoice;
    };

    // D1: no session manager, so the stream stays PAUSED: the silent clock ends a one-shot on time
    AudioVoice *pOne = play(one, false);
    Clock::time_point t0 = Clock::now();
    const bool bEnded = pOne && waitFor([&] { return pOne->IsFinished(); }, 3);
    const double took = Elapsed(t0);
    Check(bEnded && (took > 0.85) && (took < 1.8) && (pEngine->GetPeriods() == 0), Format("D1 no sink: a 1 s sound ends after %.2f s, %llu process calls", took, static_cast<unsigned long long>(pEngine->GetPeriods())));
    if (pOne)
        pOne->Release();

    // D2: the node is destroyed: lost, then restored; a loop plays on
    AudioVoice *pLoop = play(three, true);
    const int node = NodeId("XRSound");
    Run("pw-cli destroy " + std::to_string(node) + " >/dev/null 2>&1");
    t0 = Clock::now();
    const bool bLost2 = waitFor([&] { return logged("lost"); }, 2);
    const bool bBack2 = waitFor([&] { return logged("restored"); }, 6);
    Check((node > 0) && bLost2 && bBack2 && pLoop && !pLoop->IsFinished(), Format("D2 node %d destroyed: lost, restored after %.1f s, the loop plays on", node, Elapsed(t0)));

    // D3: the daemon dies and comes back
    log.clear();
    Stop(daemon);
    const bool bLost3 = waitFor([&] { return logged("lost"); }, 2);
    waitFor([] { return false; }, 1.5);
    t0 = Clock::now();
    daemon = startDaemon();
    const bool bBack3 = waitFor([&] { return logged("restored"); }, 10);
    Check(bLost3 && bBack3 && pLoop && !pLoop->IsFinished(), Format("D3 daemon killed: lost, restored %.1f s after it came back, the loop plays on", Elapsed(t0)));
    if (pLoop)
    {
        pLoop->Stop();
        pLoop->Release();
    }

    // D4: WirePlumber links the stream to the null sink: real process calls, the position follows the wall clock
    pid_t wireplumber = Spawn({ "wireplumber", "-p", "xrsound-test" }, run + "/wireplumber.log");
    const bool bStreaming = waitFor([&] { return pEngine->GetPeriods() > 10; }, 10);
    Check((wireplumber > 0) && bStreaming, Format("D4 WirePlumber: the stream is linked, %llu process calls", static_cast<unsigned long long>(pEngine->GetPeriods())));
    AudioVoice *pThree = play(three, false);
    t0 = Clock::now();
    waitFor([&] { return Elapsed(t0) > 1.0; }, 2);
    const double wall = Elapsed(t0) * 1000;
    const int pos = pThree ? pThree->GetPlayPosition() : -1;
    Check(pThree && (fabs(pos - wall) < 100), Format("D4 streaming: position %d ms after %.0f ms", pos, wall));

    // the sink goes: the silent clock takes over without a jump, and the sound ends on time
    const int sink = NodeId("xrs-null");
    Run("pw-cli destroy " + std::to_string(sink) + " >/dev/null 2>&1");
    waitFor([&] { return Elapsed(t0) > 2.0; }, 2);
    const double wall2 = Elapsed(t0) * 1000;
    const int pos2 = pThree ? pThree->GetPlayPosition() : -1;
    const uint64_t periods = pEngine->GetPeriods();
    const bool bEnded4 = pThree && waitFor([&] { return pThree->IsFinished(); }, 3);
    const double end = Elapsed(t0);
    Check((sink > 0) && (fabs(pos2 - wall2) < 150) && bEnded4 && (end > 2.7) && (end < 3.6),
        Format("D4 sink %d destroyed: position %d ms after %.0f ms, ends after %.2f s (process calls %llu -> %llu)", sink, pos2, wall2, end,
            static_cast<unsigned long long>(periods), static_cast<unsigned long long>(pEngine->GetPeriods())));
    if (pThree)
        pThree->Release();

    delete pEngine;
    Stop(wireplumber);
    Stop(daemon);
    return s_failures ? 1 : 0;
}
}

int main(int argc, char **argv)
{
    char tmpl[] = "/tmp/xrsound-test.XXXXXX";
    const char *pTmp = mkdtemp(tmpl);
    if (!pTmp)
    {
        printf("FAIL: no temp dir\n");
        return 1;
    }
    s_tmp = pTmp;

    int rc;
    if ((argc > 2) && !strcmp(argv[1], "--daemon"))
        rc = DaemonTests(argv[2]);
    else
    {
        setenv("PIPEWIRE_REMOTE", "xrsound-test-none", 1);    // whatever runs this, PipeWire is never reached
        TestDecoders();
        TestSeekAccuracy();
        TestLoop();
        TestSeek();
        TestSpeed();
        TestResample();
        TestCorrupt();
        TestCache();
        TestId3();
        TestAsync();
        TestMisc();
        rc = s_failures ? 1 : 0;
    }
    Run("rm -rf '" + s_tmp + "'");   // this run's own temp dir
    printf("%s\n", (rc == 77) ? "SKIPPED" : (rc ? "FAILED" : "PASSED"));
    return rc;
}
