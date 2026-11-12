/* Portable stock-v3 wire tests. No X4 image, hooks, network or runtime required. */
#include <stdio.h>
#include <stdlib.h>
#include "lockstep_stock_record.h"
static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr,"stock codec check failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while (0)

static float float_from_bits(uint32_t bits) {
    float value; memcpy(&value,&bits,4); return value;
}
static void expect_refusal(const unsigned char *bytes,size_t length) {
    stock_input_record output,before;
    memset(&output,0xa5,sizeof(output)); memcpy(&before,&output,sizeof(before));
    CHECK(!stock_input_decode(bytes,length,&output));
    CHECK(!memcmp(&output,&before,sizeof(output)));
}
static stock_input_record example(void) {
    stock_input_record record={0};
    record.flags=63; record.capture16=0xabcd;
    record.cursor[0]=0.5f; record.cursor[1]=float_from_bits(0x80000000); record.cursor[2]=-1.0f;
    record.aim[0]=1; record.aim[1]=2; record.aim[2]=3;
    record.steer=7; record.view=3; record.fov=1.5f; record.target=UINT64_C(0x1122334455667788);
    record.count=4;
    record.events[0]=(stock_input_event){0,0x7b,0};
    record.events[1]=(stock_input_event){1,8,1};
    record.events[2]=(stock_input_event){2,4,0xbe800000};
    record.events[3]=(stock_input_event){3,8,1};
    return record;
}
static void test_golden(void) {
    /* Independently specified LE header and four records: all flags, signed zero,
     * portable 64-bit target, action/state/range/context in dispatch order. */
    static const unsigned char golden[]={
0x03,0x3f,0xcd,0xab,0x00,0x00,0x00,0x3f,0x00,0x00,0x00,0x80,0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0x3f,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x40,0x07,0x03,0x00,0x00,0xc0,0x3f,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x04,0x7b,0x00,0x00,0x00,0x00,0x00,0x08,0x40,0x01,0x00,0x00,0x00,0x04,0x80,0x00,0x00,0x80,0xbe,0x08,0xc0,0x01,0x00,0x00,0x00
    };
    stock_input_record record=example(),decoded;
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    CHECK(stock_input_encode(&record,bytes,sizeof(bytes))==sizeof(golden));
    CHECK(!memcmp(bytes,golden,sizeof(golden)));
    CHECK(stock_input_decode(golden,sizeof(golden),&decoded));
    CHECK(decoded.capture16==0xabcd && decoded.target==UINT64_C(0x1122334455667788));
    CHECK(stock_input_float_bits(decoded.cursor[1])==0x80000000);
    CHECK(decoded.count==4 && decoded.events[2].value==0xbe800000);
    CHECK(stock_input_encode(&decoded,bytes,sizeof(bytes))==sizeof(golden));
    CHECK(!memcmp(bytes,golden,sizeof(golden)));
    /* Every accepted one-bit mutation must retain its exact wire bits. */
    for(size_t index=0;index<sizeof(golden);++index) for(unsigned bit=0;bit<8;++bit) {
        unsigned char changed[sizeof(golden)],again[STOCK_INPUT_MAX_BYTES];
        memcpy(changed,golden,sizeof(changed)); changed[index]^=(unsigned char)(1u<<bit);
        if(stock_input_decode(changed,sizeof(changed),&decoded)) {
            CHECK(stock_input_encode(&decoded,again,sizeof(again))==sizeof(changed));
            CHECK(!memcmp(changed,again,sizeof(changed)));
        } else expect_refusal(changed,sizeof(changed));
    }
}
static void test_bounds(void) {
    for(unsigned count=0;count<=STOCK_INPUT_MAX_EVENTS;++count) {
        stock_input_record record={0},decoded;
        record.count=count;
        for(unsigned i=0;i<count;++i) record.events[i]=(stock_input_event){STOCK_INPUT_CONTEXT,511,1};
        unsigned length=43+6*count;
        unsigned char storage[STOCK_INPUT_MAX_BYTES+2],before[sizeof(storage)];
        for(unsigned capacity=0;capacity<length;++capacity) {
            memset(storage,0xa5,sizeof(storage)); memcpy(before,storage,sizeof(storage));
            CHECK(!stock_input_encode(&record,storage+1,capacity));
            CHECK(!memcmp(storage,before,sizeof(storage)));
        }
        memset(storage,0xa5,sizeof(storage));
        CHECK(stock_input_encode(&record,storage+1,length)==length);
        CHECK(storage[0]==0xa5 && storage[length+1]==0xa5);
        CHECK(stock_input_decode(storage+1,length,&decoded) && decoded.count==count);
        for(unsigned cut=0;cut<length;++cut) expect_refusal(storage+1,cut);
        expect_refusal(storage+1,length+1);
    }
    stock_input_record record={0}; unsigned char bytes[STOCK_INPUT_MAX_BYTES],before[sizeof(bytes)];
    memset(bytes,0x5a,sizeof(bytes)); memcpy(before,bytes,sizeof(bytes));
    record.count=32; CHECK(!stock_input_encode(&record,bytes,sizeof(bytes)));
    record.count=~0u; CHECK(!stock_input_encode(&record,bytes,sizeof(bytes)));
    CHECK(!stock_input_encode(NULL,bytes,sizeof(bytes)));
    CHECK(!memcmp(bytes,before,sizeof(bytes)));
    CHECK(!stock_input_encode(&record,NULL,sizeof(bytes)));
    expect_refusal(NULL,STOCK_INPUT_MAX_BYTES);
    CHECK(!stock_input_decode(bytes,sizeof(bytes),NULL));
}
static void test_malformed(void) {
    stock_input_record record={0},decoded; unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    CHECK(stock_input_encode(&record,bytes,sizeof(bytes))==43);
    for(unsigned i=0;i<43;++i) {
        unsigned char changed[43]; memcpy(changed,bytes,43);
        /* capture tick is unrestricted; active/fire flags are independently legal. */
        if(i==2 || i==3) continue;
        changed[i]=0x80; expect_refusal(changed,43);
    }
    for(unsigned field=0;field<7;++field) {
        static const unsigned offsets[]={4,8,12,16,20,24,30};
        static const uint32_t invalid[]={0x80000000,0x7f800000,0xff800000,0x7fc01234,1};
        for(unsigned value=0;value<5;++value) {
            CHECK(stock_input_encode(&record,bytes,sizeof(bytes))==43);
            stock_input_write_u32(bytes+offsets[field],invalid[value]); expect_refusal(bytes,43);
        }
    }
    record=example();
    for(unsigned axis=0;axis<3;++axis) {
        record.cursor[axis]=float_from_bits(0x7fc00001);
        unsigned n=stock_input_encode(&record,bytes,sizeof(bytes)); expect_refusal(bytes,n); record=example();
        record.aim[axis]=float_from_bits(0x7f800000);
        n=stock_input_encode(&record,bytes,sizeof(bytes)); expect_refusal(bytes,n); record=example();
    }
    record.steer=8; expect_refusal(bytes,stock_input_encode(&record,bytes,sizeof(bytes))); record=example();
    record.view=4; expect_refusal(bytes,stock_input_encode(&record,bytes,sizeof(bytes))); record=example();
    record.fov=float_from_bits(0x7fc00000); expect_refusal(bytes,stock_input_encode(&record,bytes,sizeof(bytes)));
    for(unsigned kind=0;kind<4;++kind) {
        record=(stock_input_record){0}; record.count=1;
        record.events[0]=(stock_input_event){kind,512,0};
        expect_refusal(bytes,stock_input_encode(&record,bytes,sizeof(bytes)));
        record.events[0].id=511;
        record.events[0].value=kind==STOCK_INPUT_RANGE?0x7f800000:kind==STOCK_INPUT_ACTION?1:2;
        expect_refusal(bytes,stock_input_encode(&record,bytes,sizeof(bytes)));
        record.events[0].value=0; unsigned n=stock_input_encode(&record,bytes,sizeof(bytes));
        CHECK(stock_input_decode(bytes,n,&decoded)); /* Adapter, not codec, admits IDs. */
    }
}
static uint32_t random_word(uint32_t *state) {
    *state=*state*1664525u+1013904223u; return *state;
}
static void test_roundtrips(void) {
    uint32_t state=0x534f434bu;
    for(unsigned sample=0;sample<4096;++sample) {
        stock_input_record record={0},decoded;
        record.flags=random_word(&state)&63; record.capture16=(uint16_t)random_word(&state);
        if(record.flags&STOCK_INPUT_CURSOR) for(unsigned i=0;i<3;++i) record.cursor[i]=float_from_bits(random_word(&state)&0xbf7fffff);
        if(record.flags&STOCK_INPUT_AIM) {
            for(unsigned i=0;i<3;++i) record.aim[i]=float_from_bits(random_word(&state)&0xbf7fffff);
            record.steer=random_word(&state)&7;
        }
        if(record.flags&STOCK_INPUT_VIEW) {
            record.view=random_word(&state)&3; record.fov=float_from_bits(random_word(&state)&0xbf7fffff);
            record.target=(uint64_t)random_word(&state)<<32|random_word(&state);
        }
        record.count=sample%32;
        for(unsigned i=0;i<record.count;++i) {
            stock_input_event *event=&record.events[i]; event->kind=random_word(&state)&3;
            event->id=random_word(&state)&511;
            event->value=event->kind==STOCK_INPUT_RANGE?(random_word(&state)&0xbf7fffff):event->kind==STOCK_INPUT_ACTION?0:random_word(&state)&1;
        }
        unsigned char bytes[STOCK_INPUT_MAX_BYTES],again[STOCK_INPUT_MAX_BYTES];
        unsigned n=stock_input_encode(&record,bytes,sizeof(bytes));
        CHECK(n==43+6*record.count); CHECK(stock_input_decode(bytes,n,&decoded));
        CHECK(stock_input_encode(&decoded,again,sizeof(again))==n); CHECK(!memcmp(bytes,again,n));
    }
}
int main(void) {
    test_golden(); test_bounds(); test_malformed(); test_roundtrips();
    printf("stock record codec: 4 groups, %u checks passed (portable wire only)\n",checks); return 0;
}
