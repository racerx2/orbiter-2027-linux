// not upstream: sound file decoding for XRSound's audio engine (WAV parsed here; OGG, MP3, FLAC via stb_vorbis, dr_mp3, dr_flac; MOD, S3M, XM, IT via libxmp-lite)

#include "AudioDecode.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// the decoders are public-domain single-file libraries fetched by CMake; they read the file through the callbacks below
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#include <xmp.h>

namespace
{
uint16_t Le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// errors that may be gone on the next try, so a failed load is not remembered
bool IsTransient(const int e)
{
    return (e == EMFILE) || (e == ENFILE) || (e == EIO) || (e == ENOMEM) || (e == EINTR) || (e == EAGAIN);
}

// one decoder's position in a file; offsets are relative to the base (after ID3v2 tags)
struct Cursor
{
    Cursor(const AudioSource *pSource, const uint64_t base) : pSource(pSource), base(base) { }

    uint64_t Size() const { return pSource->Size() - base; }

    size_t Read(void *pOut, const size_t count)
    {
        if (pos >= Size())
            return 0;
        const int64_t n = pSource->Read(base + pos, pOut, std::min<uint64_t>(count, Size() - pos));
        if (n < 0)
        {
            bFailed = true;
            return 0;
        }
        pos += static_cast<uint64_t>(n);
        return static_cast<size_t>(n);
    }

    bool Seek(const int64_t offset, const int whence)
    {
        const int64_t target = (whence == SEEK_SET) ? offset : ((whence == SEEK_CUR) ? static_cast<int64_t>(pos) + offset : static_cast<int64_t>(Size()) + offset);
        if ((target < 0) || (static_cast<uint64_t>(target) > Size()))
            return false;
        pos = static_cast<uint64_t>(target);
        return true;
    }

    const AudioSource *pSource;
    uint64_t base;
    uint64_t pos = 0;
    bool bFailed = false;
};

size_t DrRead(void *pUser, void *pOut, size_t count)
{
    return static_cast<Cursor *>(pUser)->Read(pOut, count);
}

int Whence(const int origin)   // dr_mp3 and dr_flac number their origins SET, CUR, END like SEEK_*
{
    return (origin == 0) ? SEEK_SET : ((origin == 1) ? SEEK_CUR : SEEK_END);
}

drmp3_bool32 Mp3Seek(void *pUser, int offset, drmp3_seek_origin origin)
{
    return static_cast<Cursor *>(pUser)->Seek(offset, Whence(origin)) ? DRMP3_TRUE : DRMP3_FALSE;
}

drmp3_bool32 Mp3Tell(void *pUser, drmp3_int64 *pCursor)
{
    *pCursor = static_cast<drmp3_int64>(static_cast<Cursor *>(pUser)->pos);
    return DRMP3_TRUE;
}

drflac_bool32 FlacSeek(void *pUser, int offset, drflac_seek_origin origin)
{
    return static_cast<Cursor *>(pUser)->Seek(offset, Whence(origin)) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

drflac_bool32 FlacTell(void *pUser, drflac_int64 *pCursor)
{
    *pCursor = static_cast<drflac_int64>(static_cast<Cursor *>(pUser)->pos);
    return DRFLAC_TRUE;
}

unsigned long XmpRead(void *pDest, unsigned long len, unsigned long nmemb, void *pPriv)
{
    return (len == 0) ? 0 : static_cast<unsigned long>(static_cast<Cursor *>(pPriv)->Read(pDest, len * nmemb) / len);
}

int XmpSeek(void *pPriv, long offset, int whence)
{
    return static_cast<Cursor *>(pPriv)->Seek(offset, whence) ? 0 : -1;
}

long XmpTell(void *pPriv)
{
    return static_cast<long>(static_cast<Cursor *>(pPriv)->pos);
}

xmp_callbacks XmpCallbacks()
{
    xmp_callbacks callbacks{};
    callbacks.read_func = XmpRead;
    callbacks.seek_func = XmpSeek;
    callbacks.tell_func = XmpTell;
    return callbacks;
}

// stb_vorbis reads a FILE; glibc's fopencookie makes one on a cursor
ssize_t CookieRead(void *pCookie, char *pBuf, size_t size)
{
    Cursor *pCursor = static_cast<Cursor *>(pCookie);
    const size_t n = pCursor->Read(pBuf, size);
    return pCursor->bFailed ? -1 : static_cast<ssize_t>(n);
}

int CookieSeek(void *pCookie, off64_t *pOffset, int whence)
{
    Cursor *pCursor = static_cast<Cursor *>(pCookie);
    if (!pCursor->Seek(*pOffset, whence))
        return -1;
    *pOffset = static_cast<off64_t>(pCursor->pos);
    return 0;
}

int CookieClose(void *)
{
    return 0;   // the stream owns the cursor
}

// WAV header: fmt and data chunks read with a few small preads
bool ParseWav(const AudioSource &source, AudioProbe &w, std::string &error, bool &bTransient)
{
    bool bHaveFmt = false, bHaveData = false;
    uint64_t dataBytes = 0;
    uint64_t pos = w.base + 12;    // after "RIFF" <size> "WAVE"
    const uint64_t end = source.Size();
    while (pos + 8 <= end)
    {
        uint8_t chunk[8];
        if (source.Read(pos, chunk, 8) != 8)
        {
            error = "can't read the file";
            bTransient = true;
            return false;
        }
        const uint32_t size = Le32(chunk + 4);
        const uint64_t avail = end - (pos + 8);
        if (!memcmp(chunk, "fmt ", 4) && (size >= 16) && (avail >= 16))
        {
            uint8_t f[40] = { };
            const uint64_t n = std::min<uint64_t>(std::min<uint64_t>(size, avail), sizeof(f));
            if (source.Read(pos + 8, f, n) != static_cast<int64_t>(n))
            {
                error = "can't read the file";
                bTransient = true;
                return false;
            }
            w.wavFormat = Le16(f);
            w.channels = Le16(f + 2);
            w.sampleRate = Le32(f + 4);
            w.blockAlign = Le16(f + 12);
            w.bits = Le16(f + 14);
            if ((w.wavFormat == 0xFFFE) && (size >= 40) && (avail >= 40))   // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the format code
                w.wavFormat = Le16(f + 24);
            bHaveFmt = true;
        }
        else if (!memcmp(chunk, "data", 4))
        {
            w.dataOffset = pos + 8;
            dataBytes = std::min<uint64_t>(size, avail);   // a truncated file plays what it has
            bHaveData = true;
            if (bHaveFmt)
                break;
        }
        pos += 8 + static_cast<uint64_t>(size) + (size & 1);   // chunks are padded to an even size
    }

    if (!bHaveFmt || !bHaveData)
    {
        error = "WAV file without fmt or data chunk";
        return false;
    }
    if ((w.channels == 0) || (w.sampleRate == 0) || (w.blockAlign == 0) || (w.blockAlign % w.channels))
    {
        error = "WAV file with a broken fmt chunk";
        return false;
    }
    const uint16_t sampleBytes = w.blockAlign / w.channels;   // container size of one sample
    const bool bSupported = ((w.wavFormat == 1) && (sampleBytes <= 4)) || ((w.wavFormat == 3) && ((sampleBytes == 4) || (sampleBytes == 8)));
    if (!bSupported)
    {
        error = "unsupported WAV sample format " + std::to_string(w.wavFormat) + " with " + std::to_string(w.bits) + " bits";
        return false;
    }
    if (w.channels > 32)
    {
        error = "sound file with more than 32 channels";   // a corrupt header; buffers are sized by the channel count
        return false;
    }
    w.frames = dataBytes / w.blockAlign;
    return true;
}

// one WAV sample to -1..1 (x86-64 and AArch64 are little-endian, as WAV is)
inline float WavSample(const uint8_t *p, const uint16_t format, const uint16_t sampleBytes)
{
    if (format == 3)
    {
        float f;
        if (sampleBytes == 4)
            memcpy(&f, p, 4);
        else
        {
            double d;
            memcpy(&d, p, 8);
            f = static_cast<float>(d);
        }
        return std::isfinite(f) ? f : 0.0f;     // one NaN or Inf sample would spoil the whole mix
    }
    switch (sampleBytes)
    {
    case 1:
        return (static_cast<int>(p[0]) - 128) / 128.0f;    // 8-bit WAV is unsigned
    case 2:
        return static_cast<int16_t>(Le16(p)) / 32768.0f;
    case 3:
        return static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 24)) / 2147483648.0f;
    default:
        return static_cast<int32_t>(Le32(p)) / 2147483648.0f;
    }
}

class WavStream : public AudioStream
{
public:
    WavStream(const std::shared_ptr<AudioSource> &source, const AudioProbe &info) : m_source(source), m_info(info), m_frame(0)
    {
        channels = info.channels;
        sampleRate = info.sampleRate;
        frames = info.frames;
    }

    uint64_t Read(float *pOut, const uint64_t frameCount) override
    {
        const uint64_t want = std::min<uint64_t>(std::min(frameCount, frames - m_frame), 16384);
        if (want == 0)
            return 0;
        m_bytes.resize(want * m_info.blockAlign);
        const int64_t got = m_source->Read(m_info.dataOffset + m_frame * m_info.blockAlign, m_bytes.data(), m_bytes.size());
        if (got <= 0)
            return 0;
        const uint64_t n = static_cast<uint64_t>(got) / m_info.blockAlign;
        const uint16_t sampleBytes = m_info.blockAlign / m_info.channels;
        const uint8_t *p = m_bytes.data();
        for (uint64_t i = 0; i < n * m_info.channels; i++, p += sampleBytes)
            pOut[i] = WavSample(p, m_info.wavFormat, sampleBytes);
        m_frame += n;
        return n;
    }

    bool Seek(const uint64_t frame) override
    {
        if (frame > frames)
            return false;
        m_frame = frame;
        return true;
    }

private:
    std::shared_ptr<AudioSource> m_source;
    AudioProbe m_info;
    uint64_t m_frame;
    std::vector<uint8_t> m_bytes;
};

class OggStream : public AudioStream
{
public:
    OggStream(const std::shared_ptr<AudioSource> &source, const uint64_t base) : m_source(source), m_cursor(source.get(), base), m_pVorbis(nullptr) { }

    ~OggStream() override
    {
        stb_vorbis_close(m_pVorbis);    // closes the FILE too
    }

    bool Open(std::string &error)
    {
        if (m_cursor.Size() > UINT_MAX)
        {
            error = "OGG file too large";
            return false;
        }
        cookie_io_functions_t io{ CookieRead, nullptr, CookieSeek, CookieClose };
        FILE *pFile = fopencookie(&m_cursor, "r", io);
        if (!pFile)
        {
            error = "can't read the file";
            return false;
        }
        setvbuf(pFile, nullptr, _IOFBF, 65536);
        int err = 0;
        m_pVorbis = stb_vorbis_open_file_section(pFile, 1, &err, nullptr, static_cast<unsigned int>(m_cursor.Size()));   // closes pFile when it fails
        if (!m_pVorbis)
        {
            error = "broken OGG Vorbis data (stb_vorbis error " + std::to_string(err) + ")";
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(m_pVorbis);
        channels = static_cast<uint32_t>(info.channels);
        sampleRate = info.sample_rate;
        frames = stb_vorbis_stream_length_in_samples(m_pVorbis);
        return true;
    }

    uint64_t Read(float *pOut, const uint64_t frameCount) override
    {
        uint64_t done = 0;
        while (done < frameCount)
        {
            const int want = static_cast<int>(std::min<uint64_t>(frameCount - done, 65536) * channels);
            const int n = stb_vorbis_get_samples_float_interleaved(m_pVorbis, static_cast<int>(channels), pOut + done * channels, want);
            if (n <= 0)
                break;
            ToWavOrder(pOut + done * channels, static_cast<uint64_t>(n));
            done += static_cast<uint64_t>(n);
        }
        return done;
    }

    bool Seek(const uint64_t frame) override
    {
        if (frame == 0)
            return stb_vorbis_seek_start(m_pVorbis) != 0;  // every loop pass: the same samples as a fresh open, without the page search
        return (frame <= UINT_MAX) && (stb_vorbis_seek(m_pVorbis, static_cast<unsigned int>(frame)) != 0);
    }

private:
    // Vorbis orders 3 to 8 channels its own way; the fold takes WAV's (FL FR FC LFE BL BR SL SR)
    void ToWavOrder(float *p, const uint64_t n) const
    {
        static const int order[9][8] = {
            {}, {}, {},
            { 0, 2, 1 },                        // L C R
            { 0, 1, 2, 3 },                     // FL FR RL RR
            { 0, 2, 1, 3, 4 },                  // FL C FR RL RR
            { 0, 2, 1, 5, 3, 4 },               // FL C FR RL RR LFE
            { 0, 2, 1, 6, 5, 3, 4 },            // FL C FR SL SR RC LFE
            { 0, 2, 1, 7, 5, 6, 3, 4 } };       // FL C FR SL SR RL RR LFE
        if ((channels < 3) || (channels > 8))
            return;
        float f[8];
        for (uint64_t i = 0; i < n; i++, p += channels)
        {
            memcpy(f, p, channels * sizeof(float));
            for (uint32_t c = 0; c < channels; c++)
                p[c] = f[order[channels][c]];
        }
    }

    std::shared_ptr<AudioSource> m_source;
    Cursor m_cursor;        // stb_vorbis reads through it
    stb_vorbis *m_pVorbis;
};

class ModStream : public AudioStream
{
public:
    ModStream(const std::shared_ptr<AudioSource> &source, const uint64_t base) : m_source(source), m_cursor(source.get(), base), m_ctx(xmp_create_context()), m_bLoaded(false), m_bEnd(false) { }

    ~ModStream() override
    {
        if (m_bLoaded)
        {
            xmp_end_player(m_ctx);
            xmp_release_module(m_ctx);
        }
        xmp_free_context(m_ctx);
    }

    // renders the song once through at the engine's rate, 16-bit stereo, while it plays
    bool Open(std::string &error)
    {
        if (!m_ctx || (xmp_load_module_from_callbacks(m_ctx, &m_cursor, XmpCallbacks()) != 0))
        {
            error = "broken tracker module";
            return false;
        }
        m_bLoaded = true;
        if (xmp_start_player(m_ctx, Rate, 0) != 0)
        {
            error = "tracker module player failed to start";
            return false;
        }
        xmp_frame_info fi;
        xmp_get_frame_info(m_ctx, &fi);
        channels = 2;
        sampleRate = Rate;
        frames = static_cast<uint64_t>(std::max(fi.total_time, 0)) * Rate / 1000;   // an estimate; the feed learns the real end
        return true;
    }

    uint64_t Read(float *pOut, const uint64_t frameCount) override
    {
        uint64_t done = 0;
        int16_t pcm[2 * 1024];
        while ((done < frameCount) && !m_bEnd)
        {
            const uint64_t n = std::min<uint64_t>(frameCount - done, 1024);
            if (xmp_play_buffer(m_ctx, pcm, static_cast<int>(n * 4), 1) != 0)   // loop 1: once through, then the end
            {
                m_bEnd = true;
                break;
            }
            for (uint64_t i = 0; i < 2 * n; i++)
                pOut[2 * done + i] = pcm[i] / 32768.0f;
            done += n;
        }
        return done;
    }

    bool Seek(const uint64_t frame) override
    {
        m_bEnd = false;
        if (frame == 0)
        {
            xmp_restart_module(m_ctx);
            xmp_play_buffer(m_ctx, nullptr, 0, 0);   // resets the loop count
            return true;
        }
        return xmp_seek_time(m_ctx, static_cast<int>(std::min<uint64_t>(frame * 1000 / Rate, INT_MAX))) >= 0;
    }

private:
    static const int Rate = 48000;
    std::shared_ptr<AudioSource> m_source;
    Cursor m_cursor;        // libxmp copies what it needs while loading
    xmp_context m_ctx;
    bool m_bLoaded, m_bEnd;
};

// size and PCM frames of an MPEG audio frame from its 4-byte header; false if it is none (or free format)
bool MpegFrame(const uint8_t *h, uint32_t &bytes, uint32_t &samples)
{
    if ((h[0] != 0xFF) || ((h[1] & 0xE0) != 0xE0))
        return false;
    const int version = (h[1] >> 3) & 3;        // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
    const int layer = 4 - ((h[1] >> 1) & 3);    // 1, 2, 3; 4 is reserved
    const int bitrateIndex = h[2] >> 4;
    const int rateIndex = (h[2] >> 2) & 3;
    if ((version == 1) || (layer == 4) || (bitrateIndex == 0) || (bitrateIndex == 15) || (rateIndex == 3))
        return false;
    static const uint16_t kbps[2][3][15] = {
        { { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
          { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },
        { { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
          { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 } } };
    static const uint32_t rates[3] = { 44100, 48000, 32000 };
    const bool bMpeg1 = (version == 3);
    const uint32_t rate = rates[rateIndex] >> (bMpeg1 ? 0 : ((version == 2) ? 1 : 2));
    const uint32_t bitrate = kbps[bMpeg1 ? 0 : 1][layer - 1][bitrateIndex] * 1000u;
    const uint32_t padding = (h[2] >> 1) & 1;
    if (layer == 1)
    {
        samples = 384;
        bytes = (12 * bitrate / rate + padding) * 4;
    }
    else
    {
        samples = ((layer == 3) && !bMpeg1) ? 576 : 1152;
        bytes = samples / 8 * bitrate / rate + padding;
    }
    return bytes >= 4;
}

class Mp3Stream : public AudioStream
{
public:
    Mp3Stream(const std::shared_ptr<AudioSource> &source, const uint64_t base) :
        m_source(source), m_start(source.get(), base), m_cursor(source.get(), base), m_bOpen(false), m_delay(0), m_frameSamples(0), m_pos(0) { }

    ~Mp3Stream() override
    {
        if (m_bOpen)
            drmp3_uninit(&m_mp3);
    }

    // the length comes from the Xing/Info header or a scan of the frame headers; an index of the frames makes seeks fast
    bool Open()
    {
        m_bOpen = drmp3_init(&m_mp3, DrRead, Mp3Seek, Mp3Tell, nullptr, &m_cursor, nullptr);
        if (!m_bOpen)
            return false;
        channels = m_mp3.channels;
        sampleRate = m_mp3.sampleRate;
        m_delay = m_mp3.delayInPCMFrames;
        const uint64_t tagged = m_mp3.totalPCMFrameCount;     // with a Xing/Info header: all frames' PCM, delay and padding included
        frames = drmp3_get_pcm_frame_count(&m_mp3);
        const uint64_t all = (tagged != DRMP3_UINT64_MAX) ? tagged : frames;
        Index(m_mp3.streamStartOffset);
        if (m_index.size() * m_frameSamples != all)
            m_index.clear();    // the index and dr_mp3 disagree: seeks decode from the start instead
        return !m_cursor.bFailed;
    }

    uint64_t Read(float *pOut, const uint64_t frameCount) override
    {
        // a decoder opened mid-file knows no padding at the end: the length stops it
        const uint64_t want = (frames > 0) ? std::min(frameCount, (m_pos < frames) ? frames - m_pos : 0) : frameCount;
        const uint64_t n = ((want > 0) && m_bOpen) ? drmp3_read_pcm_frames_f32(&m_mp3, want, pOut) : 0;
        m_pos += n;
        return n;
    }

    // dr_mp3 0.7.3's seek table lands off by up to a few frames, so seeks open a decoder a few MP3 frames before the target
    bool Seek(const uint64_t frame) override
    {
        if ((frames > 0) && (frame >= frames))
        {
            m_pos = frames;
            return frame == frames;
        }
        const uint64_t raw = frame + m_delay;   // PCM frames from the first audio frame, encoder delay included
        uint64_t first = 0, drops = 0;
        if (!m_index.empty() && (raw / m_frameSamples < m_index.size()))
        {
            // two decoded frames before the target's fill the overlap and the synthesis filter; from the start if that fails
            const uint64_t k = raw / m_frameSamples;
            for (uint64_t lead = 4; ; lead *= 2)
            {
                first = (k > lead) ? k - lead : 0;
                if (first == 0)
                    break;
                drops = Drops(first, k);
                if (first + drops + 2 <= k)
                    break;
                if (lead >= 64)
                {
                    first = 0;
                    break;
                }
            }
        }
        if (m_bOpen)
            drmp3_uninit(&m_mp3);
        m_cursor = (first > 0) ? Cursor(m_source.get(), m_start.base + m_index[first]) : m_start;
        m_bOpen = drmp3_init(&m_mp3, DrRead, Mp3Seek, Mp3Tell, nullptr, &m_cursor, nullptr);
        if (!m_bOpen)
            return false;
        // from the start dr_mp3 skips the encoder delay itself; mid-file it counts raw frames from the first one it decodes
        const uint64_t skip = (first > 0) ? raw - (first + drops) * m_frameSamples : frame;
        m_pos = frame;
        return (skip == 0) || (drmp3_read_pcm_frames_s16(&m_mp3, skip, nullptr) == skip);  // s16 is dr_mp3's own format: it can decode into nothing
    }

private:
    // frames a decoder started at 'first' drops before 'last' (Layer III bit reservoir: main data in earlier frames)
    uint64_t Drops(const uint64_t first, const uint64_t last) const
    {
        uint32_t reservoir = 0;
        for (uint64_t j = first; j <= last; j++)
        {
            uint8_t h[8];
            uint32_t bytes, samples;
            if ((m_source->Read(m_start.base + m_index[j], h, sizeof(h)) != static_cast<int64_t>(sizeof(h))) || !MpegFrame(h, bytes, samples))
                return j - first;
            if (((h[1] >> 1) & 3) != 1)
                return 0;   // Layer I and II have no reservoir
            const bool bMpeg1 = ((h[1] >> 3) & 3) == 3;
            const bool bMono = (h[3] >> 6) == 3;
            const uint32_t crc = (h[1] & 1) ? 0 : 2;
            const uint32_t side = bMpeg1 ? (bMono ? 17 : 32) : (bMono ? 9 : 17);
            const uint8_t *p = h + 4 + crc;
            const uint32_t begin = bMpeg1 ? ((p[0] << 1) | (p[1] >> 7)) : p[0];   // main_data_begin: 9 bits, 8 for MPEG-2
            if (begin <= reservoir)
                return j - first;   // this one decodes
            const uint32_t main = (bytes > 4 + crc + side) ? bytes - 4 - crc - side : 0;
            reservoir = std::min<uint32_t>(511, std::min(reservoir, begin) + main);
        }
        return last - first + 1;
    }

    // the byte offset of every frame from the first audio frame, read in 64 KB blocks
    void Index(const uint64_t start)
    {
        const uint64_t size = m_start.Size();
        if (size > UINT32_MAX)
            return;
        std::vector<uint8_t> block(65536);
        uint64_t blockStart = 0, blockBytes = 0;
        uint64_t pos = start;
        while (pos + 4 <= size)
        {
            if ((pos < blockStart) || (pos + 4 > blockStart + blockBytes))
            {
                blockStart = pos;
                const int64_t n = m_source->Read(m_start.base + pos, block.data(), block.size());
                if (n < 4)
                    break;
                blockBytes = static_cast<uint64_t>(n);
            }
            uint32_t bytes, samples;
            if (!MpegFrame(block.data() + (pos - blockStart), bytes, samples) || (m_frameSamples && (samples != m_frameSamples)))
                break;
            m_frameSamples = samples;
            m_index.push_back(static_cast<uint32_t>(pos));
            pos += bytes;
        }
    }

    std::shared_ptr<AudioSource> m_source;
    Cursor m_start;         // the file from its first byte (after ID3v2 tags)
    Cursor m_cursor;        // dr_mp3 reads through it
    drmp3 m_mp3;
    bool m_bOpen;
    uint32_t m_delay;       // encoder delay from the LAME tag
    uint32_t m_frameSamples;
    uint64_t m_pos;         // next frame Read gives
    std::vector<uint32_t> m_index;
};

class FlacStream : public AudioStream
{
public:
    FlacStream(const std::shared_ptr<AudioSource> &source, const uint64_t base) : m_source(source), m_cursor(source.get(), base), m_pFlac(nullptr) { }

    ~FlacStream() override
    {
        drflac_close(m_pFlac);
    }

    bool Open()
    {
        m_pFlac = drflac_open(DrRead, FlacSeek, FlacTell, &m_cursor, nullptr);
        if (!m_pFlac)
            return false;
        channels = m_pFlac->channels;
        sampleRate = m_pFlac->sampleRate;
        frames = m_pFlac->totalPCMFrameCount;
        return true;
    }

    uint64_t Read(float *pOut, const uint64_t frameCount) override
    {
        return drflac_read_pcm_frames_f32(m_pFlac, frameCount, pOut);
    }

    bool Seek(const uint64_t frame) override
    {
        return drflac_seek_to_pcm_frame(m_pFlac, frame) != 0;
    }

private:
    std::shared_ptr<AudioSource> m_source;
    Cursor m_cursor;        // dr_flac reads through it
    drflac *m_pFlac;
};
}

AudioSource::~AudioSource()
{
    if (m_fd >= 0)
        close(m_fd);
}

std::shared_ptr<AudioSource> AudioSource::Open(const char *pPath, const std::atomic<bool> *pStop, std::string &error, bool &bTransient)
{
    bTransient = false;
    const int fd = open(pPath, O_RDONLY | O_CLOEXEC | O_NONBLOCK);    // a FIFO must not block the open (refused below); regular files ignore the flag
    if (fd < 0)
    {
        const int e = errno;
        error = std::string("can't open the file: ") + strerror(e);
        bTransient = IsTransient(e);
        return nullptr;
    }
    struct stat st;
    if ((fstat(fd, &st) != 0) || !S_ISREG(st.st_mode))
    {
        close(fd);
        error = "can't read the file: not a regular file";
        return nullptr;
    }
    std::shared_ptr<AudioSource> source(new AudioSource());
    source->m_fd = fd;
    source->m_size = static_cast<uint64_t>(st.st_size);
    source->m_pStop = pStop;
    return source;
}

int64_t AudioSource::Read(const uint64_t offset, void *pOut, const uint64_t count) const
{
    if (m_pStop && *m_pStop)
        return -1;      // the engine is shutting down: every decoder gives up at once
    if (offset >= m_size)
        return 0;
    const uint64_t want = std::min(count, m_size - offset);
    uint64_t done = 0;
    while (done < want)
    {
        const ssize_t n = pread(m_fd, static_cast<char *>(pOut) + done, want - done, static_cast<off_t>(offset + done));
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            m_bReadFailed = true;
            return -1;
        }
        if (n == 0)
            break;      // the file got shorter
        done += static_cast<uint64_t>(n);
    }
    return static_cast<int64_t>(done);
}

bool AudioDecode::Probe(const AudioSource &source, AudioProbe &probe, std::string &error, bool &bTransient)
{
    bTransient = false;
    probe = AudioProbe();
    uint8_t h[16];
    int64_t n = 0;
    bool bTagged = false;
    for (int tags = 0; ; tags++)
    {
        memset(h, 0, sizeof(h));
        n = source.Read(probe.base, h, sizeof(h));
        if (n < 0)
        {
            error = "can't read the file";
            bTransient = true;
            return false;
        }
        // ID3v2 in front of any format: 10 bytes, a syncsafe size, 10 more with a footer; detect again behind it
        const bool bId3 = (n >= 10) && !memcmp(h, "ID3", 3) && (h[3] != 0xFF) && (h[4] != 0xFF) && ((h[6] | h[7] | h[8] | h[9]) < 0x80);
        if (!bId3 || (tags == 4))
            break;
        const uint64_t size = (static_cast<uint64_t>(h[6]) << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
        probe.base += 10 + size + ((h[5] & 0x10) ? 10 : 0);
        bTagged = true;
        if (probe.base >= source.Size())
        {
            error = "sound file with only ID3 tags";
            return false;
        }
    }

    // the magic bytes decide the format, not the extension
    if ((n >= 12) && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4))
    {
        probe.format = AudioFormat::Wav;
        return ParseWav(source, probe, error, bTransient);
    }
    if ((n >= 4) && !memcmp(h, "OggS", 4))
        probe.format = AudioFormat::Ogg;
    else if ((n >= 4) && !memcmp(h, "fLaC", 4))
        probe.format = AudioFormat::Flac;
    else if ((n >= 3) && !memcmp(h, "ID3", 3))
        probe.format = AudioFormat::Mp3;
    else if ((n >= 2) && (h[0] == 0xFF) && ((h[1] & 0xE0) == 0xE0))   // MPEG audio frame sync
        probe.format = AudioFormat::Mp3;
    else
    {
        Cursor cursor(&source, probe.base);
        if ((n > 0) && (xmp_test_module_from_callbacks(&cursor, XmpCallbacks(), nullptr) == 0))   // tracker formats have no magic at the start
            probe.format = AudioFormat::Module;
        else if (cursor.bFailed)
        {
            error = "can't read the file";
            bTransient = true;
            return false;
        }
        else if (bTagged)
            probe.format = AudioFormat::Mp3;    // after the tags dr_mp3 looks for frame syncs, as before
        else
        {
            error = "unknown sound file format (WAV, OGG Vorbis, MP3, FLAC and MOD, S3M, XM, IT modules are supported)";
            return false;
        }
    }
    return true;
}

std::unique_ptr<AudioStream> AudioDecode::OpenStream(const std::shared_ptr<AudioSource> &source, const AudioProbe &probe, std::string &error)
{
    std::unique_ptr<AudioStream> stream;
    switch (probe.format)
    {
    case AudioFormat::Wav:
        stream.reset(new WavStream(source, probe));
        break;

    case AudioFormat::Ogg:
    {
        std::unique_ptr<OggStream> ogg(new OggStream(source, probe.base));
        if (ogg->Open(error))
            stream = std::move(ogg);
        break;
    }

    case AudioFormat::Mp3:
    {
        std::unique_ptr<Mp3Stream> mp3(new Mp3Stream(source, probe.base));
        if (mp3->Open())
            stream = std::move(mp3);
        else
            error = "broken MP3 data";
        break;
    }

    case AudioFormat::Flac:
    {
        std::unique_ptr<FlacStream> flac(new FlacStream(source, probe.base));
        if (flac->Open())
            stream = std::move(flac);
        else
            error = "broken FLAC data";
        break;
    }

    case AudioFormat::Module:
    {
        std::unique_ptr<ModStream> mod(new ModStream(source, probe.base));
        if (mod->Open(error))
            stream = std::move(mod);
        break;
    }

    default:
        error = "unknown sound file format (WAV, OGG Vorbis, MP3, FLAC and MOD, S3M, XM, IT modules are supported)";
        break;
    }

    if (stream && ((stream->channels == 0) || (stream->sampleRate == 0)))
    {
        error = "sound file without channels or sample rate";
        stream.reset();
    }
    else if (stream && (stream->channels > 32))
    {
        error = "sound file with more than 32 channels";   // a corrupt header; buffers are sized by the channel count
        stream.reset();
    }
    return stream;
}

std::shared_ptr<AudioBuffer> AudioDecode::Decode(AudioStream &stream, const size_t maxBytes, std::string &error, bool &bTooLong)
{
    bTooLong = false;
    std::shared_ptr<AudioBuffer> buffer = std::make_shared<AudioBuffer>();
    buffer->channels = OutChannels(stream.channels);
    buffer->sampleRate = stream.sampleRate;
    if (stream.frames > 0)
        buffer->samples.reserve(std::min<uint64_t>(stream.frames * buffer->channels, maxBytes / sizeof(float) + buffer->channels));  // the header's length is only a hint

    const uint64_t chunk = 16384;
    std::vector<float> in(chunk * stream.channels);
    for (;;)
    {
        const uint64_t n = stream.Read(in.data(), chunk);
        if (n == 0)
            break;
        const size_t used = buffer->samples.size();
        if ((used + n * buffer->channels) * sizeof(float) > maxBytes)
        {
            bTooLong = true;    // longer than its header said: the caller streams it instead
            return nullptr;
        }
        buffer->samples.resize(used + n * buffer->channels);
        Fold(in.data(), stream.channels, n, buffer->samples.data() + used);
    }
    buffer->samples.shrink_to_fit();
    buffer->frames = buffer->samples.size() / buffer->channels;
    if (buffer->frames == 0)
    {
        error = "sound file without samples";
        return nullptr;
    }
    return buffer;
}

void AudioDecode::Fold(const float *p, const uint32_t channels, const uint64_t frames, float *pOut)
{
    if (channels <= 2)
    {
        memcpy(pOut, p, frames * channels * sizeof(float));
        return;
    }
    // more channels fold into two: centre and surrounds at -3 dB, LFE out
    const float k = 0.7071f;
    for (uint64_t i = 0; i < frames; i++, p += channels)
    {
        float left = p[0];
        float right = p[1];
        switch (channels)
        {
        case 3: left += k * p[2]; right += k * p[2]; break;                                         // FL FR FC
        case 4: left += k * p[2]; right += k * p[3]; break;                                         // FL FR BL BR
        case 5: left += k * (p[2] + p[3]); right += k * (p[2] + p[4]); break;                       // FL FR FC BL BR
        case 6: left += k * (p[2] + p[4]); right += k * (p[2] + p[5]); break;                       // FL FR FC LFE BL BR
        case 7: left += k * (p[2] + p[5]) + 0.5f * p[4]; right += k * (p[2] + p[6]) + 0.5f * p[4]; break; // FL FR FC LFE BC SL SR
        default: left += k * (p[2] + p[4] + p[6]); right += k * (p[2] + p[5] + p[7]); break;         // FL FR FC LFE BL BR SL SR
        }
        pOut[2 * i] = left;
        pOut[2 * i + 1] = right;
    }
}
