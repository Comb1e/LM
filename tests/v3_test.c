#define _GNU_SOURCE
#include "eval_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *path,*root;
static unsigned checks;
static json_object *cli(const char *command,int expected) {
    char *args[]={"build/lm0",(char *)command,path,NULL};Process p=process(args,30);
    if(p.code!=expected||p.timed_out||p.limited)die(format("%s failed (%d): %s%s",command,p.code,p.out,p.err));
    json_object *r=parse_json(p.out);process_free(&p);return r;
}
static void program(const char *text) {write_text(path,text);}
static void succeeds(const char *text,int code) {
    program(text);json_object *r=cli("run",0);
    if(integer_member(r,"exit_code")!=code)die(format("Wrong V3 exit: %s",json_object_to_json_string(r)));
    json_object_put(r);checks++;
}
static void rejects(const char *text,const char *code) {
    program(text);json_object *r=cli("check",2);
    json_object *d=json_object_array_get_idx(member(r,"diagnostics",json_type_array),0);
    if(strcmp(string_member(d,"code"),code))die(format("Expected %s: %s",code,json_object_to_json_string(r)));
    json_object_put(r);checks++;
}
int main(void) {
    char temp[]="/tmp/lm0-v3-test-XXXXXX";root=mkdtemp(temp);if(!root)die("mkdtemp failed");path=format("%s/input.lm0",root);
    succeeds("module t version 3\nfn main() -> i32:\n    x = 1\n    for i in range(1, 6, 2):\n        if i == 3:\n            continue\n        x += i\n    while x < 10:\n        x += 1\n        if x == 9:\n            break\n    return cast<i32>(x)\n",9);
    succeeds("module t version 3\nfn fact(n:i64) -> i64:\n    if n < 2:\n        return 1\n    return n * fact(n - 1)\nfn main() -> i32:\n    return cast<i32>(fact(5))\n",120);
    succeeds("module t version 3\nfn inc(p:ptr<i32>) -> bool:\n    store(p, load(p) + 1)\n    return true\nfn main() -> i32:\n    p = stack<i32>(1)\n    store(p, 0)\n    a = false and inc(p)\n    b = true or inc(p)\n    c = true and inc(p)\n    return load(p)\n",1);
    succeeds("module t version 3\nfn main() -> i32:\n    p = stack<i64>(3)\n    a = view(p, 3)\n    b = a\n    a[0] = 10\n    b[1] = 20\n    a[2] = 12\n    return cast<i32>(b[0] + b[1] + b[2])\n",42);
    succeeds("module t version 3\nfn main() -> i32:\n    if true:\n        x = 42\n    else:\n        x = 0\n    return cast<i32>(x)\n",42);
    succeeds("module t version 3\nfn main() -> i32:\n    n = 0\n    for i in range(9223372036854775806, 9223372036854775807, 2):\n        n += 1\n    return cast<i32>(n)\n",1);
    succeeds("module t version 3\nfn main() -> i32:\n    n:u8 = 255\n    n += 1\n    x = 0x2a\n    return cast<i32>(n) + cast<i32>(x)\n",42);
    succeeds("module t version 3\nstruct Node:\n    value:i64\nfn main() -> i32:\n    p = stack<Node>(1)\n    store(field(p, value), 42)\n    return cast<i32>(load(field(p, value)))\n",42);
    rejects("module t version 3\nfn main() -> i32:\n    if true:\n        x = 1\n    return cast<i32>(x)\n","E_REGISTER");
    rejects("module t version 3\nfn main() -> i32:\n    for i in range(0):\n        x = 1\n    return cast<i32>(x)\n","E_REGISTER");
    rejects("module t version 3\nfn main() -> i32:\n    x = 1\n    x = false\n    return 0\n","E_TYPE");
    rejects("module t version 3\nfn main() -> i32:\n    break\n","E_BLOCK");
    rejects("module t version 3\nfn main() -> i32:\n    if 1:\n        return 0\n    return 1\n","E_TYPE");
    rejects("module t version 3\nfn main() -> i32:\n    return\n","E_TYPE");
    rejects("module t version 3\nfn main() -> i32:\n    return 1 + 2.5:f64\n","E_TYPE");
    rejects("module t version 3\nfn main() -> i32:\n    return (","E_SYNTAX");
    rejects("module t version 3\nfn main(","E_SYNTAX");
    rejects("module t version 3\nfn main() -> i32:\n    return 1 +\n","E_SYNTAX");
    rejects("module t version 3\nfn len(x:i64) -> i64:\n    return x\n","E_DUPLICATE");
    const char *traps[]={"a[-1]","a[1]"};
    for(size_t i=0;i<2;i++) {
        program(format("module t version 3\nfn main() -> i32:\n    p = stack<i64>(1)\n    a = view(p, 1)\n    a[0] = 0\n    return cast<i32>(%s)\n",traps[i]));
        json_object_put(cli("run",3));checks++;
    }
    const char *broken="module t version 3\nfn main() -> i32:\n    return missing(1)\n";
    program(broken);
    char *inspect[]={"build/lm0","inspect",path,"--function","main","--view","compact",NULL};
    Process p=process(inspect,30);if(p.code)die(p.out);json_object *r=parse_json(p.out);process_free(&p);
    if(json_object_get_boolean(member(member(r,"validation",json_type_object),"ok",json_type_boolean))||!strstr(string_member(r,"source"),"missing")||!json_object_array_length(member(r,"unresolved",json_type_array)))die("Invalid V3 inspection lost source or validation");
    const char *revision=string_member(r,"revision");char *fragment=format("%s/function.txt",root),*fixed=format("%s/fixed.lm0",root);
    write_text(fragment,"fn main() -> i32:\n    return 42\n");
    char *replace[]={"build/lm0","replace",path,"--function","main","--replacement",fragment,"--expect-revision",(char *)revision,"-o",fixed,NULL};
    p=process(replace,30);if(p.code)die(p.out);process_free(&p);
    char *old=path;path=fixed;json_object *run=cli("run",0);if(integer_member(run,"exit_code")!=42)die("Replacement execution failed");json_object_put(run);path=old;
    program(format("%s\n",broken));p=process(replace,30);if(p.code!=2||!strstr(p.out,"E_STALE"))die("Stale V3 replacement accepted");process_free(&p);
    char *before=read_text(fixed);
    program(broken);write_text(fragment,"fn main() -> i32:\n    return 0\nfn injected() -> i32:\n    return 0\n");p=process(replace,30);
    if(p.code!=2||strcmp(before,read_text(fixed)))die("V3 replacement was not atomic");process_free(&p);json_object_put(r);checks+=3;
    printf("Passed %u V3 control-flow, numeric, memory and repair checks.\n",checks);
    return 0;
}
