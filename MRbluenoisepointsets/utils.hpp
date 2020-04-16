#pragma once

#include <chrono>
#include <iostream>


typedef uint64_t loooong;
namespace aux 
{
    // High res clock //
    class Timer 
	{
        typedef std::chrono::high_resolution_clock high_resolution_clock;
        typedef std::chrono::milliseconds milliseconds;
    public:
        explicit Timer(bool run = false)
        {
            if (run) Reset();
        }
        void Reset()
        {
            _start = high_resolution_clock::now();
        }
        milliseconds Elapsed() const
        {
            return std::chrono::duration_cast<milliseconds>(high_resolution_clock::now() - _start);
        }
        template <typename T, typename Traits>
        friend std::basic_ostream<T, Traits>& operator<<(std::basic_ostream<T, Traits>& out, const Timer& timer)
        {
            return out << timer.Elapsed().count();
        }
        static loooong nanoTime() {
            auto now = std::chrono::system_clock::now();
            auto now_nanosecs = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
            auto epoch = now_nanosecs.time_since_epoch();
            auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch);
            const loooong duration = value.count();
            return duration;
        }
    private:
        high_resolution_clock::time_point _start;
    };

	// Console output coloring //
    enum Code {
        FG_RED      = 31,
        FG_GREEN    = 32,
        FG_YELLOW   = 33,
        FG_BLUE     = 34,
        FG_DEFAULT  = 39,
        BG_RED      = 41,
        BG_GREEN    = 42,
        BG_YELLOW   = 43,
        BG_BLUE     = 44,
        BG_DEFAULT  = 49
    };
    class Modifier {
        Code code;
    public:
        Modifier(Code pCode) : code(pCode) {}
        friend std::ostream&
        operator<<(std::ostream& os, const Modifier& mod) {
            return os << "\033[" << mod.code << "m";
        }
    };
} // end namespace aux
