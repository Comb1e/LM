#define _POSIX_C_SOURCE 200809L
#include "eval_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *at(json_object *a,size_t i) { return json_object_get_string(json_object_array_get_idx(a,i)); }
static json_object *invoke(char *const argv[]) {
    Process p=process(argv,process_timeout);
    if(p.code || p.timed_out || p.limited) { fputs(p.out,stderr);fputs(p.err,stderr);die("Library verification command failed"); }
    json_object *result=parse_json(p.out);process_free(&p);return result;
}
int main(int argc,char **argv) {
    if(argc<5) die("Usage: library-check CATALOG COMPILER ARCHIVE LM0-SOURCES...");
    json_object *cat=read_json(argv[1]),*expected=json_object_new_object(),*seen=json_object_new_object();
    json_object *mods=member(cat,"modules",json_type_array);
    for(size_t i=0;i<json_object_array_length(mods);i++) {
        json_object *m=json_object_array_get_idx(mods,i),*fns=member(m,"functions",json_type_array);
        for(size_t j=0;j<json_object_array_length(fns);j++) {
            json_object *fn=json_object_array_get_idx(fns,j);char *name=format("%s_%s",string_member(m,"name"),at(fn,0));
            json_object_object_add(expected,name,json_object_get(fn));free(name);
        }
    }
    for(int i=4;i<argc;i++) {
        char *args[]={argv[2],"inspect",argv[i],"--module",NULL};json_object *module=invoke(args),*fns=member(module,"functions",json_type_array);
        for(size_t j=0;j<json_object_array_length(fns);j++) {
            json_object *fn=json_object_array_get_idx(fns,j);
            const char *name=string_member(fn,"name");json_object *want;
            if(!json_object_get_boolean(member(fn,"exported",json_type_boolean)) && strncmp(name,"std_",4)) continue;
            if(!json_object_object_get_ex(expected,name,&want)) die(format("Uncatalogued export: %s",name));
            if(strcmp(string_member(fn,"returns"),at(want,1))) die(format("Return type mismatch: %s",name));
            json_object *got=member(fn,"params",json_type_array),*params=json_object_array_get_idx(want,2);
            if(json_object_array_length(got)!=json_object_array_length(params)) die(format("Arity mismatch: %s",name));
            for(size_t k=0;k<json_object_array_length(params);k++)
                if(strcmp(string_member(json_object_array_get_idx(got,k),"type"),at(json_object_array_get_idx(params,k),1)))
                    die(format("Parameter type mismatch: %s",name));
        }
        json_object_put(module);
    }
    char *args[]={"nm","-g","--defined-only","--format=posix",argv[3],NULL};Process p=process(args,process_timeout);
    if(p.code || p.timed_out || p.limited) die("Cannot inspect library archive symbols");
    char *save=NULL;
    for(char *line=strtok_r(p.out,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        char name[256],kind;json_object *unused;
        if(sscanf(line,"%255s %c",name,&kind)!=2 || kind!='T') continue;
        if(!json_object_object_get_ex(expected,name,&unused)) die(format("Uncatalogued public function: %s",name));
        if(json_object_object_get_ex(seen,name,&unused)) die(format("Duplicate public function: %s",name));
        set_bool(seen,name,1);
    }
    json_object_object_foreach(expected,name,fn) {
        (void)fn;json_object *unused;
        if(!json_object_object_get_ex(seen,name,&unused)) die(format("Missing public function: %s",name));
    }
    printf("Verified %zu standard-library signatures and exports\n",(size_t)json_object_object_length(expected));
    process_free(&p);json_object_put(seen);json_object_put(expected);json_object_put(cat);return 0;
}
