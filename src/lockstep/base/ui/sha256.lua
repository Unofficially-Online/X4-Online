-- X4LAN_SHA256.digest(text) -> 64 lowercase hex characters (FIPS 180-4 SHA-256), pure LuaJIT.
-- Used for plan/identity hashes; the per-tick digest uses the gate's BCrypt SHA-256 when present.
local bit = require('bit')
local band, bor, bxor, bnot, rshift, lshift, ror, tobit =
    bit.band, bit.bor, bit.bxor, bit.bnot, bit.rshift, bit.lshift, bit.ror, bit.tobit

local K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}
for i = 1, 64 do K[i] = tobit(K[i]) end

local function pad(text)
    local length = #text
    local zeros = (55 - length) % 64
    local bits = length * 8
    local tail = {}
    for i = 7, 0, -1 do tail[#tail + 1] = string.char(math.floor(bits / 2 ^ (8 * i)) % 256) end
    return text .. '\128' .. string.rep('\0', zeros) .. table.concat(tail)
end

local function digest(text)
    assert(type(text) == 'string', 'sha256 input must be a string')
    local h0, h1, h2, h3 = tobit(0x6a09e667), tobit(0xbb67ae85), tobit(0x3c6ef372), tobit(0xa54ff53a)
    local h4, h5, h6, h7 = tobit(0x510e527f), tobit(0x9b05688c), tobit(0x1f83d9ab), tobit(0x5be0cd19)
    local data, w = pad(text), {}
    for chunk = 1, #data, 64 do
        for i = 0, 15 do
            local a, b, c, d = data:byte(chunk + 4 * i, chunk + 4 * i + 3)
            w[i] = bor(lshift(a, 24), lshift(b, 16), lshift(c, 8), d)
        end
        for i = 16, 63 do
            local x, y = w[i - 15], w[i - 2]
            local s0 = bxor(ror(x, 7), ror(x, 18), rshift(x, 3))
            local s1 = bxor(ror(y, 17), ror(y, 19), rshift(y, 10))
            w[i] = tobit(w[i - 16] + s0 + w[i - 7] + s1)
        end
        local a, b, c, d, e, f, g, h = h0, h1, h2, h3, h4, h5, h6, h7
        for i = 0, 63 do
            local S1 = bxor(ror(e, 6), ror(e, 11), ror(e, 25))
            local ch = bxor(band(e, f), band(bnot(e), g))
            local t1 = tobit(h + S1 + ch + K[i + 1] + w[i])
            local S0 = bxor(ror(a, 2), ror(a, 13), ror(a, 22))
            local maj = bxor(band(a, b), band(a, c), band(b, c))
            local t2 = tobit(S0 + maj)
            h, g, f, e, d, c, b, a = g, f, e, tobit(d + t1), c, b, a, tobit(t1 + t2)
        end
        h0, h1, h2, h3 = tobit(h0 + a), tobit(h1 + b), tobit(h2 + c), tobit(h3 + d)
        h4, h5, h6, h7 = tobit(h4 + e), tobit(h5 + f), tobit(h6 + g), tobit(h7 + h)
    end
    return (bit.tohex(h0) .. bit.tohex(h1) .. bit.tohex(h2) .. bit.tohex(h3) ..
            bit.tohex(h4) .. bit.tohex(h5) .. bit.tohex(h6) .. bit.tohex(h7))
end

X4LAN_SHA256 = {digest = digest}
