#define _GNU_SOURCE
#include "eval_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static json_object *inspect(const char *compiler,const char *file,const char *function) {
    char *args[]={(char *)compiler,"inspect",(char *)file,function?"--function":"--module",(char *)function,"--view","compact",NULL};
    if(!function) args[4]=NULL;
    Process p=process(args,process_timeout);
    if(p.code || p.timed_out || p.limited) {fputs(p.out,stderr);die("Cannot inspect measurement source");}
    json_object *r=parse_json(p.out);process_free(&p);return r;
}
static void artifact(json_object *artifacts,const char *dir,const char *name,const char *text) {
    char *path=format("%s/%s",dir,name),hash[65];write_text(path,text);lm0_sha256(text,strlen(text),hash);
    json_object *item=json_object_new_object();set_string(item,"file",name);set_integer(item,"bytes",(int64_t)strlen(text));set_string(item,"sha256",hash);
    json_object_object_add(item,"tokens",NULL);json_object_array_add(artifacts,item);free(path);
}
static void verify(const char *compiler,const char *file) {
    char *args[]={(char *)compiler,"check",(char *)file,NULL};Process p=process(args,process_timeout);
    if(p.code || p.timed_out || p.limited) {fputs(p.out,stderr);die("Manual-interface baseline did not verify");}
    process_free(&p);
}
int main(int argc,char **argv) {
    if(argc<4) die("Usage: library-measure COMPILER OUTPUT-DIRECTORY SOURCES...");
    if(mkdir(argv[2],0755)) die("Measurement output directory must be new and have an existing parent");
    json_object *report=json_object_new_object(),*pairs=json_object_new_array(),*artifacts=json_object_new_array();
    set_string(report,"protocol","stdlib-bytes-1");
    set_string(report,"baseline","Original bodies and data, imports replaced by only the needed exact type definitions and foreign declarations. Context selects main in compact view. Raw manual context omits library contracts; enriched manual context restores identical library guidance. Byte measurements are not tokenizer counts.");
    json_object_object_add(report,"pairs",pairs);json_object_object_add(report,"artifacts",artifacts);json_object_object_add(report,"tokenizer",NULL);
    for(int arg=3;arg<argc;arg++) {
        const char *file=argv[arg],*base=strrchr(file,'/');base=base?base+1:file;
        char *name=strdup(base),*dot=strrchr(name,'.');if(dot)*dot=0;
        char *source=read_text(file);json_object *module=inspect(argv[1],file,NULL),*types=json_object_new_object(),*apis=json_object_new_object();
        json_object *functions=member(module,"functions",json_type_array);
        for(size_t i=0;i<json_object_array_length(functions);i++) {
            json_object *fn=json_object_array_get_idx(functions,i);
            if(json_object_get_boolean(member(fn,"external",json_type_boolean)))continue;
            json_object *context=inspect(argv[1],file,string_member(fn,"name")),*required=member(context,"types",json_type_array);
            for(size_t j=0;j<json_object_array_length(required);j++) {
                json_object *type=json_object_array_get_idx(required,j);json_object_object_add(types,string_member(type,"name"),json_object_get(type));
            }
            json_object *contracts=member(member(context,"library",json_type_object),"functions",json_type_array);
            for(size_t j=0;j<json_object_array_length(contracts);j++) {
                json_object *api=json_object_array_get_idx(contracts,j);json_object_object_add(apis,string_member(api,"name"),json_object_get(api));
            }
            json_object_put(context);
        }
        char *manual=NULL;size_t manual_len=0;FILE *out=open_memstream(&manual,&manual_len);
        const char *first=strchr(source,'\n');if(!first)die("Missing module header");
        fwrite(source,1,(size_t)(first+1-source),out);
        json_object *ordered_types=member(module,"types",json_type_array);
        for(size_t i=0;i<json_object_array_length(ordered_types);i++) {
            json_object *type=json_object_array_get_idx(ordered_types,i),*used;const char *type_name=string_member(type,"name");
            if(!json_object_object_get_ex(types,type_name,&used))continue;
            fprintf(out,"struct %s {\n",type_name);json_object *fields=member(type,"fields",json_type_array);
            for(size_t j=0;j<json_object_array_length(fields);j++) {
                json_object *field=json_object_array_get_idx(fields,j);fprintf(out,"    %s:%s\n",string_member(field,"name"),string_member(field,"type"));
            }
            fputs("}\n",out);
        }
        json_object_object_foreach(apis,symbol,api) { (void)symbol;fprintf(out,"%s\n",string_member(api,"signature")); }
        // Inputs have already been parsed. A line beginning with the use keyword
        // can only be a module declaration; strings cannot contain raw newlines.
        for(const char *line=first+1;*line;) {
            const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);else end++;
            const char *p=line;while(*p==' '||*p=='\t')p++;
            if(strncmp(p,"use",3) || (p[3]!=' ' && p[3]!='\t'))fwrite(line,1,(size_t)(end-line),out);
            line=end;
        }
        fclose(out);
        char *import_name=format("%s.imports.lm0",name),*manual_name=format("%s.manual.lm0",name),*manual_path=format("%s/%s",argv[2],manual_name);
        artifact(artifacts,argv[2],import_name,source);artifact(artifacts,argv[2],manual_name,manual);verify(argv[1],manual_path);
        json_object *import_context=inspect(argv[1],file,"main"),*manual_context=inspect(argv[1],manual_path,"main"),*pair=json_object_new_object();
        const char *import_json=json_object_to_json_string_ext(import_context,JSON_C_TO_STRING_PLAIN);
        const char *manual_json=json_object_to_json_string_ext(manual_context,JSON_C_TO_STRING_PLAIN);
        set_string(pair,"example",name);set_integer(pair,"source_import_bytes",(int64_t)strlen(source));set_integer(pair,"source_manual_bytes",(int64_t)manual_len);
        set_integer(pair,"context_import_bytes",(int64_t)strlen(import_json));set_integer(pair,"context_manual_bytes",(int64_t)strlen(manual_json));
        char *ci=format("%s.imports.context.json",name),*cm=format("%s.manual.context.json",name),*ce=format("%s.manual.contracts.json",name);
        artifact(artifacts,argv[2],ci,import_json);artifact(artifacts,argv[2],cm,manual_json);
        json_object_object_add(manual_context,"library",json_object_get(member(import_context,"library",json_type_object)));
        const char *enriched=json_object_to_json_string_ext(manual_context,JSON_C_TO_STRING_PLAIN);
        set_integer(pair,"context_manual_with_contracts_bytes",(int64_t)strlen(enriched));artifact(artifacts,argv[2],ce,enriched);
        json_object_array_add(pairs,pair);json_object_put(import_context);json_object_put(manual_context);json_object_put(types);json_object_put(apis);json_object_put(module);
        free(ci);free(cm);free(ce);free(source);free(manual);free(name);free(import_name);free(manual_name);free(manual_path);
    }
    char *output=format("%s/report.json",argv[2]);write_json(output,report);puts(output);free(output);json_object_put(report);return 0;
}
