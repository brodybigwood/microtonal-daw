#include <pluginterfaces/base/ibstream.h>    // Steinberg::IBStream
#include <cstring>                           // std::memcpy
#include <vector>                            // std::vector
#include <algorithm>                         // std::min

//gpt

using namespace Steinberg;

class VectorStream : public Steinberg::IBStream {
public:
    std::vector<uint8_t> data;
    uint64_t pos = 0;

    // IUnknown boilerplate
    tresult PLUGIN_API queryInterface(const TUID, void**) SMTG_OVERRIDE { return kNoInterface; }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return 1; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return 1; }

    // IBStream methods
    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numRead) SMTG_OVERRIDE {
        if (pos >= data.size()) {
            if (numRead) *numRead = 0;
            return kResultFalse;
        }
        uint64_t remaining = data.size() - pos;
        uint64_t toRead = std::min<uint64_t>(numBytes, remaining);
        std::memcpy(buffer, data.data() + pos, toRead);
        pos += toRead;
        if (numRead) *numRead = static_cast<int32>(toRead);
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numWritten) SMTG_OVERRIDE {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);
        if (pos + numBytes > data.size()) data.resize(pos + numBytes);
        std::memcpy(data.data() + pos, bytes, numBytes);
        pos += numBytes;
        if (numWritten) *numWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos_, int32 mode, int64* result) SMTG_OVERRIDE {
        switch (mode) {
            case kIBSeekSet: pos = pos_; break;
            case kIBSeekCur: pos += pos_; break;
            case kIBSeekEnd: pos = data.size() + pos_; break;
            default: return kResultFalse;
        }
        if (result) *result = pos;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos_) SMTG_OVERRIDE {
        if (pos_) *pos_ = pos;
        return kResultOk;
    }

    tresult PLUGIN_API flush() { return kResultOk; }
};

class VectorReadStream : public Steinberg::IBStream {
public:
    explicit VectorReadStream(const std::vector<uint8_t>& data)
    : data_(data), pos_(0) {}

    // IUnknown
    tresult PLUGIN_API queryInterface(const TUID, void**) SMTG_OVERRIDE { return kNoInterface; }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return 1; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE { return 1; }

    // IBStream methods
    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numRead) SMTG_OVERRIDE {
        if (pos_ >= data_.size()) {
            if (numRead) *numRead = 0;
            return kResultFalse;
        }
        int64_t remaining = static_cast<int64_t>(data_.size() - pos_);
        int32 toRead = static_cast<int32>(std::min<int64_t>(numBytes, remaining));
        std::memcpy(buffer, data_.data() + pos_, toRead);
        pos_ += toRead;
        if (numRead) *numRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void*, int32, int32*) SMTG_OVERRIDE {
        return kNotImplemented;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) SMTG_OVERRIDE {
        int64 newPos = 0;
        switch (mode) {
            case kIBSeekSet: newPos = pos; break;
            case kIBSeekCur: newPos = pos_ + pos; break;
            case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
            default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size()))
            return kResultFalse;
        pos_ = static_cast<size_t>(newPos);
        if (result) *result = pos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) SMTG_OVERRIDE {
        if (pos) *pos = pos_;
        return kResultOk;
    }

    tresult PLUGIN_API flush() { return kResultOk; }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_;
};
