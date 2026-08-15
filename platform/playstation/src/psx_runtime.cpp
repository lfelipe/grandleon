// SPDX-License-Identifier: MIT
// See psx_runtime.h. Nothing here is in `engine/`.

#include "psx_runtime.h"

#include <cstddef>
#include <cstdint>
#include <new>
// For the one explicit instantiation at the foot of this file, which is the
// alternative to vendoring a second libstdc++ source. See the comment there.
#include <string>

// Defined at the bottom of this file, replacing Nugget's weak one. Declared
// here because operator new needs it and <cstdlib> is not available to a
// translation unit compiled without a hosted C library.
extern "C" [[noreturn]] void abort();

namespace {

// PCSX-Redux's control port. Writing an exit code to it ends the emulator
// process under -testmode, which is the only way a program on a machine with
// no operating system can fail loudly rather than silently.
void emulator_exit(int code) {
    *reinterpret_cast<volatile std::int16_t*>(0x1f802082) =
        static_cast<std::int16_t>(code);
}

}  // namespace

namespace {

// The BIOS teletype. `putchar` is entry 0x3d of the kernel's B0 table; the
// call convention is "put the entry number in $t1 and jump to the table
// address", which is why this is written out rather than declared. PCSX-Redux
// intercepts the call and writes the byte to its own stdout, so this is the
// whole reporting channel: no display, no framebuffer, no host filesystem.
//
// Taken from Nugget's common/syscalls/syscalls.h rather than including it: the
// header is C with inline assembly throughout, and putting it on the include
// path of a translation unit compiled with -Wpedantic -Wconversion -Werror
// would mean relaxing the warning discipline for the sake of one function.
void bios_putchar(int value) {
    register int entry asm("t1") = 0x3d;
    __asm__ volatile("" : "=r"(entry) : "r"(entry));
    reinterpret_cast<void (*)(int)>(0xb0)(value);
}

// Root counter 2, the only clock a PlayStation offers a program that has not
// set up interrupts. In divide-by-eight mode it counts one tick per eight
// system clocks, and it is sixteen bits wide, so it wraps every 15.5 ms. That
// is why clock_ticks() extends it by polling rather than reading it once at
// each end of a measurement.
struct Counter {
    volatile std::uint16_t value;
    std::uint16_t padding0;
    volatile std::uint16_t mode;
    std::uint16_t padding1;
    volatile std::uint16_t target;
    std::uint16_t padding2[3];
};

// KSEG1: uncached, so the counter is read from the hardware rather than from
// whatever the data cache last saw. The PlayStation has no data cache to speak
// of, but the address is the documented one and there is no reason to be
// clever about it.
Counter* const counters = reinterpret_cast<Counter*>(0xbf801100);

constexpr std::uint16_t counter_clock_div8 = 0x0200;

std::uint32_t clock_high = 0;
std::uint16_t clock_last = 0;

// The heap. Its extent is what the linker script leaves over: everything from
// the end of .bss up to the stack, which is the same budget a real port would
// have. Nothing is reserved statically, so the numbers this reports are the
// machine's, not an arena's.
extern "C" {
extern std::uint8_t __heap_base[];
extern std::uint8_t __sp[];
}

// Blocks are eight-byte aligned and carry an eight-byte header, so a payload
// is aligned for anything the engine puts in it, including std::uint64_t,
// whose __alignof__ is 8 on this ABI even though MIPS-I has no instruction
// that needs it.
//
// `prev_size` makes coalescing backwards O(1); the physically next block is at
// `this + size`, so coalescing forwards is O(1) too. Free blocks additionally
// hold two list pointers in their payload, which is why the minimum block is
// sixteen bytes.
struct Block {
    std::uint32_t size;  // total, header included; bit 0 marks it in use
    std::uint32_t prev_size;
};

struct FreeLinks {
    Block* next;
    Block* previous;
};

constexpr std::uint32_t header_size = 8;
constexpr std::uint32_t minimum_block = 16;
constexpr std::uint32_t used_flag = 1;

// The stack has to live somewhere. 32 KiB is well beyond what this executable
// uses: the deepest recursion in it is the JSON parser's, over a document
// nested six levels. The gap is reported rather than hidden so that a future
// renderer knows what it is being charged.
constexpr std::uint32_t stack_reserve = 32 * 1024;

Block* heap_start = nullptr;
Block* heap_sentinel = nullptr;
Block* free_head = nullptr;
std::uint32_t heap_total = 0;

std::uint32_t live_bytes = 0;
std::uint32_t peak_bytes = 0;
std::uint32_t allocation_count = 0;
std::uint32_t live_allocations = 0;

std::uint32_t block_size(const Block* block) { return block->size & ~used_flag; }
bool block_used(const Block* block) { return (block->size & used_flag) != 0; }

Block* next_block(Block* block) {
    return reinterpret_cast<Block*>(
        reinterpret_cast<std::uint8_t*>(block) + block_size(block)
    );
}

Block* previous_block(Block* block) {
    if (block->prev_size == 0) {
        return nullptr;
    }
    return reinterpret_cast<Block*>(
        reinterpret_cast<std::uint8_t*>(block) - block->prev_size
    );
}

FreeLinks* links(Block* block) {
    return reinterpret_cast<FreeLinks*>(
        reinterpret_cast<std::uint8_t*>(block) + header_size
    );
}

void unlink_free(Block* block) {
    FreeLinks* self = links(block);
    if (self->previous != nullptr) {
        links(self->previous)->next = self->next;
    } else {
        free_head = self->next;
    }
    if (self->next != nullptr) {
        links(self->next)->previous = self->previous;
    }
}

void link_free(Block* block) {
    FreeLinks* self = links(block);
    self->previous = nullptr;
    self->next = free_head;
    if (free_head != nullptr) {
        links(free_head)->previous = block;
    }
    free_head = block;
}

void initialize_heap() {
    if (heap_start != nullptr) {
        return;
    }
    auto base = reinterpret_cast<std::uintptr_t>(__heap_base);
    auto limit = reinterpret_cast<std::uintptr_t>(__sp) - stack_reserve;
    base = (base + 7U) & ~static_cast<std::uintptr_t>(7U);
    limit &= ~static_cast<std::uintptr_t>(7U);

    // The last eight bytes are a permanently-used sentinel block, so that
    // forward coalescing has something to stop at without a bounds test in the
    // hot path.
    heap_start = reinterpret_cast<Block*>(base);
    heap_total = static_cast<std::uint32_t>(limit - base) - header_size;
    heap_start->size = heap_total;
    heap_start->prev_size = 0;

    heap_sentinel = next_block(heap_start);
    heap_sentinel->size = header_size | used_flag;
    heap_sentinel->prev_size = heap_total;

    free_head = nullptr;
    link_free(heap_start);
}

void* allocate(std::size_t requested) {
    initialize_heap();

    // Refused before the request is added to, because on this machine
    // `std::size_t` is thirty-two bits wide and everything below adds to it:
    // the header first, then the rounding up to eight. A request within
    // fifteen bytes of the top of the address space comes out of that sum as a
    // number below `minimum_block`, is raised to it, and is answered with a
    // sixteen-byte block, after which the caller writes the four gigabytes it
    // asked for, starting inside the heap.
    //
    // The bound is the heap rather than the wrap, because the heap is the
    // honest one and it is smaller by three orders of magnitude: a request for
    // more bytes than this machine has could never have been answered with
    // anything but nothing, whatever the arithmetic did on the way. Every
    // request the wrap could reach is refused here by an enormous margin, so
    // the sum below cannot overflow.
    if (requested > heap_total) {
        return nullptr;
    }

    std::uint32_t want = static_cast<std::uint32_t>(requested) + header_size;
    want = (want + 7U) & ~7U;
    if (want < minimum_block) {
        want = minimum_block;
    }

    for (Block* block = free_head; block != nullptr; block = links(block)->next) {
        const std::uint32_t available = block_size(block);
        if (available < want) {
            continue;
        }
        unlink_free(block);
        if (available - want >= minimum_block) {
            Block* remainder = reinterpret_cast<Block*>(
                reinterpret_cast<std::uint8_t*>(block) + want
            );
            remainder->size = available - want;
            remainder->prev_size = want;
            next_block(remainder)->prev_size = remainder->size;
            block->size = want;
            link_free(remainder);
        }
        block->size |= used_flag;
        live_bytes += block_size(block);
        if (live_bytes > peak_bytes) {
            peak_bytes = live_bytes;
        }
        ++allocation_count;
        ++live_allocations;
        return reinterpret_cast<std::uint8_t*>(block) + header_size;
    }
    return nullptr;
}

void release(void* pointer) {
    if (pointer == nullptr) {
        return;
    }
    Block* block = reinterpret_cast<Block*>(
        reinterpret_cast<std::uint8_t*>(pointer) - header_size
    );
    live_bytes -= block_size(block);
    --live_allocations;
    block->size = block_size(block);

    Block* after = next_block(block);
    if (!block_used(after)) {
        unlink_free(after);
        block->size += block_size(after);
    }
    Block* before = previous_block(block);
    if (before != nullptr && !block_used(before)) {
        unlink_free(before);
        before->size += block->size;
        block = before;
    }
    next_block(block)->prev_size = block->size;
    link_free(block);
}

}  // namespace

namespace grandleon::playstation {

Line& Line::text(const char* value) {
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        push(*cursor);
    }
    return *this;
}

Line& Line::decimal(std::uint32_t value) {
    char digits[10];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    while (count > 0) {
        push(digits[--count]);
    }
    return *this;
}

Line& Line::signed_decimal(int value) {
    if (value < 0) {
        push('-');
        return decimal(static_cast<std::uint32_t>(-value));
    }
    return decimal(static_cast<std::uint32_t>(value));
}

Line& Line::hex64(std::uint64_t value) {
    static const char alphabet[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        push(alphabet[(value >> shift) & 0xfU]);
    }
    return *this;
}

void Line::push(char value) {
    if (length_ < capacity - 1) {
        buffer_[length_++] = value;
    }
}

void Line::flush() {
    for (std::size_t index = 0; index < length_; ++index) {
        bios_putchar(buffer_[index]);
    }
    bios_putchar('\n');
    length_ = 0;
}

void print(const char* message) {
    Line line;
    line.text(message).flush();
}

void signal_host(int slot) {
    if (slot <= 0 || slot > 255) return;
    *reinterpret_cast<volatile std::uint8_t*>(0x1f802081) =
        static_cast<std::uint8_t>(slot);
}

HeapCensus heap_census() {
    initialize_heap();
    HeapCensus census{};
    census.allocated_bytes = live_bytes;
    census.peak_allocated_bytes = peak_bytes;
    census.allocations = allocation_count;
    census.live_allocations = live_allocations;
    for (Block* block = free_head; block != nullptr; block = links(block)->next) {
        const std::uint32_t size = block_size(block);
        census.free_bytes += size;
        if (size > census.largest_free_block) {
            census.largest_free_block = size;
        }
    }
    return census;
}

void reset_heap_peak() {
    initialize_heap();
    peak_bytes = live_bytes;
    allocation_count = 0;
}

std::uint32_t heap_capacity() {
    initialize_heap();
    return heap_total;
}

void start_clock() {
    counters[2].mode = counter_clock_div8;
    counters[2].target = 0;
    counters[2].value = 0;
    clock_high = 0;
    clock_last = 0;
}

std::uint32_t clock_ticks() {
    const std::uint16_t now = counters[2].value;
    if (now < clock_last) {
        clock_high += 0x10000U;
    }
    clock_last = now;
    return clock_high + now;
}

namespace {

// I_STAT, uncached. Bit 0 is the vertical-blanking request, raised once a
// display frame by the hardware regardless of I_MASK. Writing a zero to a bit
// clears it and writing a one leaves it, so acknowledging one request without
// disturbing any other is a write of everything-but-that-bit.
volatile std::uint32_t* const interrupt_status =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801070);
constexpr std::uint32_t vblank_request = 1u << 0;

// A little over two NTSC frames. 33.868800 MHz over eight is 4.2336 ticks a
// microsecond, and a frame is 16,683 µs, so a frame is 70,633 ticks; this is
// 150,000, which is two frames and change. It bounds a wait that would
// otherwise be a hang and is never reached on a machine whose retrace is its
// own.
constexpr std::uint32_t vblank_budget_ticks = 150000;

std::uint32_t vblank_wait_count = 0;
std::uint32_t vblank_retrace_count = 0;

}  // namespace

void wait_vblank() {
    ++vblank_wait_count;
    // Acknowledge whatever is standing, so what is waited for below is the
    // *next* retrace rather than the one that happened while the frame's work
    // was being done.
    *interrupt_status = ~vblank_request;
    const std::uint32_t opened = clock_ticks();
    for (;;) {
        if ((*interrupt_status & vblank_request) != 0) {
            *interrupt_status = ~vblank_request;
            ++vblank_retrace_count;
            return;
        }
        if (clock_ticks() - opened >= vblank_budget_ticks) return;
    }
}

std::uint32_t vblank_waits() { return vblank_wait_count; }
std::uint32_t vblank_retraces() { return vblank_retrace_count; }

}  // namespace grandleon::playstation

// `std::nothrow` is an object, not a function, and libstdc++ defines it in
// new_handler.o, which is one of the 223 archive members compiled -mabicalls
// and so cannot be linked here. Defining it is the one thing in this file that
// adds a name to namespace std, which the standard reserves; there is no other
// way to satisfy the reference, and the reference is not optional. Every
// stable_sort in engine/package_runtime instantiates std::_Temporary_buffer,
// which calls the nothrow operator new below and needs this tag to name it.
namespace std {
const nothrow_t nothrow{};
}  // namespace std

// `std::string`'s out-of-line members, for the same reason and by the other of
// the two routes available.
//
// A machine that plays a campaign reaches `package_runtime::load_dialogue`,
// which builds authored strings, and a machine that saves one reaches
// `storage::ByteWindowSlotStorage`, which copies the slot names it parsed out
// of a device. Between them they call four members of `std::string` that are
// not in any header: libstdc++ declares `extern template class
// basic_string<char>` and emits its out-of-line members into
// `src/c++11/string-inst.cc`, so a caller gets an undefined reference rather
// than an implicit instantiation. The whole archive here is compiled
// `-mabicalls` against glibc and cannot be linked at all, which is why
// `tree.cc` is compiled from source in
// `platform/playstation/CMakeLists.txt`.
//
// A second vendored source file would be the consistent answer and is the wrong
// one: `string-inst.cc` instantiates *every* out-of-line member of two string
// classes and pulls in the locale facets behind them, to satisfy a single
// four-argument function. An explicit instantiation definition emits exactly
// that function, from the header the caller already included, compiled with the
// same flags as everything else here. Access control is deliberately not
// applied to an explicit instantiation, so a private member can be named.
//
// There are five, and they are two chains and a leaf rather than five choices:
// `_M_replace` calls `_M_mutate`, which calls `_M_create`; and `_M_assign`,
// which the copy-assignment a slot directory does, calls `_M_create` as well
// when the name it is taking is longer than the one it is replacing.
// `_M_create` is where both end: what it needs is `operator new`, which this
// file already answers. `_M_dispose` is the other side of it, a string going
// out of scope in a campaign's session options, and needs only `operator
// delete`. Its length check against `max_size()` is a comparison against a
// compile-time constant that no reachable call can fail, so GCC folds the
// `std::__throw_length_error` branch away. That is checked rather than
// assumed: `nm` on the linked image finds no `__throw_` symbol at all.
//
// Vendoring `string-inst.cc` beside `tree.cc` is the consistent-looking
// alternative and is worse: that file instantiates every out-of-line member of
// two string classes and the locale facets behind them, to satisfy four
// functions that share a leaf. The trade tips the day the list here stops
// being a list of leaves and starts being a list of everything.
template std::basic_string<char>& std::basic_string<char>::_M_replace(
    std::basic_string<char>::size_type, std::basic_string<char>::size_type,
    const char*, std::basic_string<char>::size_type
);
template void std::basic_string<char>::_M_mutate(
    std::basic_string<char>::size_type, std::basic_string<char>::size_type,
    const char*, std::basic_string<char>::size_type
);
template char* std::basic_string<char>::_M_create(
    std::basic_string<char>::size_type&, std::basic_string<char>::size_type
);
template void std::basic_string<char>::_M_assign(
    const std::basic_string<char>&
);
template void std::basic_string<char>::_M_dispose();

// The C++ allocation interface, over the heap above. The nothrow forms are not
// optional either, for the same reason.
//
// Nugget's cxxglue.c defines weak versions of all of these that abort; these
// strong definitions replace them.
void* operator new(std::size_t size) {
    void* pointer = allocate(size);
    if (pointer == nullptr) {
        // Not a throw. This target compiles the content path with exceptions
        // on, so std::bad_alloc could be thrown, but the one caller that
        // could catch it is the JSON parser, and an out-of-memory that unwinds
        // into a diagnostic would be reported as a malformed document. Saying
        // it plainly and stopping is the honest failure.
        grandleon::playstation::print("FAIL out of memory in operator new");
        abort();
    }
    return pointer;
}

void* operator new[](std::size_t size) { return operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size);
}

void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    release(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    release(pointer);
}

// The C library entry points a freestanding libstdc++ still expects. Nugget's
// crt0 supplies memcpy, memmove, memcmp, memset, strlen and memchr; these two
// are what the content path's JSON scanner reaches for and nothing supplies.
extern "C" {

// Nugget's own `abort()` cannot be used, and the reason is not discoverable
// without reading the emulator. Its implementation calls `pcsx_debugbreak()`
// before `pcsx_exit(-1)`; `pcsx_debugbreak` writes zero to 0x1f802081, which
// PCSX-Redux maps to *pause the machine*, so the CPU stops before the exit
// store is ever executed and the emulator spins for ever waiting for a
// debugger that is not there. Since GCC routes every unhandled C++ ABI failure
// through `abort()`, taking Nugget's version would turn any such failure into
// a CI hang instead of a non-zero exit. This one just exits.
//
// Nugget declares its version weak precisely so that a project can do this.
[[noreturn]] void abort() {
    emulator_exit(-1);
    while (true) {
        __asm__ volatile("");
    }
}

int isdigit(int value) { return value >= '0' && value <= '9'; }

int isspace(int value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\v' ||
           value == '\f' || value == '\r';
}

}  // extern "C"

// GCC 12 emits calls to these even under -fno-exceptions: the containers still
// have the "this cannot be represented" paths, and with exceptions off the
// call is what remains where the throw was. libstdc++ has them in
// functexcept.o, which cannot be linked here: it is compiled -mabicalls and
// calls gettext. So they become panics, which is what they mean on a machine
// with no way to recover: the container was asked for more than 2^32 bytes, or
// the heap is gone.
//
// Naming them by mangled symbol rather than declaring them in namespace std is
// deliberate. Defining functions in namespace std is undefined behaviour, and
// the point here is to satisfy a linker, not to extend a library.
extern "C" {

[[noreturn]] void _ZSt17__throw_bad_allocv() {
    grandleon::playstation::print("FAIL std::bad_alloc on target");
    abort();
}

[[noreturn]] void _ZSt28__throw_bad_array_new_lengthv() {
    grandleon::playstation::print("FAIL std::bad_array_new_length on target");
    abort();
}

[[noreturn]] void _ZSt20__throw_length_errorPKc(const char* what) {
    grandleon::playstation::Line line;
    line.text("FAIL std::length_error on target: ").text(what).flush();
    abort();
}

[[noreturn]] void _ZSt19__throw_logic_errorPKc(const char* what) {
    grandleon::playstation::Line line;
    line.text("FAIL std::logic_error on target: ").text(what).flush();
    abort();
}

[[noreturn]] void _ZSt20__throw_out_of_rangePKc(const char* what) {
    grandleon::playstation::Line line;
    line.text("FAIL std::out_of_range on target: ").text(what).flush();
    abort();
}

[[noreturn]] void _ZSt24__throw_out_of_range_fmtPKcz(const char* format, ...) {
    grandleon::playstation::Line line;
    line.text("FAIL std::out_of_range on target: ").text(format).flush();
    abort();
}

}  // extern "C"
