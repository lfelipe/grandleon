// SPDX-License-Identifier: MIT
#include "exact_snapshot.h"
#include <array>
#include <vector>
#include <cstdlib>
#include <cstdio>

static void require(bool value) {
    if (!value) { std::fputs("Exact snapshot invariant failed\n", stderr); std::abort(); }
}

int main() {
    constexpr size_t words=71, count=342;
    std::vector<uint32_t> values(words*count), original;
    uint32_t random=0x3616;
    auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    std::array<unsigned char,22720+2> storage{};
    grandleon_exact_snapshot key{};
    for(unsigned trial=0;trial<2;++trial) {
        for(size_t r=0;r<count;++r)for(size_t w=0;w<words;++w)
            values[r*words+w]=(r && next()%8) ? values[(r-1)*words+w] : next();
        original=values;
        storage.front()=0x81;storage.back()=0x64;
        require(grandleon_snapshot_capture(&key,storage.data()+1,22720,values.data(),count,words));
        require(!key.raw && key.bytes<=22720 && key.records==count);
        require(grandleon_snapshot_matches(&key,storage.data()+1,values.data(),count,words));
        for(size_t prefix=0;prefix<count;++prefix)
            require(grandleon_snapshot_matches(&key,storage.data()+1,values.data(),prefix,words));
        for(size_t word=0;word<values.size();++word) {
            values[word]^=1u << (word%32);
            require(!grandleon_snapshot_matches(&key,storage.data()+1,values.data(),count,words));
            values[word]^=1u << (word%32);
        }
        require(storage.front()==0x81 && storage.back()==0x64);
        const size_t needed=key.bytes;
        require(!grandleon_snapshot_capture(&key,storage.data()+1,needed-1,values.data(),count,words));
        require(key.bytes==0 && key.records==0);
        require(storage.front()==0x81 && storage.back()==0x64);
        require(!grandleon_snapshot_matches(&key,storage.data()+1,values.data(),1,words));
        require(grandleon_snapshot_capture(&key,storage.data()+1,22720,values.data(),70,words));
        require(key.raw && grandleon_snapshot_matches(&key,storage.data()+1,values.data(),70,words));
        require(!grandleon_snapshot_matches(&key,storage.data()+1,values.data(),count,words));
        for (size_t word=0; word<70*words; ++word) {
            values[word]^=0x80000000u;
            require(!grandleon_snapshot_matches(&key,storage.data()+1,values.data(),70,words));
            values[word]^=0x80000000u;
        }
        require(values==original);
    }
    require(!grandleon_snapshot_capture(&key,storage.data()+1,22720,values.data(),SIZE_MAX,words));
    require(!grandleon_snapshot_capture(&key,storage.data()+1,22720,values.data(),1,0));
    puts("Exact bounded snapshots: every-word invalidation, prefixes, overflow and unaligned storage pass.");
}
