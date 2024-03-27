//  Copyright (c) 2022-2024 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef AUDIOSOURCE_H
#define AUDIOSOURCE_H

#include "bsshared.h"
#include <cstdint>
#include <stdexcept>
#include <list>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <array>
#include <memory>



struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;

class AudioException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct AudioProperties {
    bool IsFloat;
    int BytesPerSample;
    int BitsPerSample;
    int SampleRate;
    int Channels;
    uint64_t ChannelLayout;
    int64_t NumFrames; // can be -1 to signal that the number of frames is completely unknown
    int64_t NumSamples; /* estimated by decoder, may be wrong */
    double StartTime; /* in seconds */
};

struct LWAudioDecoder {
private:
    AVFormatContext *FormatContext = nullptr;
    AVCodecContext *CodecContext = nullptr;
    AVFrame *DecodeFrame = nullptr;
    int64_t CurrentFrame = 0;
    int64_t CurrentSample = 0;
    int TrackNumber = -1;
    bool DecodeSuccess = true;
    AVPacket *Packet = nullptr;
    bool ResendPacket = false;
    bool Seeked = false;

    void OpenFile(const std::string &SourceFile, int Track, bool VariableFormat, int Threads, const std::map<std::string, std::string> &LAVFOpts, double DrcScale);
    bool ReadPacket();
    bool DecodeNextFrame(bool SkipOutput = false);
    void Free();
public:
    LWAudioDecoder(const std::string &SourceFile, int Track, bool VariableFormat, int Threads, const std::map<std::string, std::string> &LAVFOpts, double DrcScale); // Positive track numbers are absolute. Negative track numbers mean nth audio track to simplify things.
    ~LWAudioDecoder();
    [[nodiscard]] int64_t GetSourceSize() const;
    [[nodiscard]] int64_t GetSourcePostion() const;
    [[nodiscard]] int GetTrack() const; // Useful when opening nth video track to get the actual number
    [[nodiscard]] int64_t GetFrameNumber() const; // The frame you will get when calling GetNextFrame()
    [[nodiscard]] int64_t GetSamplePos() const; // The frame you will get when calling GetNextFrame()
    void SetFrameNumber(int64_t N, int64_t SampleNumber); // Use after seeking to update internal frame number
    void GetAudioProperties(AudioProperties &VP); // Decodes one frame and advances the position to retrieve the full properties, only call directly after creation
    [[nodiscard]] AVFrame *GetNextFrame();
    bool SkipFrames(int64_t Count);
    [[nodiscard]] bool HasMoreFrames() const;
    [[nodiscard]] bool Seek(int64_t PTS); // Note that the current frame number isn't updated and if seeking fails the decoder is in an undefined state
    [[nodiscard]] bool HasSeeked() const;
};


class BestAudioFrame {
private:
    AVFrame *Frame;
public:
    BestAudioFrame(AVFrame *Frame);
    ~BestAudioFrame();
    [[nodiscard]] const AVFrame *GetAVFrame() const;

    int64_t Pts;
    int64_t NumSamples;
};

class BestAudioSource {
private:
    struct AudioTrackIndex {
        struct FrameInfo {
            int64_t PTS;
            int64_t Start;
            int64_t Length;
            std::array<uint8_t, 16> Hash;
        };

        std::vector<FrameInfo> Frames;
    };

    bool WriteAudioTrackIndex(const std::string &CachePath);
    bool ReadAudioTrackIndex(const std::string &CachePath);

    class Cache {
    private:
        class CacheBlock {
        public:
            int64_t FrameNumber;
            AVFrame *Frame;
            size_t Size = 0;
            CacheBlock(int64_t FrameNumber, AVFrame *Frame);
            ~CacheBlock();
        };

        size_t Size = 0;
        size_t MaxSize = 1024 * 1024 * 1024;
        std::list<CacheBlock> Data;
        void ApplyMaxSize();
    public:
        void Clear();
        void SetMaxSize(size_t Bytes);
        void CacheFrame(int64_t FrameNumber, AVFrame *Frame); // Takes ownership of Frame
        [[nodiscard]] BestAudioFrame *GetFrame(int64_t N);
    };

    AudioTrackIndex TrackIndex;
    Cache FrameCache;

    static constexpr int MaxVideoSources = 4;
    std::map<std::string, std::string> LAVFOptions;
    double DrcScale;
    AudioProperties AP = {};
    std::string Source;
    int AudioTrack;
    bool VariableFormat;
    int Threads;
    bool LinearMode = false;
    uint64_t DecoderSequenceNum = 0;
    uint64_t DecoderLastUse[MaxVideoSources] = {};
    std::unique_ptr<LWAudioDecoder> Decoders[MaxVideoSources];
    int64_t PreRoll = 40;
    int64_t SampleDelay = 0;
    static constexpr size_t RetrySeekAttempts = 10;
    std::set<int64_t> BadSeekLocations;
    void SetLinearMode();
    [[nodiscard]] int64_t GetSeekFrame(int64_t N);
    [[nodiscard]] BestAudioFrame *SeekAndDecode(int64_t N, int64_t SeekFrame, std::unique_ptr<LWAudioDecoder> &Decoder, size_t Depth = 0);
    [[nodiscard]] BestAudioFrame *GetFrameInternal(int64_t N);
    [[nodiscard]] BestAudioFrame *GetFrameLinearInternal(int64_t N, int64_t SeekFrame = -1, size_t Depth = 0, bool ForceUnseeked = false);
    [[nodiscard]] bool IndexTrack(const std::function<void(int Track, int64_t Current, int64_t Total)> &Progress = nullptr);
    bool InitializeRFF();
    void ZeroFillStartPacked(uint8_t *Data, int64_t &Start, int64_t &Count);
    void ZeroFillEndPacked(uint8_t *Data, int64_t Start, int64_t &Count);
    bool FillInFramePacked(const BestAudioFrame *Frame, int64_t FrameStartSample, uint8_t *Data, int64_t &Start, int64_t &Count);
    void ZeroFillStartPlanar(uint8_t *Data[], int64_t &Start, int64_t &Count);
    void ZeroFillEndPlanar(uint8_t *Data[], int64_t Start, int64_t &Count);
    bool FillInFramePlanar(const BestAudioFrame *Frame, int64_t FrameStartSample, uint8_t *Data[], int64_t &Start, int64_t &Count);
public:
    struct FrameRange {
        int64_t First;
        int64_t Last;
        int64_t FirstSamplePos;
    };

    BestAudioSource(const std::string &SourceFile, int Track, int AjustDelay, bool VariableFormat, int Threads, const std::string &CachePath, const std::map<std::string, std::string> *LAVFOpts, double DrcScale, const std::function<void(int Track, int64_t Current, int64_t Total)> &Progress = nullptr);
    [[nodiscard]] int GetTrack() const; // Useful when opening nth video track to get the actual number
    void SetMaxCacheSize(size_t Bytes); /* default max size is 1GB */
    void SetSeekPreRoll(int64_t Frames); /* the number of frames to cache before the position being fast forwarded to */
    double GetRelativeStartTime(int Track) const;
    [[nodiscard]] const AudioProperties &GetAudioProperties() const;
    [[nodiscard]] BestAudioFrame *GetFrame(int64_t N, bool Linear = false);
    [[nodiscard]] FrameRange GetFrameRangeBySamples(int64_t Start, int64_t Count) const;
    void GetPackedAudio(uint8_t *Data, int64_t Start, int64_t Count);
    void GetPlanarAudio(uint8_t *const *const Data, int64_t Start, int64_t Count);
};

#endif
