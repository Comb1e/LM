#define _POSIX_C_SOURCE 200809L
#include "std.h"
#include <assert.h>
#include <limits.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long budget = -1;
static size_t live;
static void *allocations[65536];
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static int allowed(void) { if (budget < 0) return 1; if (!budget) return 0; --budget; return 1; }
static void record(void *p) {
    if (!p) return;
    for (size_t i=0;i<sizeof(allocations)/sizeof(*allocations);i++) if (!allocations[i]) { allocations[i]=p; ++live; return; }
    assert(!"allocation tracker full");
}
static void forget(void *p) {
    if (!p) return;
    for (size_t i=0;i<sizeof(allocations)/sizeof(*allocations);i++) if (allocations[i]==p) { allocations[i]=NULL; --live; return; }
    assert(!"untracked or double free");
}
void *__wrap_calloc(size_t n,size_t width) { if (!allowed()) return NULL; void *p=__real_calloc(n,width); record(p); return p; }
void *__wrap_realloc(void *old,size_t n) {
    if (!allowed()) return NULL;
    void *p=__real_realloc(old,n);
    if (p) { forget(old); record(p); }
    return p;
}
void __wrap_free(void *p) { forget(p); __real_free(p); }
#define B(s) ((uint8_t *)(s))
static void core_bytes(void) {
    void *p=(void *)1;
    assert(std_core_allocate(UINT64_MAX,8,&p)==STD_RANGE && p==(void *)1);
    budget=0; assert(std_core_allocate(1,1,&p)==STD_NOMEM && p==(void *)1); budget=-1;
    assert(!std_core_allocate(0,1,&p) && p); std_core_release(p); std_core_release(NULL);
    for(int i=0;i<=STD_LIMIT;i++) assert(strlen((char *)std_core_status_text(i)));
    assert(std_core_initial_capacity()>=2 && std_core_json_depth() && std_core_json_nodes() && std_core_file_limit() && std_core_random_zero_seed());
    StdBuf *b=NULL; assert(!std_bytes_new(&b));
    assert(!std_bytes_append(b,B("abc"),3));
    assert(!std_bytes_append(b,b->data,b->len));
    assert(b->len==6 && !memcmp(b->data,"abcabc",6));
    assert(!std_bytes_reserve(b,32));
    assert(!std_bytes_append(b,b->data+1,4));
    assert(!std_bytes_push(b,'!'));
    uint8_t *view;uint64_t len;
    std_bytes_view(b,&view,&len);assert(len==11 && view==b->data);
    assert(std_bytes_compare(B("a"),1,B("b"),1)<0);
    assert(std_bytes_compare(NULL,0,NULL,0)==0);
    uint64_t i=99;assert(!std_bytes_find(view,len,B("cab"),3,&i) && i==2);
    assert(std_bytes_find(view,len,B("zz"),2,&i)==STD_MISSING && i==2);
    assert(!std_bytes_find(NULL,0,NULL,0,&i) && i==0);
    assert(!std_bytes_copy(view+1,10,view,10));
    assert(std_bytes_copy(view,0,view,1)==STD_RANGE);
    size_t old=b->cap;budget=0;
    assert(std_bytes_reserve(b,old+1)==STD_NOMEM && b->cap==old && b->len==11);
    budget=-1;std_bytes_clear(b);assert(!b->len);std_bytes_destroy(b);std_bytes_destroy(NULL);
}
static void text(void) {
    assert(!std_text_validate(B("\xf0\x9f\x98\x80"),4));
    assert(!std_text_validate(NULL,0));
    const char *bad[]={"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\x80"};
    const size_t sizes[]={2,3,4,2,1};
    for(size_t i=0;i<5;i++) assert(std_text_validate(B(bad[i]),sizes[i])==STD_ENCODING);
    uint64_t i=0;uint32_t scalar=9;
    assert(!std_text_next(B("\xf0\x9f\x98\x80"),4,&i,&scalar) && i==4 && scalar==0x1f600);
    assert(std_text_next(NULL,0,&i,&scalar)==STD_RANGE && i==4);
    uint8_t ascii[]="AZaz09";std_text_lower_ascii(ascii,6);assert(!memcmp(ascii,"azaz09",6));
    std_text_upper_ascii(ascii,6);assert(!memcmp(ascii,"AZAZ09",6));
    int64_t s=9;uint64_t u=9;
    assert(!std_text_parse_i64(B("-9223372036854775808"),20,&s) && s==INT64_MIN);
    assert(std_text_parse_i64(B("9223372036854775808"),19,&s)==STD_RANGE && s==INT64_MIN);
    assert(!std_text_parse_u64(B("18446744073709551615"),20,&u) && u==UINT64_MAX);
    assert(std_text_parse_u64(B("18446744073709551616"),20,&u)==STD_RANGE && u==UINT64_MAX);
    assert(std_text_parse_i64(B("-"),1,&s)==STD_PARSE);
    assert(std_text_parse_u64(B("+1"),2,&u)==STD_PARSE);
    StdBuf *b;assert(!std_bytes_new(&b));assert(!std_text_format_i64(INT64_MIN,b));
    assert(b->len==20 && !memcmp(b->data,"-9223372036854775808",20));
    std_bytes_clear(b);assert(!std_text_format_u64(UINT64_MAX,b));
    assert(b->len==20 && !memcmp(b->data,"18446744073709551615",20));std_bytes_destroy(b);
}
static void containers(void) {
    StdVec *v;assert(!std_vec_new(&v));
    for(int64_t i=299;i>=0;i--) assert(!std_vec_push(v,i%73));
    assert(std_vec_len(v)==300);assert(!std_vec_insert(v,0,-9));
    int64_t n;assert(!std_vec_remove(v,0,&n) && n==-9);assert(!std_vec_set(v,0,1000));
    assert(!std_vec_get(v,0,&n) && n==1000);n=7;
    assert(std_vec_get(v,300,&n)==STD_RANGE && n==7);
    assert(std_vec_insert(v,301,0)==STD_RANGE);
    std_vec_sort(v);for(uint64_t i=1;i<v->len;i++)assert(v->data[i-1]<=v->data[i]);
    uint64_t pos=99;assert(!std_vec_search(v,0,&pos) && pos==0);
    assert(std_vec_search(v,-1,&pos)==STD_MISSING && pos==0);
    budget=0;assert(std_vec_reserve(v,10000)==STD_NOMEM && std_vec_len(v)==300);budget=-1;
    std_vec_destroy(v);std_vec_destroy(NULL);
    StdMap *m;assert(!std_map_new(&m));
    char key[32];
    for(int i=0;i<300;i++) { int len=snprintf(key,sizeof(key),"k%d",i);assert(!std_map_set(m,B(key),(uint64_t)len,i)); }
    assert(std_map_len(m)==300);
    for(int i=0;i<300;i++) { int len=snprintf(key,sizeof(key),"k%d",i);assert(!std_map_get(m,B(key),(uint64_t)len,&n)&&n==i); }
    assert(!std_map_set(m,NULL,0,42));assert(!std_map_get(m,NULL,0,&n)&&n==42);
    for(int i=0;i<150;i++) { int len=snprintf(key,sizeof(key),"k%d",i);assert(!std_map_remove(m,B(key),(uint64_t)len)); }
    for(int i=0;i<150;i++) { int len=snprintf(key,sizeof(key),"k%d",i);assert(!std_map_set(m,B(key),(uint64_t)len,-i)); }
    assert(std_map_len(m)==301);uint64_t cursor=0,len,count=0;uint8_t *borrow;
    while(!std_map_next(m,&cursor,&borrow,&len,&n))++count;
    assert(count==301);budget=0;
    assert(std_map_set(m,B("new"),3,1)==STD_NOMEM && std_map_len(m)==301);
    budget=-1;std_map_destroy(m);std_map_destroy(NULL);
    for(long k=0;k<3;k++) {
        assert(!std_map_new(&m));budget=k;
        int status=std_map_set(m,B("initial"),7,17);budget=-1;
        if(status) assert(status==STD_NOMEM && std_map_len(m)==0);
        else assert(!std_map_get(m,B("initial"),7,&n) && n==17);
        std_map_destroy(m);
    }
}
static void numbers(void) {
    int64_t out=11;
    assert(std_math_add_i64(INT64_MAX,1,&out)==STD_RANGE && out==11);
    assert(std_math_sub_i64(INT64_MIN,1,&out)==STD_RANGE && out==11);
    assert(std_math_mul_i64(INT64_MIN,-1,&out)==STD_RANGE && out==11);
    assert(!std_math_mul_i64(INT64_MIN,1,&out) && out==INT64_MIN);
    const int64_t edge[]={INT64_MIN,INT64_MIN+1,-3037000500,-2,-1,0,1,2,3037000500,INT64_MAX-1,INT64_MAX};
    for(size_t i=0;i<sizeof(edge)/sizeof(*edge);i++) for(size_t j=0;j<sizeof(edge)/sizeof(*edge);j++) {
        __int128 product=(__int128)edge[i]*edge[j];out=17;
        int s=std_math_mul_i64(edge[i],edge[j],&out);
        if(product<INT64_MIN || product>INT64_MAX) assert(s==STD_RANGE && out==17);
        else assert(!s && out==(int64_t)product);
    }
    for(int64_t a=-500;a<500;a+=7) for(int64_t b=-400;b<400;b+=11) {
        assert(!std_math_add_i64(a,b,&out)&&out==a+b);
        assert(!std_math_sub_i64(a,b,&out)&&out==a-b);
        assert(!std_math_mul_i64(a,b,&out)&&out==a*b);
    }
    assert(std_math_sqrt(9)==3 && isnan(std_math_sqrt(-1)));
    assert(std_math_sin(0)==0 && std_math_cos(0)==1 && std_math_log(1)==0);
    assert(std_math_exp(0)==1 && std_math_pow(2,3)==8 && std_math_floor(1.5)==1 && std_math_ceil(1.5)==2);
    StdRng rng;std_random_seed(&rng,1);
    assert(std_random_next(&rng)==UINT64_C(5180492295206395165));
    uint64_t n=17,state=rng.state;assert(std_random_bounded(&rng,0,&n)==STD_INVALID && n==17 && rng.state==state);
    for(int i=0;i<1000;i++){assert(!std_random_bounded(&rng,7,&n));assert(n<7);}
    std_random_seed(&rng,0);assert(rng.state==std_core_random_zero_seed());
    uint8_t bytes[16];std_random_fill(&rng,bytes,sizeof(bytes));
    // Invert one xorshift64* output to force the low rejected interval.
    uint64_t inverse=1,multiplier=UINT64_C(2685821657736338717);
    for(int i=0;i<6;i++) inverse*=2-multiplier*inverse;
    uint64_t state_for_one=inverse;
    for(unsigned s=27;s<64;s*=2)state_for_one^=state_for_one>>s;
    for(unsigned s=25;s<64;s*=2)state_for_one^=state_for_one<<s;
    for(unsigned s=12;s<64;s*=2)state_for_one^=state_for_one>>s;
    std_random_seed(&rng,state_for_one);assert(std_random_next(&rng)==1);
    uint64_t expected=std_random_next(&rng)%2,expected_state=rng.state;
    std_random_seed(&rng,state_for_one);assert(!std_random_bounded(&rng,2,&n) && n==expected && rng.state==expected_state);
}
static void io_time(void) {
    char path[]="/tmp/lm0-stdlib-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(!std_io_write_file(B(path),strlen(path),B("hello"),5));
    StdBuf *b=(void *)1;assert(std_io_read_file(B(path),strlen(path),4,&b)==STD_LIMIT && b==(void *)1);
    assert(!std_io_read_file(B(path),strlen(path),5,&b) && b->len==5 && !memcmp(b->data,"hello",5));std_bytes_destroy(b);
    StdFile *f;assert(!std_io_open(B(path),strlen(path),2,&f));uint64_t n;
    assert(!std_io_write(f,B("!"),1,&n) && n==1);assert(!std_io_close(f));
    assert(!std_io_read_file(B(path),strlen(path),100,&b) && b->len==6);std_bytes_destroy(b);unlink(path);
    assert(std_io_read_file(B(path),strlen(path),100,&b)==STD_IO);
    assert(std_io_open(B("a\0b"),3,0,&f)==STD_INVALID);assert(!std_io_close(NULL));
    assert(!std_io_stdout(NULL,0) && !std_io_stderr(NULL,0));
    int pipes[2];assert(!pipe(pipes));close(pipes[0]);StdFile broken={pipes[1]};n=123;
    assert(std_io_write(&broken,B("x"),1,&n)==STD_IO && n==0);close(pipes[1]);
    sigset_t pending;assert(!sigpending(&pending) && !sigismember(&pending,SIGPIPE));
    assert(!pipe(pipes));assert(fcntl(pipes[1],F_SETFL,O_NONBLOCK)>=0);StdFile partial={pipes[1]};
    uint8_t large[131072]={0};assert(std_io_write(&partial,large,sizeof(large),&n)==STD_IO && n>0 && n<sizeof(large));
    close(pipes[0]);close(pipes[1]);
    uint64_t start,end,elapsed=99;int64_t unix_ns;
    assert(!std_time_monotonic(&start));assert(!std_time_sleep(1000));assert(!std_time_monotonic(&end));
    assert(end>=start && !std_time_elapsed(start,end,&elapsed) && elapsed==end-start);
    assert(std_time_elapsed(10,9,&elapsed)==STD_RANGE);assert(!std_time_unix(&unix_ns) && unix_ns>0);
}
static void json_tests(void) {
    const char *input=" {\"name\":\"\\uD83D\\uDE00\\n\",\"n\":-42,\"values\":[null,false,true,0,1.25e+3,{},[]]} ";
    StdJson *doc;assert(!std_json_parse(B(input),strlen(input),0,&doc));
    StdJsonNode *root=std_json_root(doc),*node;
    assert(std_json_kind(root)==6 && root->kind==6 && root->owner==doc);
    assert(!std_json_get(root,B("n"),1,&node));int64_t n;
    assert(!std_json_i64(node,&n) && n==-42);
    assert(!std_json_get(root,B("name"),4,&node));uint8_t *data;uint64_t len;
    assert(!std_json_text(node,&data,&len) && len==5 && !memcmp(data,"\xf0\x9f\x98\x80\n",5));
    assert(!std_json_get(root,B("values"),6,&node));StdJsonNode *item;
    assert(!std_json_at(node,4,&item));assert(std_json_i64(item,&n)==STD_PARSE);
    assert(std_json_at(node,7,&item)==STD_RANGE);
    assert(std_json_text(root,&data,&len)==STD_INVALID);
    assert(std_json_get(root,B("missing"),7,&node)==STD_MISSING);
    StdBuf *serialized;assert(!std_json_stringify(doc,0,1000,&serialized));
    StdJson *again;assert(!std_json_parse(serialized->data,serialized->len,0,&again));
    StdBuf *twice;assert(!std_json_stringify(again,0,1000,&twice));
    assert(serialized->len==twice->len && !memcmp(serialized->data,twice->data,twice->len));
    std_bytes_destroy(twice);std_json_destroy(again);std_bytes_destroy(serialized);
    serialized=(void *)1;assert(std_json_stringify(doc,0,2,&serialized)==STD_LIMIT && serialized==(void *)1);
    assert(std_json_stringify(doc,1,1000,&serialized)==STD_LIMIT && serialized==(void *)1);
    size_t baseline=live;
    for(long k=0;k<30;k++) {
        budget=k;serialized=(void *)1;int s=std_json_stringify(doc,0,1000,&serialized);budget=-1;
        if (!s) std_bytes_destroy(serialized); else assert(s==STD_NOMEM && serialized==(void *)1);
        assert(live==baseline);
    }
    std_json_destroy(doc);
    const char *bad[]={"", "[", "{", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "[true false]", "01", "-", "1.", "1e", "1e+", "+1", "nan", "true x", "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800\\u0041\"", "\"\\x\"", "\"a\n\"", "{\"a\":1,\"\\u0061\":2}", "[}", "{]"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        doc=(void *)1;assert(std_json_parse(B(bad[i]),strlen(bad[i]),0,&doc)!=0 && doc==(void *)1);assert(!live);
    }
    doc=(void *)1;assert(std_json_parse(B("[[0]]"),5,1,&doc)==STD_LIMIT && doc==(void *)1);
    for(long k=0;k<180;k++) {
        budget=k;doc=(void *)1;int s=std_json_parse(B(input),strlen(input),0,&doc);budget=-1;
        if (!s) std_json_destroy(doc);else assert(s==STD_NOMEM && doc==(void *)1);
        assert(!live);
    }
    assert(!std_json_new(&doc));assert(!std_json_root(doc));
    assert(std_json_stringify(doc,0,100,&serialized)==STD_INVALID);
    assert(!std_json_create(doc,6,NULL,0,&root));assert(!std_json_set_root(doc,root));
    assert(!std_json_create(doc,3,B("17"),2,&node));assert(!std_json_set(root,B("n"),1,node));
    assert(std_json_set_root(doc,node)==STD_INVALID);
    assert(!std_json_create(doc,5,NULL,0,&item));assert(!std_json_set(root,B("n"),1,item));
    assert(!std_json_append(item,node));assert(std_json_append(item,root)==STD_INVALID);
    assert(std_json_append(item,item)==STD_INVALID);
    assert(!std_json_stringify(doc,0,100,&serialized));assert(serialized->len==10 && !memcmp(serialized->data,"{\"n\":[17]}",10));
    std_bytes_destroy(serialized);
    assert(std_json_create(doc,3,B("1x"),2,&node)==STD_PARSE);
    assert(std_json_create(doc,4,B("\xff"),1,&node)==STD_ENCODING);
    assert(std_json_create(doc,-1,NULL,0,&node)==STD_INVALID);
    std_json_destroy(doc);std_json_destroy(NULL);assert(!live);
    char deep[2049];memset(deep,'[',1024);memset(deep+1024,']',1024);deep[2048]=0;
    assert(!std_json_parse(B(deep),2048,1024,&doc));
    assert(!std_json_stringify(doc,1024,2048,&serialized) && serialized->len==2048);
    std_bytes_destroy(serialized);std_json_destroy(doc);assert(!live);
    const char *controls="\"\\u0000\\b\\f\\n\\r\\t\\\\\\/\\\"\"";
    assert(!std_json_parse(B(controls),strlen(controls),0,&doc));
    assert(!std_json_stringify(doc,0,1000,&serialized));
    assert(!std_json_parse(serialized->data,serialized->len,0,&again));
    uint8_t *a,*b;uint64_t an,bn;
    assert(!std_json_text(std_json_root(doc),&a,&an));assert(!std_json_text(std_json_root(again),&b,&bn));
    assert(an==bn && !memcmp(a,b,an));std_bytes_destroy(serialized);std_json_destroy(doc);std_json_destroy(again);
    uint64_t random=1;
    for(unsigned i=0;i<500;i++) {
        char mutated[256];size_t size=strlen(input);memcpy(mutated,input,size);
        for(unsigned j=0;j<3;j++) { random=random*6364136223846793005ULL+1;mutated[random%size]=(char)((random>>32)%128); }
        doc=(void *)1;int s=std_json_parse(B(mutated),size,32,&doc);
        if(!s) {
            assert(!std_json_stringify(doc,32,4096,&serialized));
            assert(!std_json_parse(serialized->data,serialized->len,32,&again));
            std_json_destroy(again);std_bytes_destroy(serialized);std_json_destroy(doc);
        } else assert(doc==(void *)1);
        assert(!live);
    }
}
int main(void) {
    core_bytes();assert(!live);text();assert(!live);containers();assert(!live);numbers();assert(!live);io_time();assert(!live);json_tests();assert(!live);
    puts("Native standard-library tests passed");return 0;
}
