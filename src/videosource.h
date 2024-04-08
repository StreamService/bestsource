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

#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include "bsshared.h"
#include <cstdint>
#include <list>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <array>
#include <memory>

struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;
struct AVPixFmtDescriptor;

struct VideoFormat {
    int ColorFamily; /* Unknown = 0, Gray = 1, RGB = 2, YUV = 3 */
    bool Alpha;
    bool Float;
    int Bits;
    int SubSamplingW;
    int SubSamplingH;

    void Set(const AVPixFmtDescriptor *Desc);
};

struct VideoProperties {
    BSRational TimeBase;
    double StartTime; // in seconds
    int64_t Duration;
    int64_t NumFrames; // can be -1 to signal that the number of frames is completely unknown, RFF ignored
    int64_t NumRFFFrames; // can be -1 to signal that the number of frames is completely unknown, RFF applied

    BSRational FPS;
    BSRational SAR;

    VideoFormat VF;
    int Width;
    int Height;

    /* Height and width but largest possible size where it's a multiple of the subsampling */
    int SSModWidth;
    int SSModHeight; 

    bool FieldBased;
    bool TFF;

    /* Stereo 3D */
    int Stereo3DType;
    int Stereo3DFlags;

    /* MasteringDisplayPrimaries */
    bool HasMasteringDisplayPrimaries;
    BSRational MasteringDisplayPrimaries[3][2];
    BSRational MasteringDisplayWhitePoint[2];

    /* MasteringDisplayLuminance */
    bool HasMasteringDisplayLuminance;
    BSRational MasteringDisplayMinLuminance;
    BSRational MasteringDisplayMaxLuminance;

    /* ContentLightLevel */
    bool HasContentLightLevel;
    unsigned ContentLightLevelMax;
    unsigned ContentLightLevelAverage;

    /* Orientation */
    bool FlipVerical;
    bool FlipHorizontal;
    int Rotation; /* A positive number in degrees */
};

struct LWVideoDecoder {
private:
    AVFormatContext *FormatContext = nullptr;
    AVCodecContext *CodecContext = nullptr;
    AVBufferRef *HWDeviceContext = nullptr;
    AVFrame *DecodeFrame = nullptr;
    AVFrame *HWFrame = nullptr;
    int64_t CurrentFrame = 0;
    int TrackNumber = -1;
    bool HWMode = false;
    bool DecodeSuccess = true;
    AVPacket *Packet = nullptr;
    bool Seeked = false;

    void OpenFile(const std::filesystem::path &SourceFile, const std::string &HWDeviceName, int ExtraHWFrames, int Track, bool VariableFormat, int Threads, const std::map<std::string, std::string> &LAVFOpts);
    bool ReadPacket();
    bool DecodeNextFrame(bool SkipOutput = false);
    void Free();
public:
    LWVideoDecoder(const std::filesystem::path &SourceFile, const std::string &HWDeviceName, int ExtraHWFrames, int Track, bool VariableFormat, int Threads, const std::map<std::string, std::string> &LAVFOpts); // Positive track numbers are absolute. Negative track numbers mean nth audio track to simplify things.
    ~LWVideoDecoder();
    [[nodiscard]] int64_t GetSourceSize() const;
    [[nodiscard]] int64_t GetSourcePostion() const;
    [[nodiscard]] int GetTrack() const; // Useful when opening nth video track to get the actual number
    [[nodiscard]] int64_t GetFrameNumber() const; // The frame you will get when calling GetNextFrame()
    void SetFrameNumber(int64_t N); // Use after seeking to update internal frame number
    void GetVideoProperties(VideoProperties &VP); // Decodes one frame and advances the position to retrieve the full properties, only call directly after creation
    [[nodiscard]] AVFrame *GetNextFrame();
    bool SkipFrames(int64_t Count);
    [[nodiscard]] bool HasMoreFrames() const;
    [[nodiscard]] bool Seek(int64_t PTS); // Note that the current frame number isn't updated and if seeking fails the decoder is in an undefined state
    [[nodiscard]] bool HasSeeked() const;
};


class BestVideoFrame {
private:
    AVFrame *Frame;
public:
    BestVideoFrame(AVFrame *Frame);
    ~BestVideoFrame();
    [[nodiscard]] const AVFrame *GetAVFrame() const;
    void MergeField(bool Top, const BestVideoFrame *FieldSrc); // Useful for RFF and other such things where fields from multiple decoded frames need to be combined, retains original frame's properties
    bool ExportAsPlanar(uint8_t **Dsts, ptrdiff_t *Stride, uint8_t *AlphaDst = nullptr, ptrdiff_t AlphaStride = 0) const;

    VideoFormat VF;
    int Width;
    int Height;

    /* Height and width but largest possible size where it's a multiple of the subsampling */
    int SSModWidth;
    int SSModHeight;

    int64_t PTS;
    int64_t Duration;
    int Matrix;
    int Primaries;
    int Transfer;
    int ChromaLocation;
    int ColorRange;

    bool InterlacedFrame;
    bool TopFieldFirst;
    char PictType;
    bool KeyFrame;
    int RepeatPict;

    /* MasteringDisplayPrimaries */
    bool HasMasteringDisplayPrimaries = false;
    BSRational MasteringDisplayPrimaries[3][2] = {};
    BSRational MasteringDisplayWhitePoint[2] = {};

    /* MasteringDisplayLuminance */
    bool HasMasteringDisplayLuminance = false;
    BSRational MasteringDisplayMinLuminance = {};
    BSRational MasteringDisplayMaxLuminance = {};

    /* ContentLightLevel */
    bool HasContentLightLevel = false;
    unsigned ContentLightLevelMax = 0;
    unsigned ContentLightLevelAverage = 0;

    /* DolbyVisionRPU */
    uint8_t *DolbyVisionRPU = nullptr;
    size_t DolbyVisionRPUSize = 0;

    /* HDR10Plus */
    uint8_t *HDR10Plus = nullptr;
    size_t HDR10PlusSize = 0;

    /* ICC Profile */
    uint8_t *ICCProfile = nullptr;
    size_t ICCProfileSize = 0;
};

class BestVideoSource {
public:
    struct FrameInfo {
        int64_t PTS;
        int RepeatPict;
        bool KeyFrame;
        bool TFF;
        std::array<uint8_t, HashSize> Hash;
    };
private:
    struct VideoTrackIndex {
        int64_t LastFrameDuration;
        std::vector<FrameInfo> Frames;
    };

    bool WriteVideoTrackIndex(const std::filesystem::path &CachePath);
    bool ReadVideoTrackIndex(const std::filesystem::path &CachePath);

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
        [[nodiscard]] BestVideoFrame *GetFrame(int64_t N);
    };

    VideoTrackIndex TrackIndex;
    Cache FrameCache;

    enum class RFFStateEnum {
        Uninitialized, Ready, Unused
    };

    RFFStateEnum RFFState = RFFStateEnum::Uninitialized;
    std::vector<std::pair<int64_t, int64_t>> RFFFields;

    static constexpr int MaxVideoSources = 4;
    std::map<std::string, std::string> LAVFOptions;
    VideoProperties VP = {};
    std::filesystem::path Source;
    std::string HWDevice;
    int ExtraHWFrames;
    int VideoTrack;
    bool VariableFormat;
    int Threads;
    bool LinearMode = false;
    uint64_t DecoderSequenceNum = 0;
    uint64_t DecoderLastUse[MaxVideoSources] = {};
    std::unique_ptr<LWVideoDecoder> Decoders[MaxVideoSources];
    int64_t PreRoll = 20;
    int64_t FileSize = -1;
    static constexpr size_t RetrySeekAttempts = 10;
    std::set<int64_t> BadSeekLocations;
    void SetLinearMode();
    [[nodiscard]] int64_t GetSeekFrame(int64_t N);
    [[nodiscard]] BestVideoFrame *SeekAndDecode(int64_t N, int64_t SeekFrame, std::unique_ptr<LWVideoDecoder> &Decoder, size_t Depth = 0);
    [[nodiscard]] BestVideoFrame *GetFrameInternal(int64_t N);
    [[nodiscard]] BestVideoFrame *GetFrameLinearInternal(int64_t N, int64_t SeekFrame = -1, size_t Depth = 0, bool ForceUnseeked = false);
    [[nodiscard]] bool IndexTrack(const ProgressFunction &Progress = nullptr);
    bool InitializeRFF();
public:
    BestVideoSource(const std::filesystem::path &SourceFile, const std::string &HWDeviceName, int ExtraHWFrames, int Track, bool VariableFormat, int Threads, int CacheMode, const std::filesystem::path &CachePath, const std::map<std::string, std::string> *LAVFOpts, const ProgressFunction &Progress = nullptr);
    [[nodiscard]] int GetTrack() const; // Useful when opening nth video track to get the actual number
    void SetMaxCacheSize(size_t Bytes); /* default max size is 1GB */
    void SetSeekPreRoll(int64_t Frames); /* the number of frames to cache before the position being fast forwarded to */
    [[nodiscard]] const VideoProperties &GetVideoProperties() const;
    [[nodiscard]] BestVideoFrame *GetFrame(int64_t N, bool Linear = false);
    [[nodiscard]] BestVideoFrame *GetFrameWithRFF(int64_t N, bool Linear = false);
    [[nodiscard]] BestVideoFrame *GetFrameByTime(double Time, bool Linear = false);
    [[nodiscard]] bool GetFrameIsTFF(int64_t N, bool RFF = false);
    void WriteTimecodes(const std::filesystem::path &TimecodeFile) const;
    [[nodiscard]] const FrameInfo &GetFrameInfo(int64_t N) const;
    [[nodiscard]] bool GetLinearDecodingState() const;
};

#endif
