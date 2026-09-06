#define _GNU_SOURCE
#include "eval_common.h"
#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[]="/tmp/lm0-library-tools-XXXXXX";
static char *compiler;
static void copy_file(const char *from,const char *to) {
    FILE *in=fopen(from,"rb"),*out=fopen(to,"wb");assert(in && out);
    char bytes[8192];size_t n;
    while((n=fread(bytes,1,sizeof(bytes),in))) assert(fwrite(bytes,1,n,out)==n);
    assert(!ferror(in));assert(!fclose(in));assert(!fclose(out));
}
static char *path(const char *name) { return format("%s/%s",directory,name); }
static void command(char *const args[]) {
    Process p=process(args,process_timeout);
    if(p.code || p.timed_out || p.limited) { fputs(p.out,stderr);fputs(p.err,stderr);abort(); }
    process_free(&p);
}
static json_object *json_command(int code,char *const args[]) {
    Process p=process(args,process_timeout);
    if(p.code!=code || p.timed_out || p.limited) { fputs(p.out,stderr);fputs(p.err,stderr);abort(); }
    json_object *result=parse_json(p.out);process_free(&p);return result;
}
static void error_code(json_object *r,const char *code) {
    json_object *d=json_object_array_get_idx(member(r,"diagnostics",json_type_array),0);
    assert(!strcmp(string_member(d,"code"),code));json_object_put(r);
}
static const char source_text[]="module library_test version 2\nuse std_math\nuse std_bytes\nuse std_math\n"
    "export c fn @answer() -> i64 {\n^entry:\n%out = stack i64, 1\n%status = call @std_math_add_i64(20, 22, %out)\n%v = load %out\nreturn %v\n}\n"
    "fn @main() -> i32 {\n^entry:\n%v = call @answer()\n%r:i32 = cast %v\nreturn %r\n}\n";
static void tools_tests(void) {
    char *source=path("source.lm0"),*output=path("program"),*object=path("module.o"),*assembly=path("module.s"),*shared=path("module.so");
    write_text(source,source_text);
    char *list[]={compiler,"library","list",NULL};json_object *r=json_command(0,list);
    assert(json_object_array_length(member(r,"modules",json_type_array))==13);json_object_put(r);
    char *describe[]={compiler,"library","describe","std_math","std_math_add_i64",NULL};r=json_command(0,describe);
    assert(json_object_array_length(member(r,"functions",json_type_array))==1);assert(strlen(string_member(r,"policy")));json_object_put(r);
    char *unknown[]={compiler,"library","describe","std_nothing",NULL};error_code(json_command(2,unknown),"E_LIBRARY");
    char *inspect[]={compiler,"inspect",source,"--function","answer","--view","compact",NULL};r=json_command(0,inspect);
    char hash[65];assert(lm0_sha256(source_text,strlen(source_text),hash));assert(!strcmp(string_member(r,"revision"),hash));
    assert(json_object_get_boolean(member(member(r,"validation",json_type_object),"ok",json_type_boolean)));
    json_object *functions=member(member(r,"library",json_type_object),"functions",json_type_array);
    assert(json_object_array_length(functions)==1 && !strcmp(string_member(json_object_array_get_idx(functions,0),"name"),"std_math_add_i64"));
    Process again=process(inspect,process_timeout);json_object *other=parse_json(again.out);assert(json_object_equal(r,other));
    process_free(&again);json_object_put(other);json_object_put(r);
    char *build[]={compiler,"build",source,"-o",output,NULL};r=json_command(0,build);json_object_put(r);
    char *run[]={output,NULL};Process execution=process(run,process_timeout);assert(execution.code==42);process_free(&execution);
    char *obj[]={compiler,"build",source,"--kind","object","-o",object,NULL};r=json_command(0,obj);
    assert(json_object_array_length(member(member(r,"link_requirements",json_type_object),"archives",json_type_array))==1);json_object_put(r);
    char *emit[]={compiler,"emit-asm",source,"--entry","-o",assembly,NULL};r=json_command(0,emit);json_object_put(r);
    char *gcc[]={"gcc",assembly,"build/stdlib/liblm0std.a","-lm","-o",output,NULL};command(gcc);
    execution=process(run,process_timeout);assert(execution.code==42);process_free(&execution);
    char *so[]={compiler,"build",source,"--kind","shared","-o",shared,NULL};r=json_command(0,so);json_object_put(r);
    char *host=path("host.c"),*driver=path("host");
    write_text(host,"#include <dlfcn.h>\n#include <stdint.h>\nint main(int n,char **a){if(n!=2)return 1;void *h=dlopen(a[1],RTLD_NOW);if(!h)return 2;int64_t(*fn)(void)=dlsym(h,\"answer\");if(!fn||fn()!=42)return 3;if(dlsym(h,\"std_math_add_i64\"))return 4;return dlclose(h);}\n");
    char *cc[]={"gcc",host,"-ldl","-o",driver,NULL};command(cc);char *hostrun[]={driver,shared,NULL};command(hostrun);
    char *replacement=path("fragment.txt"),*fixed=path("fixed.lm0");write_text(replacement,"^entry:\nreturn 7\n");
    char *replace[]={compiler,"replace",source,"--function","answer","--block","entry","--replacement",replacement,"--expect-revision",hash,"-o",fixed,NULL};
    r=json_command(0,replace);json_object_put(r);char *changed=read_text(fixed);assert(strstr(changed,"use std_math"));free(changed);
    char *migrate[]={compiler,"migrate",source,"-o",fixed,NULL};r=json_command(0,migrate);json_object_put(r);
    changed=read_text(fixed);assert(strstr(changed,"use std_bytes"));free(changed);
    write_text(source,"module bad version 2\nuse std_math\nfn @main() -> i32 {\n^entry:\n%v = call @std_math_sqrt(1:i32)\nreturn 0\n}\n");
    char *check[]={compiler,"check",source,NULL};r=json_command(2,check);
    json_object *d=json_object_array_get_idx(member(r,"diagnostics",json_type_array),0);
    assert(integer_member(member(d,"span",json_type_object),"line")==5);error_code(r,"E_TYPE");
    char *invalid_inspect[]={compiler,"inspect",source,"--function","main","--view","compact",NULL};r=json_command(0,invalid_inspect);
    assert(!json_object_get_boolean(member(member(r,"validation",json_type_object),"ok",json_type_boolean)));json_object_put(r);
    write_text(source,"module bad version 2\nuse std_missing\n");error_code(json_command(2,check),"E_LIBRARY");
    write_text(source,"module bad version 1\nuse std_math\n");error_code(json_command(2,check),"E_VERSION");
    write_text(source,"module bad version 2\nuse std_bytes\nextern c fn @std_bytes_new() -> void\n");error_code(json_command(2,check),"E_DUPLICATE");
    write_text(source,"module bad version 2\nuse std_core\nstruct StdBuf {\nx:i32\n}\n");error_code(json_command(2,check),"E_DUPLICATE");
    write_text(source,"module bad version 2\nuse std_core\nexport c fn @std_bytes_new(%out:ptr<ptr<StdBuf>>) -> i64 {\n^entry:\nreturn 0\n}\n");
    char *signatures[]={"build/library-check","stdlib/catalog.json",compiler,"build/stdlib/liblm0std.a",source,NULL};r=json_command(2,signatures);
    assert(strstr(string_member(r,"error"),"Return type mismatch"));json_object_put(r);
    write_text(source,source_text);
    assert(!mkdir(path("bin"),0700));char *standalone=path("bin/lm0");copy_file(compiler,standalone);assert(!chmod(standalone,0700));
    check[0]=standalone;r=json_command(0,check);json_object_put(r);
    describe[0]=standalone;r=json_command(0,describe);json_object_put(r);
    obj[0]=standalone;r=json_command(0,obj);json_object_put(r);
    write_text(output,"preserved");build[0]=standalone;error_code(json_command(2,build),"E_LIBRARY");
    changed=read_text(output);assert(!strcmp(changed,"preserved"));free(changed);
    char *override[]={standalone,"build",source,"--stdlib-dir","build/stdlib","-o",output,NULL};r=json_command(0,override);json_object_put(r);
    char *dest=format("DESTDIR=%s",directory);char *install[]={"make","install",dest,"PREFIX=/usr",NULL};command(install);free(dest);
    assert(!rename(path("usr"),path("relocated")));char *installed=path("relocated/bin/lm0");build[0]=installed;
    r=json_command(0,build);json_object_put(r);
    char *identity=path("relocated/lib/lm0/catalog.id");write_text(identity,"incompatible\n");error_code(json_command(2,build),"E_LIBRARY");
    copy_file("build/stdlib/catalog.id",identity);
    r=json_command(0,build);json_object_put(r);
    char *catalog=path("catalog.json"),*generated=path("generated");assert(!mkdir(generated,0700));
    copy_file("stdlib/catalog.json",catalog);char *generate[]={"build/library-gen",catalog,generated,NULL};command(generate);
    char *inc=format("%s/catalog.inc",generated),*before=read_text(inc);
    json_object *bad_catalog=read_json(catalog);set_integer(member(bad_catalog,"constants",json_type_object),"initial_capacity",1);
    write_json(catalog,bad_catalog);json_object_put(bad_catalog);
    r=json_command(2,generate);assert(strstr(string_member(r,"error"),"initial_capacity"));json_object_put(r);
    char *after=read_text(inc);assert(!strcmp(before,after));free(before);free(after);free(inc);
    char *readme=read_text("docs/libraries.md"),*example=strstr(readme,"```text\nmodule hello_library");assert(example);
    example+=8;char *end=strstr(example,"\n```");assert(end);char *hello=strndup(example,(size_t)(end-example));
    write_text(source,hello);free(hello);free(readme);char *hello_run[]={compiler,"run",source,NULL};r=json_command(0,hello_run);
    assert(integer_member(r,"exit_code")==0 && !strcmp(string_member(r,"stdout"),"42"));json_object_put(r);
}
static void measurement_tests(void) {
    char *dir=path("comparison");char *measure[]={"build/library-measure",compiler,dir,"examples/stdlib/word_count.lm0","examples/stdlib/json_transform.lm0","examples/stdlib/statistics.lm0",NULL};command(measure);
    json_object *report=read_json(format("%s/report.json",dir)),*pairs=member(report,"pairs",json_type_array),*artifacts=member(report,"artifacts",json_type_array);
    assert(json_object_array_length(pairs)==3);
    for(size_t i=0;i<json_object_array_length(artifacts);i++) {
        json_object *a=json_object_array_get_idx(artifacts,i);char *text=read_text(format("%s/%s",dir,string_member(a,"file"))),hash[65];
        assert(integer_member(a,"bytes")== (int64_t)strlen(text));lm0_sha256(text,strlen(text),hash);assert(!strcmp(hash,string_member(a,"sha256")));free(text);
    }
    for(size_t i=0;i<json_object_array_length(pairs);i++) {
        json_object *pair=json_object_array_get_idx(pairs,i);const char *name=string_member(pair,"example");
        assert(integer_member(pair,"source_import_bytes")<integer_member(pair,"source_manual_bytes"));
        assert(integer_member(pair,"context_import_bytes")==integer_member(pair,"context_manual_with_contracts_bytes"));
        char *manual=format("%s/%s.manual.lm0",dir,name),*original=format("examples/stdlib/%s.lm0",name);
        char *run_manual[]={compiler,"run",manual,"--link","build/stdlib/liblm0std.a","--library","m",NULL},*run_original[]={compiler,"run",original,NULL};
        json_object *a=json_command(0,run_manual),*b=json_command(0,run_original);
        assert(integer_member(a,"exit_code")==integer_member(b,"exit_code"));assert(!strcmp(string_member(a,"stdout"),string_member(b,"stdout")));
        json_object_put(a);json_object_put(b);free(manual);free(original);
    }
    json_object_put(report);
}
static void examples(void) {
    char *words[]={compiler,"run","examples/stdlib/word_count.lm0",NULL};json_object *r=json_command(0,words);
    assert(integer_member(r,"exit_code")==0);const char *text=string_member(r,"stdout");
    assert(strstr(text,"lm0=2\n") && strstr(text,"libraries=2\n") && strstr(text,"programs=2\n"));json_object_put(r);
    char *json[]={compiler,"run","examples/stdlib/json_transform.lm0",NULL};r=json_command(0,json);
    assert(integer_member(r,"exit_code")==0);json_object *transformed=parse_json(string_member(r,"stdout"));
    assert(integer_member(transformed,"count")==42);json_object_put(transformed);json_object_put(r);
    char *stats[]={compiler,"run","examples/stdlib/statistics.lm0",NULL};r=json_command(0,stats);
    assert(integer_member(r,"exit_code")==0);assert(!strcmp(string_member(r,"stdout"),"sum=47221\nupper_median=423\n"));json_object_put(r);
}
static int cleanup(const char *p,const struct stat *s,int type,struct FTW *info) { (void)s;(void)type;(void)info;return remove(p); }
int main(void) {
    assert(mkdtemp(directory));compiler=realpath("build/lm0",NULL);assert(compiler);
    tools_tests();examples();measurement_tests();assert(!nftw(directory,cleanup,16,FTW_DEPTH|FTW_PHYS));free(compiler);
    puts("Library compiler, installation and example tests passed");return 0;
}
