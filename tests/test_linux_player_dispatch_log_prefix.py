"""Actual exclusive lifecycle file I/O plus the shared frame-prefix helper."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class DispatchLogPrefixTests(unittest.TestCase):
    def test_actual_log_open_precedes_prefix_and_default_output_is_unchanged(self):
        root = Path(__file__).resolve().parents[1]
        engine = root / 'src/native/engine/linux'
        lifecycle = (engine / 'gate_player_lifecycle_discard.h').read_text()
        physics = (engine / 'gate_player_physics_discard.h').read_text()

        def function(text, signature):
            start = text.index(signature)
            opening = text.index('{', start)
            depth = 1
            end = opening + 1
            while depth:
                depth += (text[end] == '{') - (text[end] == '}')
                end += 1
            return text[start:end]

        initial = function(lifecycle, 'static void lifecycle_discard_begin(')
        self.assertLess(initial.index('lifecycle_discard_open_log();'),
                        initial.index('player_physics_frame_begin_record(stats.issued);'))
        self.assertIn('if (physics == 2)', initial)
        recurring = function(physics, 'static void player_physics_frame_record(')
        self.assertIn('if (!strcmp(event, "begin"))', recurring)
        self.assertIn('player_physics_frame_begin_record(generation);', recurring)
        source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static int lifecycle_discard_log = -1;
'''
        source += function(lifecycle, 'static void lifecycle_discard_open_log(') + '\n'
        source += function(lifecycle, 'static void lifecycle_discard_write(') + '\n'
        source += '#include "gate_player_physics_frame_prefix.h"\n'
        source += r'''
static unsigned callbacks;
static void prefix(uint64_t generation) {
    assert(fcntl(lifecycle_discard_log, F_GETFD) >= 0);
    assert(generation == 37 + callbacks);
    if (!callbacks) lifecycle_discard_write("bootstrap\n");
    char row[64]; snprintf(row, sizeof row, "ticket=%llu\n", (unsigned long long)generation);
    lifecycle_discard_write(row); ++callbacks;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    /* A premature write remains a fatal error and creates no file. */
    pid_t child = fork(); assert(child >= 0);
    if (!child) { lifecycle_discard_write("premature\n"); _exit(99); }
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 224);
    assert(access(argv[1], F_OK) == -1);
    for (int active = 0; active <= 1; ++active) {
        assert(!setenv("X4_DEV_PLAYER_LIFECYCLE_LOG", argv[active + 1], 1));
        lifecycle_discard_open_log();
        struct stat info; assert(!fstat(lifecycle_discard_log, &info));
        assert((info.st_mode & 0777) == 0600);
        player_physics_frame_prefix = active ? prefix : NULL;
        player_physics_frame_begin_record(37);
        player_physics_frame_begin_record(38);
        assert(!close(lifecycle_discard_log)); lifecycle_discard_log = -1;
    }
    assert(callbacks == 2);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            fixture = temporary / 'prefix.c'
            executable = temporary / 'prefix'
            fixture.write_text(source)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(engine), str(fixture), '-o', str(executable)],
                           check=True, timeout=30)
            off, active = temporary / 'off.log', temporary / 'active.log'
            subprocess.run([str(executable), str(off), str(active)], check=True, timeout=10)
            first = 'X4LAN LIFECYCLE_DISCARD physics=frame tick=37 event=begin\n'
            second = 'X4LAN LIFECYCLE_DISCARD physics=frame tick=38 event=begin\n'
            self.assertEqual(off.read_text(), first + second)
            self.assertEqual(active.read_text(), 'bootstrap\nticket=37\n' + first + 'ticket=38\n' + second)
