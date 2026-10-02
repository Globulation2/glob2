#include <ChunkedStreamBackend.h>
#include <cstdlib>
#include <new>
#include <cstdio>
static bool failTable = false;
static bool failPayload = false;
static int livePayloads = 0;
void* operator new(std::size_t n)
{
    if (failTable && n == sizeof(std::unique_ptr<unsigned char[]>))
    { failTable = false; throw std::bad_alloc(); }
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n)
{
    if (failPayload) { failPayload = false; throw std::bad_alloc(); }
    if (void* p = std::malloc(n)) { ++livePayloads; return p; }
    throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { if (p) --livePayloads; std::free(p); }
int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        GAGCore::ChunkedBuffer bytes;
        failTable = mode == 0;
        failPayload = mode == 1;
        bool caught = false;
        try { bytes.writeAt(0, "x", 1); } catch (const std::bad_alloc&) { caught = true; }
        if (!caught || livePayloads || bytes.size() || bytes.allocatedCapacity()) return 1;
        bytes.writeAt(0, "x", 1);
        char result = 0; bytes.readAt(0, &result, 1);
        if (result != 'x' || livePayloads != 1) return 2;
    }
    if (livePayloads) return 3;
    std::puts("PASS: block/table allocation failure releases payload; reuse succeeds");
}
