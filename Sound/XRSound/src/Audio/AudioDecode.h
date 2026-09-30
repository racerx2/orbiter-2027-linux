// not upstream: sound file decoding for XRSound's audio engine (WAV parsed here; OGG, MP3, FLAC via stb_vorbis, dr_mp3, dr_flac; MOD, S3M, XM, IT via libxmp-lite)

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// an open sound file, read with pread only so several decoders can share it; nothing is kept in memory
class AudioSource
{
public:
    ~AudioSource();

    // nullptr and an error text if the file can't be opened; bTransient: worth trying again later (EMFILE, EIO, ...)
    static std::shared_ptr<AudioSource> Open(const char *pPath, const std::atomic<bool> *pStop, std::string &error, bool &bTransient);

    // bytes read at offset, 0 at the end, -1 on a read error or once the engine stops
    int64_t Read(const uint64_t offset, void *pOut, const uint64_t count) const;

    uint64_t Size() const { return m_size; }
    bool ReadFailed() const { return m_bReadFailed; }  // a read error happened (EIO, ...): the file may read fine next time

private:
    AudioSource() { }
    int m_fd = -1;
    uint64_t m_size = 0;
    const std::atomic<bool> *m_pStop = nullptr;
    mutable std::atomic<bool> m_bReadFailed{ false };
};

// a whole sound decoded to interleaved float samples, folded to mono or stereo
struct AudioBuffer
{
    std::vector<float> samples;   // frames * channels
    uint32_t channels = 0;        // 1 or 2
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
};

// decodes a sound piece by piece from its file (long sounds such as music)
class AudioStream
{
public:
    virtual ~AudioStream() { }
    virtual uint64_t Read(float *pOut, const uint64_t frameCount) = 0;   // interleaved, the file's channels; frames read, 0 at the end
    virtual bool Seek(const uint64_t frame) = 0;

    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;    // 0 if the length is unknown
};

enum class AudioFormat { Unknown, Wav, Ogg, Flac, Mp3, Module };

// what the first bytes of a file say; WAV files are known completely from their header
struct AudioProbe
{
    AudioFormat format = AudioFormat::Unknown;
    uint64_t base = 0;              // where the sound data starts after ID3v2 tags
    uint16_t wavFormat = 0;         // 1 = integer PCM, 3 = IEEE float
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t blockAlign = 0;        // bytes per frame
    uint16_t bits = 0;
    uint64_t dataOffset = 0;        // WAV sample data in the file
    uint64_t frames = 0;
};

namespace AudioDecode
{
    // reads only the header; false and an error text if the format is unknown or the header is broken
    bool Probe(const AudioSource &source, AudioProbe &probe, std::string &error, bool &bTransient);

    // opens a decoder that reads the file on demand; nullptr and an error text on failure
    std::unique_ptr<AudioStream> OpenStream(const std::shared_ptr<AudioSource> &source, const AudioProbe &probe, std::string &error);

    // decodes a stream from where it is to its end; nullptr and bTooLong if the result would pass maxBytes
    std::shared_ptr<AudioBuffer> Decode(AudioStream &stream, const size_t maxBytes, std::string &error, bool &bTooLong);

    // channels of the folded output: 1 stays mono, the rest become stereo
    inline uint32_t OutChannels(const uint32_t channels) { return (channels == 1) ? 1 : 2; }

    // folds frames of 1 to 32 channels (WAV order FL FR FC LFE BL BR SL SR) to OutChannels
    void Fold(const float *pIn, const uint32_t channels, const uint64_t frames, float *pOut);
}
