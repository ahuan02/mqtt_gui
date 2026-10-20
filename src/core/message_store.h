#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct MessageEntry {
    std::string clock;      // 12:34:56.789
    std::string topic;
    std::string payload;    // 已做可见化处理，保留换行/制表
    std::string preview;    // 单行摘要，供表格直接绘制
    uint64_t    seq{0};
    uint32_t    size{0};    // 原始负载字节数
    uint16_t    packet_id{0};
    uint8_t     qos{0};
    bool        outbound{false};
    bool        retain{false};
    bool        truncated{false};
};

// 固定容量环形缓冲：超出容量直接覆盖最旧一条，全程零额外分配。
class MessageStore {
public:
    static constexpr size_t kCapacity   = 1000;
    static constexpr size_t kMaxPayload = 64 * 1024;
    static constexpr size_t kMaxPreview = 200;

    MessageStore() {
        buffer_.resize(kCapacity);
        view_.reserve(kCapacity);
    }

    void push(MessageEntry entry) {
        const size_t capacity = buffer_.size();
        if (count_ < capacity) {
            buffer_[(head_ + count_) % capacity] = std::move(entry);
            ++count_;
        } else {
            buffer_[head_] = std::move(entry);
            head_ = (head_ + 1) % capacity;
        }
        ++version_;
    }

    size_t size() const { return count_; }
    size_t capacity() const { return buffer_.size(); }
    uint64_t version() const { return version_; }

    const MessageEntry& at(size_t index) const {
        return buffer_[(head_ + index) % buffer_.size()];
    }

    void clear() {
        head_ = 0;
        count_ = 0;
        ++version_;
        view_.clear();
        view_version_ = 0;
        filter_cache_.clear();
        direction_cache_ = -1;
    }

    const MessageEntry* find(uint64_t seq) const {
        for (size_t i = 0; i < count_; ++i) {
            if (buffer_[(head_ + i) % buffer_.size()].seq == seq) return &buffer_[(head_ + i) % buffer_.size()];
        }
        return nullptr;
    }

    // 过滤视图：只在过滤条件或数据版本变化时重建，渲染时零成本
    const std::vector<uint32_t>& view(std::string_view filter, int direction) {
        if (direction != direction_cache_ || version_ != view_version_ || filter != filter_cache_) {
            rebuild(filter, direction);
            filter_cache_.assign(filter);
            direction_cache_ = direction;
            view_version_ = version_;
        }
        return view_;
    }

    static std::string make_preview(std::string_view payload) {
        std::string out;
        out.reserve(payload.size() < kMaxPreview ? payload.size() : kMaxPreview + 3);

        size_t index = 0;
        for (; index < payload.size() && out.size() < kMaxPreview; ++index) {
            const auto byte = static_cast<unsigned char>(payload[index]);
            if (byte == '\n' || byte == '\r' || byte == '\t' || byte == ' ') out.push_back(' ');
            else if (byte < 0x20 || byte == 0x7F) out.push_back('.');
            else out.push_back(static_cast<char>(byte));
        }
        if (index < payload.size()) out += "...";
        return out;
    }

    // 二进制安全：保留可读字符与换行，其余控制字符替换掉，避免渲染异常
    static std::string sanitize(std::string_view raw) {
        std::string out;
        out.reserve(raw.size());
        for (const char ch : raw) {
            const auto byte = static_cast<unsigned char>(ch);
            if (byte == '\n' || byte == '\r' || byte == '\t') out.push_back(ch);
            else if (byte < 0x20 || byte == 0x7F) out.push_back('.');
            else out.push_back(ch);
        }
        return out;
    }

private:
    static bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
        if (needle.empty()) return true;
        if (needle.size() > haystack.size()) return false;

        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        const size_t last = haystack.size() - needle.size();
        for (size_t i = 0; i <= last; ++i) {
            size_t j = 0;
            while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
            if (j == needle.size()) return true;
        }
        return false;
    }

    void rebuild(std::string_view filter, int direction) {
        view_.clear();
        for (size_t i = 0; i < count_; ++i) {
            const MessageEntry& entry = at(i);
            if (direction == 1 && entry.outbound) continue;
            if (direction == 2 && !entry.outbound) continue;
            if (!filter.empty() && !contains_ignore_case(entry.topic, filter) &&
                !contains_ignore_case(entry.preview, filter))
                continue;
            view_.push_back(static_cast<uint32_t>(i));
        }
    }

    std::vector<MessageEntry> buffer_;
    size_t                    head_{0};
    size_t                    count_{0};
    uint64_t                  version_{0};

    std::vector<uint32_t> view_;
    std::string           filter_cache_;
    int                   direction_cache_{-1};
    uint64_t              view_version_{0};
};
