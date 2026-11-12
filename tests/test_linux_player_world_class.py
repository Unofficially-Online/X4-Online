"""Synthetic adversarial checks for the diagnostic P2 world class exception."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class PlayerWorldClassTest(unittest.TestCase):
    def test_owned_table_and_refusals(self):
        fixture = r'''
#include <assert.h>
#include "gate_player_world_class.h"
int main(void) {
    uintptr_t stock[81], table[81];
    static const unsigned slots[] = {13,14,17,21,30,56,57,40,41,42,43};
    for (unsigned i=0;i<81;i++) stock[i]=0x1000+i;
    memcpy(table,stock,sizeof table);
    player_world_class_check c = {
        .ship=11,.controller=22,.held_ship=11,.owned_controller=22,.route_ship=11,
        .route_controller=22,.ship_controller=22,.controller_ship=11,
        .observed_table=33,.private_table=33,.stock_table=44,.expected_stock_table=44,
        .ready=1,.installed=1,.left=1,.active=1,.held=1,.stock_pin=1,
        .stock_words=stock,.private_words=table
    };
    for (unsigned i=0;i<11;i++) { c.replacements[i]=0x2000+i; table[2+slots[i]]=c.replacements[i]; }
    assert(player_world_class_matches(&c));
    /* Every word matters, including offset-to-top, RTTI and all replacements. */
    for (unsigned i=0;i<81;i++) { table[i]^=1; assert(!player_world_class_matches(&c)); table[i]^=1; }
#define REFUSE(field, bad) do { __typeof__(c.field) old=c.field; c.field=(bad); assert(!player_world_class_matches(&c)); c.field=old; } while (0)
    REFUSE(ship,12); REFUSE(controller,23); REFUSE(held_ship,12); REFUSE(owned_controller,23);
    REFUSE(route_ship,12); REFUSE(route_controller,23); REFUSE(ship_controller,23);
    REFUSE(controller_ship,12); REFUSE(observed_table,44); REFUSE(private_table,34);
    REFUSE(stock_table,45); REFUSE(expected_stock_table,45); REFUSE(ready,0); REFUSE(installed,0);
    REFUSE(left,0); REFUSE(active,0); REFUSE(held,0); REFUSE(stock_pin,0); REFUSE(failures,1);
    REFUSE(replacements[7],0); REFUSE(replacements[10],0x3333);
    stock[2+12]^=1; assert(!player_world_class_matches(&c));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'test.c'
            exe = Path(tmp) / 'test'
            source.write_text(fixture)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/native/engine/linux'), str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
