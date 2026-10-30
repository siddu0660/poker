#pragma once
#include <cstdint>

namespace pf {

// splitmix64: seeds xoshiro and derives per-thread streams.
constexpr std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// xoshiro256** — one instance per thread, never shared.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1, std::uint64_t stream = 0) noexcept {
        std::uint64_t sm = seed ^ (stream * 0xD1B54A32D192ED03ULL);
        for (auto& w : s_) w = splitmix64(sm);
    }

    std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Unbiased integer in [0, bound) using Lemire's multiply-shift with rejection.
    std::uint32_t below(std::uint32_t bound) noexcept {
        std::uint64_t x = next() >> 32;
        std::uint64_t m = x * bound;
        auto low = static_cast<std::uint32_t>(m);
        if (low < bound) {
            const std::uint32_t threshold = (0u - bound) % bound;
            while (low < threshold) {
                x = next() >> 32;
                m = x * bound;
                low = static_cast<std::uint32_t>(m);
            }
        }
        return static_cast<std::uint32_t>(m >> 32);
    }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }
    std::uint64_t s_[4]{};
};

}  // namespace pf
