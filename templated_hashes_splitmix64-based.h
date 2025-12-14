#pragma once

#include <cstdint>
#include <cmath>
#include <type_traits>

// With CUDA .. let's use build-in floorf
#if CUDAENABLED
#define FLOOOOOOR floorf
#else
#define FLOOOOOOR std::floor
#endif

namespace RMB
{
    // -----------------------------------------------------------------------------
    // Minimal vector types (adapt/replace with yours)
    // -----------------------------------------------------------------------------
#ifndef ALREADYHAVEVECTORTYPES
    struct float2 { float x, y; };
    struct float3 { float x, y, z; };
    struct int2 { int   x, y; };
    struct int3 { int   x, y, z; };
    struct uint2 { uint32_t x, y; };
    struct uint3 { uint32_t x, y, z; };

    inline float2 make_float2(float x, float y) { return { x, y }; }
    inline float3 make_float3(float x, float y, float z) { return { x, y, z }; }
    inline int2   make_int2(int x, int y) { return { x, y }; }
    inline int3   make_int3(int x, int y, int z) { return { x, y, z }; }
    inline uint2  make_uint2(uint32_t x, uint32_t y) { return { x, y }; }
    inline uint3  make_uint3(uint32_t x, uint32_t y, uint32_t z) { return { x, y, z }; }
#endif

    // -----------------------------------------------------------------------------
    // SplitMix64 core
    // -----------------------------------------------------------------------------
    inline uint64_t splitmix64(uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        x ^= (x >> 31);
        return x;
    }

    inline float u32_to_float(uint32_t x)
    {
        return float(x) * (1.0f / 4294967296.0f); // [0,1)
    }

    // -----------------------------------------------------------------------------
    // Compile-time dimension inference (type traits)
    // -----------------------------------------------------------------------------
    template<typename T> struct hash_dim;

    template<> struct hash_dim<float>       { static constexpr int value = 1; };
    template<> struct hash_dim<int>         { static constexpr int value = 1; };
    template<> struct hash_dim<uint32_t>    { static constexpr int value = 1; };

    template<> struct hash_dim<float2>      { static constexpr int value = 2; };
    template<> struct hash_dim<int2>        { static constexpr int value = 2; };
    template<> struct hash_dim<uint2>       { static constexpr int value = 2; };

    template<> struct hash_dim<float3>      { static constexpr int value = 3; };
    template<> struct hash_dim<int3>        { static constexpr int value = 3; };
    template<> struct hash_dim<uint3>       { static constexpr int value = 3; };

    // Helper for nicer static_assert messages
    template<typename> inline constexpr bool dependent_false_v = false;

    // -----------------------------------------------------------------------------
    // Input packing (explicit 32-bit domains)
    // -----------------------------------------------------------------------------

    // 1D
    inline uint64_t pack1(int x) { return uint64_t(uint32_t(x)); }
    inline uint64_t pack1(uint32_t x) { return uint64_t(x); }
    inline uint64_t pack1(float x) { return uint64_t(uint32_t(FLOOOOOOR(x))); }

    // 2D
    inline uint64_t pack2(int2 v)
    {
        return (uint64_t(uint32_t(v.x)) << 32) |
            uint64_t(uint32_t(v.y));
    }

    inline uint64_t pack2(uint2 v)
    {
        return (uint64_t(v.x) << 32) | uint64_t(v.y);
    }

    inline uint64_t pack2(float2 v)
    {
        return pack2(make_int2(
            int(FLOOOOOOR(v.x)),
            int(FLOOOOOOR(v.y))
        ));
    }

    // 3D (fold 96 → 64, order-sensitive)
    inline uint64_t pack3(int3 v)
    {
        uint64_t x = uint64_t(uint32_t(v.x));
        uint64_t y = uint64_t(uint32_t(v.y));
        uint64_t z = uint64_t(uint32_t(v.z));

        return (x << 32)
            ^ (y * 0x9E3779B97F4A7C15ULL)
            ^ (z * 0xBF58476D1CE4E5B9ULL);
    }

    inline uint64_t pack3(uint3 v)
    {
        return pack3(make_int3(int(v.x), int(v.y), int(v.z)));
    }

    inline uint64_t pack3(float3 v)
    {
        return pack3(make_int3(
            int(FLOOOOOOR(v.x)),
            int(FLOOOOOOR(v.y)),
            int(FLOOOOOOR(v.z))
        ));
    }

    // Dimension-driven pack dispatcher
    template<typename IN>
    inline uint64_t pack_any(const IN& v)
    {
        if constexpr (hash_dim<IN>::value == 1) return pack1(v);
        else if constexpr (hash_dim<IN>::value == 2) return pack2(v);
        else if constexpr (hash_dim<IN>::value == 3) return pack3(v);
        else
        {
            static_assert(dependent_false_v<IN>, "Unsupported input type for rmb_hash (no hash_dim specialization).");
            return 0;
        }
    }

    // -----------------------------------------------------------------------------
    // Output behavior traits (define how many floats, how derived)
    // -----------------------------------------------------------------------------
    template<typename T> struct hash_out;

    // Floats
    template<> struct hash_out<float>
    {
        static float eval(uint64_t& s)
        {
            s = splitmix64(s);
            return u32_to_float(uint32_t(s >> 32));
        }
    };

    template<> struct hash_out<float2>
    {
        static float2 eval(uint64_t& s)
        {
            s = splitmix64(s);
            float x = u32_to_float(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            float y = u32_to_float(uint32_t(s >> 32));

            return make_float2(x, y);
        }
    };

    template<> struct hash_out<float3>
    {
        static float3 eval(uint64_t& s)
        {
            s = splitmix64(s);
            float x = u32_to_float(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            float y = u32_to_float(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            float z = u32_to_float(uint32_t(s >> 32));

            return make_float3(x, y, z);
        }
    };

    // Ints
    template<> struct hash_out<int> {
        static int eval(uint64_t s)
        {
            s = splitmix64(s);
            return int(uint32_t(s >> 32));
        }
    };

    template<> struct hash_out<uint32_t> {
        static uint32_t eval(uint64_t s)
        {
            s = splitmix64(s);
            return uint32_t(s >> 32);
        }
    };

    template<> struct hash_out<int2> {
        static int2 eval(uint64_t s)
        {
            s = splitmix64(s);
            int x = int(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            int y = int(uint32_t(s >> 32));

            return make_int2(x, y);
        }
    };

    template<> struct hash_out<uint2> {
        static uint2 eval(uint64_t s)
        {
            s = splitmix64(s);
            uint32_t x = uint32_t(s >> 32);

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            uint32_t y = uint32_t(s >> 32);

            return make_uint2(x, y);
        }
    };

    template<> struct hash_out<int3> {
        static int3 eval(uint64_t s)
        {
            s = splitmix64(s);
            int x = int(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            int y = int(uint32_t(s >> 32));

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            int z = int(uint32_t(s >> 32));

            return make_int3(x, y, z);
        }
    };

    template<> struct hash_out<uint3> {
        static uint3 eval(uint64_t s)
        {
            s = splitmix64(s);
            uint32_t x = uint32_t(s >> 32);

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            uint32_t y = uint32_t(s >> 32);

            s += 0x9E3779B97F4A7C15ULL;
            s = splitmix64(s);
            uint32_t z = uint32_t(s >> 32);

            return make_uint3(x, y, z);
        }
    };

    // Optional: compile-time output dimension inference too
    template<typename T> struct out_dim;
    template<> struct out_dim<float> { static constexpr int value = 1; };
    template<> struct out_dim<float2> { static constexpr int value = 2; };
    template<> struct out_dim<float3> { static constexpr int value = 3; };

    // -----------------------------------------------------------------------------
    // Generic hash entry point
    // -----------------------------------------------------------------------------
    template<typename TOut, typename TIn>
    inline TOut rmb_hash(const TIn& v, uint64_t stream = 0)
    {
        static_assert(std::is_same_v<TOut, float>       || std::is_same_v<TOut, float2> ||
                      std::is_same_v<TOut, float3>      || std::is_same_v<TOut, int>    ||
                      std::is_same_v<TOut, uint32_t>    || std::is_same_v<TOut, int2>   ||
                      std::is_same_v<TOut, uint2>   || std::is_same_v<TOut, int3>       ||
                      std::is_same_v<TOut, uint3>,
                      "Unsupported TOut type for rmb_hash (add hash_out specialization).");

        // Dimension inferred at compile time from TIn via hash_dim<TIn>::value
        uint64_t seed = pack_any(v);

        // Stream / domain separation (optional)
        seed += stream * 0x9E3779B97F4A7C15ULL;

        // With CUDA/PTX this is erroring out with (btw .. it should not) :
        // "a nonstatic member reference must be relative to a specific object"
        // So intanciate it and on that call the member function.
        //return hash_out<TOut>::eval(seed);
        hash_out<TOut> hasher;
        return hasher.eval(seed);
    }

    // -----------------------------------------------------------------------------
    // Examples:
    //
    // float    a = rmb_hash<float >    (make_int2(12, 7));
    // float2   b = rmb_hash<float2>    (make_float3(1.2f, 3.4f, 5.6f));
    // float3   c = rmb_hash<float3>    (uint32_t(42), 3);
    // uint32_t d = rmb_hash<uint32_t>  (make_int2(5, 9));
    // int3     e = rmb_hash<int3>      (make_float2(1.5f, 2.5f));
    //
    // To new vector types, just:
    //  1) add hash_dim<YourType> specialization,
    //  2) add packN(YourType) overload (or map to existing types).
    // -----------------------------------------------------------------------------

} // end namespace RMB