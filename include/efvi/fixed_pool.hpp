#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT 26
#endif

#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif

#ifndef MAP_HUGE_1GB
#define MAP_HUGE_1GB (30 << MAP_HUGE_SHIFT)
#endif

namespace efvi {

enum class PageMode {
    Normal,
    Huge2M,
    Huge1G,
    Auto
};

class MappedRegion final {
public:
    MappedRegion() = default;

    MappedRegion(void* base,
                 std::size_t length,
                 bool mmaped,
                 bool hugepage_backed,
                 std::size_t page_size) noexcept
        : base_(base),
          length_(length),
          mmaped_(mmaped),
          hugepage_backed_(hugepage_backed),
          page_size_(page_size) {}

    ~MappedRegion() {
        reset();
    }

    MappedRegion(const MappedRegion&) = delete;
    MappedRegion& operator=(const MappedRegion&) = delete;

    MappedRegion(MappedRegion&& other) noexcept {
        move_from(std::move(other));
    }

    MappedRegion& operator=(MappedRegion&& other) noexcept {
        if (this != &other) {
            reset();
            move_from(std::move(other));
        }
        return *this;
    }

    [[nodiscard]] void* data() noexcept { return base_; }
    [[nodiscard]] const void* data() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return length_; }
    [[nodiscard]] bool valid() const noexcept { return base_ != nullptr; }
    [[nodiscard]] bool hugepage_backed() const noexcept { return hugepage_backed_; }
    [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }

    void reset() noexcept {
        if (base_ == nullptr) return;

        if (mmaped_) {
            (void)::munmap(base_, length_);
        } else {
            std::free(base_);
        }

        base_ = nullptr;
        length_ = 0;
        mmaped_ = false;
        hugepage_backed_ = false;
        page_size_ = 0;
    }

private:
    void move_from(MappedRegion&& other) noexcept {
        base_ = other.base_;
        length_ = other.length_;
        mmaped_ = other.mmaped_;
        hugepage_backed_ = other.hugepage_backed_;
        page_size_ = other.page_size_;

        other.base_ = nullptr;
        other.length_ = 0;
        other.mmaped_ = false;
        other.hugepage_backed_ = false;
        other.page_size_ = 0;
    }

    void* base_{nullptr};
    std::size_t length_{0};
    bool mmaped_{false};
    bool hugepage_backed_{false};
    std::size_t page_size_{0};
};

[[nodiscard]] inline std::size_t round_up(std::size_t value,
                                           std::size_t alignment) {
    if (alignment == 0 ||
        value > std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
        throw std::overflow_error("size overflow while aligning memory region");
    }

    return ((value + alignment - 1) / alignment) * alignment;
}

[[nodiscard]] inline MappedRegion map_pages(std::size_t bytes,
                                            PageMode mode,
                                            bool strict,
                                            std::size_t alignment = 64) {
    if (bytes == 0) {
        throw std::invalid_argument("cannot map a zero-byte region");
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    const long page_size_raw = ::sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        throw std::runtime_error("cannot determine Linux base page size");
    }
    const auto normal_page = static_cast<std::size_t>(page_size_raw);

    const auto try_map =
        [&](std::size_t page_size, int flags) -> MappedRegion {
            const auto length = round_up(bytes, page_size);

            void* ptr = ::mmap(nullptr,
                               length,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | flags,
                               -1,
                               0);

            if (ptr == MAP_FAILED) {
                return {};
            }

            return MappedRegion(ptr, length, true, true, page_size);
        };

    const bool needs_1g =
        mode == PageMode::Huge1G ||
        (mode == PageMode::Auto && bytes >= (1ULL << 30));

    const bool needs_2m =
        mode == PageMode::Huge2M ||
        (mode == PageMode::Auto && bytes >= (2ULL << 20));

    if (needs_1g) {
        auto region = try_map(1ULL << 30, MAP_HUGETLB | MAP_HUGE_1GB);
        if (region.valid()) return region;

        if (mode == PageMode::Huge1G && strict) {
            throw std::runtime_error(
                "1 GiB HUGETLB mapping failed; provision 1 GiB huge pages first");
        }
    }

    if (needs_2m) {
        auto region = try_map(2ULL << 20, MAP_HUGETLB | MAP_HUGE_2MB);
        if (region.valid()) return region;

        if (mode == PageMode::Huge2M && strict) {
            throw std::runtime_error(
                "2 MiB HUGETLB mapping failed; provision 2 MiB huge pages first");
        }
    }

    if ((mode == PageMode::Huge1G || mode == PageMode::Huge2M) && strict) {
        throw std::runtime_error("requested strict huge-page mapping is unavailable");
    }

    const auto length = round_up(bytes, normal_page);

    void* ptr = nullptr;
    if (::posix_memalign(&ptr, alignment, length) != 0) {
        throw std::bad_alloc();
    }

    return MappedRegion(ptr, length, false, false, normal_page);
}

template <typename T>
class FixedPool final {
    struct FreeNode {
        FreeNode* next;
    };

    static constexpr std::size_t kAlignment =
        (alignof(T) > alignof(std::max_align_t)) ? alignof(T) : alignof(std::max_align_t);

    static constexpr std::size_t kStride =
        ((sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode)) +
         alignof(T) - 1) /
        alignof(T) * alignof(T);

public:
    explicit FixedPool(std::size_t capacity,
                       PageMode mode = PageMode::Auto,
                       bool strict = false)
        : capacity_(capacity),
          stride_(kStride),
          region_(make_region(capacity, mode, strict)) {
        if (capacity_ == 0) {
            throw std::invalid_argument("FixedPool capacity must be positive");
        }

        auto* bytes = static_cast<std::byte*>(region_.data());
        for (std::size_t i = 0; i < capacity_; ++i) {
            auto* node = reinterpret_cast<FreeNode*>(bytes + i * stride_);
            node->next = (i + 1 < capacity_)
                ? reinterpret_cast<FreeNode*>(bytes + (i + 1) * stride_)
                : nullptr;
        }

        free_head_ = reinterpret_cast<FreeNode*>(bytes);
    }

    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;

    template <typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* memory = allocate();
        if (memory == nullptr) return nullptr;

        try {
            return ::new (memory) T(std::forward<Args>(args)...);
        } catch (...) {
            deallocate(memory);
            throw;
        }
    }

    void destroy(T* object) noexcept {
        if (object == nullptr) return;
        object->~T();
        deallocate(object);
    }

    [[nodiscard]] void* allocate() noexcept {
        if (free_head_ == nullptr) return nullptr;

        auto* out = free_head_;
        free_head_ = free_head_->next;
        ++used_;
        return out;
    }

    void deallocate(void* memory) noexcept {
        if (memory == nullptr) return;

        auto* node = static_cast<FreeNode*>(memory);
        node->next = free_head_;
        free_head_ = node;
        if (used_ > 0) --used_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return capacity_ - used_; }
    [[nodiscard]] const void* base() const noexcept { return region_.data(); }
    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return region_.size(); }
    [[nodiscard]] bool hugepage_backed() const noexcept { return region_.hugepage_backed(); }
    [[nodiscard]] std::size_t page_size() const noexcept { return region_.page_size(); }
    [[nodiscard]] static constexpr std::size_t stride() noexcept { return kStride; }

private:
    static MappedRegion make_region(std::size_t capacity,
                                    PageMode mode,
                                    bool strict) {
        if (capacity == 0) {
            throw std::invalid_argument("FixedPool capacity must be positive");
        }

        if (kStride > std::numeric_limits<std::size_t>::max() / capacity) {
            throw std::overflow_error("FixedPool size exceeds addressable memory");
        }

        return map_pages(kStride * capacity, mode, strict, kAlignment);
    }

    std::size_t capacity_{0};
    std::size_t stride_{0};
    MappedRegion region_;
    FreeNode* free_head_{nullptr};
    std::size_t used_{0};
};

} // namespace efvi
