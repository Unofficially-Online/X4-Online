"""The master's limits: token buckets per IP and for the whole server, and the caps (docs/NETPLAY.md, "Trust and safety").

A public server must not become an amplifier, a reflector or a port scanner, and one address must not be able to take
it over. Every request costs a token from its IP's bucket and from the global one; the expensive ones (a new game, a
join, a code lookup, a probe) have buckets of their own. A bucket refills at `rate` tokens a second up to `burst`, so a
short burst passes and a flood is cut to the rate."""
from dataclasses import dataclass
import threading
import time

KEYS_MAX = 20000   # IPs with a bucket of their own; beyond that idle (full) buckets are dropped first


@dataclass
class MasterLimits:
    """Every cap and rate of the master. The defaults are the contract's (200 games, 32 relays, 4 live games per IP,
    8 GiB of data); the rest are sized for a few hundred players."""
    games_max: int = 200
    relays_max: int = 32
    games_per_ip: int = 4
    data_bytes_max: int = 8 << 30          # every uploaded file together (the relays' own logs are not counted)
    json_bytes_max: int = 64 << 10         # a JSON request body
    heartbeat_s: float = 90                # a game without a heartbeat for this long is gone
    player_idle_s: float = 90              # before the start: a joined player without any request for this long leaves
    lobby_wait_s: float = 25               # the longest long poll
    ended_keep_s: float = 60               # an ended game still answers its players' polls (state 'ended') this long
    socket_timeout_s: float = 30           # a stalled connection gives up after this long without progress
    connections_max: int = 512             # open HTTP connections (a long poll holds one for up to 25 s)
    connections_per_ip: int = 32           # ... from one address (players behind one NAT share it)
    requests_per_s: float = 5              # HTTP requests per IP
    requests_burst: float = 60
    global_requests_per_s: float = 300
    global_requests_burst: float = 1000
    creates_per_minute: float = 10         # new games per IP
    joins_per_minute: float = 30           # joins per IP
    codes_per_minute: float = 30           # code lookups per IP (guessing codes)
    probes_per_minute: float = 6           # reachability probes per IP (the contract's 6)
    global_probes_per_minute: float = 120  # every probe connects out: a cap for the whole server
    echo_per_s: float = 10                 # UDP echo datagrams per IP
    echo_burst: float = 20
    global_echo_per_s: float = 1000
    global_echo_burst: float = 2000
    probe_valid_s: float = 3600            # a passed probe counts for the direct/relay decision this long (one lease)
    probe_timeout_s: float = 3             # the probe's TCP connect
    load_wait_s: float = 30                # the load second: 30 s after the last ready player ...
    start_wait_s: float = 20               # ... and at least 20 s after the start
    relay_ready_s: float = 10              # a relay that is not listening this long after its start has failed
    idle_end: int = 420                    # serve's: a relay without a step for this long is over (the load takes minutes)
    idle_left: int = 60                    # ... once it has stepped: every player has left


class TokenBucket:
    """`burst` tokens, refilled at `rate` a second. take() spends one if there is one."""
    def __init__(self, rate, burst, now):
        self.rate = float(rate)
        self.burst = float(burst)
        self.tokens = float(burst)
        self.stamp = now

    def refill(self, now):
        elapsed = max(0.0, now - self.stamp)
        self.tokens = min(self.burst, self.tokens + elapsed * self.rate)
        self.stamp = now

    def take(self, now, cost=1.0):
        self.refill(now)
        if self.tokens < cost:
            return False
        self.tokens -= cost
        return True

    def full(self, now):
        self.refill(now)
        return self.tokens >= self.burst


class RateLimiter:
    """A bucket per key (an IP) and one for every key together; allow(key) spends a token of both or refuses."""
    def __init__(self, rate, burst, global_rate, global_burst, clock=time.monotonic, keys_max=KEYS_MAX):
        self.rate = rate
        self.burst = burst
        self.clock = clock
        self.keys_max = keys_max
        self.lock = threading.Lock()
        self.buckets = {}
        self.everyone = TokenBucket(global_rate, global_burst, clock())

    def allow(self, key):
        now = self.clock()
        with self.lock:
            bucket = self.buckets.get(key)
            if bucket is None:
                if len(self.buckets) >= self.keys_max:
                    self.prune(now)
                bucket = TokenBucket(self.rate, self.burst, now)
                self.buckets[key] = bucket
            if not bucket.take(now):
                return False
            return self.everyone.take(now)

    def prune(self, now):
        """Room for new keys: drop the buckets that are full again (their IPs have been quiet); under a flood of new
        addresses that leaves too many, so the older half goes (a dropped IP starts again with a full bucket)."""
        self.buckets = {key: bucket for key, bucket in self.buckets.items() if not bucket.full(now)}
        if len(self.buckets) >= self.keys_max:
            keep = list(self.buckets.items())[len(self.buckets) // 2:]
            self.buckets = dict(keep)


def per_minute(count, clock=time.monotonic, global_count=None):
    """A limiter of `count` per minute per key (a burst of `count`), and `global_count` per minute for all keys."""
    total = global_count if global_count is not None else count * 1000
    return RateLimiter(count / 60.0, count, total / 60.0, total, clock=clock)
