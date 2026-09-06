#define _POSIX_C_SOURCE 200809L
#include "eval_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <inttypes.h>

typedef struct Output { char *path, *text; size_t size; struct Output *next; } Output;
static Output *outputs;

static FILE *open_output(const char *dir, const char *name) {
    Output *output = allocate(sizeof(*output));
    output->path = format("%s/%s", dir, name);
    output->next = outputs;
    outputs = output;
    FILE *f = open_memstream(&output->text, &output->size);
    if (!f) die("Cannot write library artifact");
    return f;
}
static void identifier(const char *s) {
    if (!*s) die("Empty catalogue identifier");
    for (size_t i=0;s[i];i++) {
        unsigned char c=(unsigned char)s[i];
        if (c=='_' || (c>='A' && c<='Z') || (c>='a' && c<='z') || (i && c>='0' && c<='9')) continue;
        die("Invalid catalogue identifier");
    }
}
static const char *str(json_object *a, size_t i) {
    json_object *v = json_object_array_get_idx(a, i);
    if (!v || json_object_get_type(v) != json_type_string) die("Invalid catalogue string");
    return json_object_get_string(v);
}
static void ctype(FILE *f, const char *t) {
    size_t n = strlen(t);
    if (n > 5 && !strncmp(t, "ptr<", 4) && t[n-1] == '>') {
        char *inner = strndup(t+4, n-5);
        ctype(f, inner); fputs(" *", f); free(inner); return;
    }
    if (!strcmp(t,"void")) fputs("void", f);
    else if (!strcmp(t,"bool")) fputs("_Bool", f);
    else if (!strcmp(t,"f64")) fputs("double", f);
    else if (!strcmp(t,"f32")) fputs("float", f);
    else if (t[0]=='i' || t[0]=='u') fprintf(f,"%sint%s_t", t[0]=='u'?"u":"", t+1);
    else fprintf(f,"struct %s",t);
}
static char *signature(const char *module, json_object *fn) {
    char *s = NULL; size_t n = 0; FILE *f = open_memstream(&s,&n);
    fprintf(f,"extern c fn @%s_%s(",module,str(fn,0));
    json_object *p = json_object_array_get_idx(fn,2);
    if (!p || json_object_get_type(p)!=json_type_array) die("Invalid parameters");
    for (size_t i=0;i<json_object_array_length(p);i++) {
        json_object *v=json_object_array_get_idx(p,i);
        fprintf(f,"%s%%%s:%s",i?", ":"",str(v,0),str(v,1));
    }
    fprintf(f,") -> %s",str(fn,1)); fclose(f); return s;
}
static char *v3_names(const char *source) {
    char *result=allocate(strlen(source)+1),*p=result;
    for(;*source;source++)if(*source!='@'&&*source!='%')*p++=*source;
    return result;
}
static void asm_string(FILE *f,const char *label,const char *value) {
    fprintf(f,"%s: .byte ",label);
    for (const unsigned char *p=(const unsigned char *)value;*p;p++) fprintf(f,"%u,",*p);
    fputs("0\n",f);
}
int main(int argc,char **argv) {
    if (argc!=3) die("Usage: library-gen CATALOG OUTPUT-DIRECTORY");
    char *raw=read_text(argv[1]),hash[65]; lm0_sha256(raw,strlen(raw),hash);
    json_object *cat=parse_json(raw),*mods=member(cat,"modules",json_type_array);
    json_object *types=member(cat,"types",json_type_object),*constants=member(cat,"constants",json_type_object);
    const char *policy=string_member(cat,"policy");
    if (integer_member(cat,"abi") != 1) die("Unsupported library ABI");
    if (integer_member(constants,"initial_capacity") < 2 || integer_member(constants,"initial_capacity") > INT_MAX)
        die("initial_capacity must be between 2 and INT_MAX");
    if (integer_member(constants,"json_depth") < 1 || integer_member(constants,"json_nodes") < 1 || integer_member(constants,"file_limit") < 1)
        die("Library limits must be positive");
    const char *seed_text=string_member(constants,"random_zero_seed");char *seed_end;
    errno=0;uint64_t seed=strtoull(seed_text,&seed_end,10);
    if(errno || !seed || *seed_end || seed_text[0]<'0' || seed_text[0]>'9') die("random_zero_seed must be a nonzero decimal u64");
    size_t count=json_object_array_length(mods);
    if (!count || count>63) die("Library module count exceeds native bitset");
    FILE *a=open_output(argv[2],"catalog.inc"),*h=open_output(argv[2],"std.h"),*d=open_output(argv[2],"reference.md");
    fputs(".section .rodata\n",a); asm_string(a,"library_identity",hash);
    asm_string(a,"library_policy",policy);
    fputs("#ifndef LM0_STD_H\n#define LM0_STD_H\n#include <stdint.h>\n#include <stddef.h>\n",h);
    json_object *statuses=member(cat,"statuses",json_type_array);
    static const char *const status_names[]={"OK","INVALID","RANGE","NOMEM","MISSING","ENCODING","PARSE","IO","EOF","LIMIT"};
    if(json_object_array_length(statuses)!=10) die("ABI 1 requires ten status codes");
    fputs("enum { ",h);
    for(size_t i=0;i<json_object_array_length(statuses);i++) {
        json_object *status=json_object_array_get_idx(statuses,i),*code=json_object_array_get_idx(status,1);
        identifier(str(status,0));
        if(strcmp(str(status,0),status_names[i])) die("Status names must retain their ABI indices");
        if(!json_object_is_type(code,json_type_int) || json_object_get_int64(code)!=(int64_t)i) die("Status codes must retain their ABI indices");
        fprintf(h,"%sSTD_%s = %zu",i?", ":"",str(status,0),i);
    }
    fputs(" };\n#define STD_STATUS_NAMES { ",h);
    for(size_t i=0;i<json_object_array_length(statuses);i++) {
        json_object *status=json_object_array_get_idx(statuses,i);
        (void)str(status,2);
        fprintf(h,"%s%s",i?", ":"",json_object_to_json_string_ext(json_object_array_get_idx(status,2),JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE));
    }
    fputs(" }\n",h);
    json_object_object_foreach(types,name,fields) { (void)fields; identifier(name); fprintf(h,"typedef struct %s %s;\n",name,name); }
    char *type_source=NULL;size_t tn=0;FILE *tf=open_memstream(&type_source,&tn);
    json_object_object_foreach(types,name2,fields2) {
        fprintf(h,"struct %s {\n",name2);fprintf(tf,"struct %s {\n",name2);
        for(size_t k=0;k<json_object_array_length(fields2);k++) {
            json_object *field=json_object_array_get_idx(fields2,k);
            identifier(str(field,0));
            ctype(h,str(field,1));fprintf(h," %s;\n",str(field,0));
            fprintf(tf,"%s:%s\n",str(field,0),str(field,1));
        }
        fputs("};\n",h);fputs("}\n",tf);
    }
    fclose(tf);
    fprintf(h,"#define STD_CATALOG_ID \"%s\"\n",hash);
    fprintf(h,"#define STD_ABI_SYMBOL std_abi_%s\n",hash);
    json_object_object_foreach(constants,key,value) {
        identifier(key);
        char *upper=strdup(key);for(char *p=upper;*p;p++) if(*p>='a'&&*p<='z') *p-=32;
        if(!strcmp(key,"random_zero_seed")) fprintf(h,"#define STD_%s UINT64_C(%" PRIu64 ")\n",upper,seed);
        else fprintf(h,"#define STD_%s UINT64_C(%" PRId64 ")\n",upper,integer_member(constants,key));
        (void)value;free(upper);
    }
    fprintf(d,"# LM0 Standard Library\n\nCatalogue `%s`.\n\n%s\n",hash,policy);
    json_object *listing=json_object_new_array();
    for(size_t i=0;i<count;i++) {
        json_object *m=json_object_array_get_idx(mods,i),*deps=member(m,"depends",json_type_array),*fns=member(m,"functions",json_type_array);
        const char *name=string_member(m,"name");
        identifier(name);
        if(strncmp(name,"std_",4) || (!i && strcmp(name,"std_core"))) die("Modules must use std_ names and start with std_core");
        for(size_t j=0;j<i;j++) if(!strcmp(name,string_member(json_object_array_get_idx(mods,j),"name"))) die("Duplicate module");
        char *src=NULL;size_t sn=0;FILE *sf=open_memstream(&src,&sn);
        for(size_t j=0;j<json_object_array_length(deps);j++) {
            const char *dep=str(deps,j);int found=0;
            for(size_t k=0;k<i;k++) if(!strcmp(dep,string_member(json_object_array_get_idx(mods,k),"name"))) found=1;
            if(!found) die("Dependencies must precede their users (no cycles)");
            fprintf(sf,"use %s\n",dep);
        }
        if(i==0) fputs(type_source,sf);
        json_object *desc=json_object_new_object(),*apis=json_object_new_array(),*apis3=json_object_new_array();
        set_bool(desc,"ok",1);set_string(desc,"module",name);set_string(desc,"catalogue",hash);set_string(desc,"policy",policy);
        set_string(desc,"summary",string_member(m,"summary"));json_object_object_add(desc,"depends",json_object_get(deps));
        fprintf(d,"\n## %s\n\n%s\n",name,string_member(m,"summary"));
        for(size_t j=0;j<json_object_array_length(fns);j++) {
            json_object *fn=json_object_array_get_idx(fns,j),*params=json_object_array_get_idx(fn,2);
            identifier(str(fn,0));
            char *sig=signature(name,fn),*symbol=format("%s_%s",name,str(fn,0));
            for(size_t k=0;k<j;k++) if(!strcmp(str(fn,0),str(json_object_array_get_idx(fns,k),0))) die("Duplicate function");
            fprintf(sf,"%s\n",sig);ctype(h,str(fn,1));fprintf(h," %s(",symbol);
            for(size_t k=0;k<json_object_array_length(params);k++) {
                if(k) fputs(", ",h);
                json_object *p=json_object_array_get_idx(params,k);
                identifier(str(p,0));
                ctype(h,str(p,1));fprintf(h," %s",str(p,0));
            }
            if(!json_object_array_length(params)) fputs("void",h);
            fputs(");\n",h);
            json_object *api=json_object_new_object();set_string(api,"name",symbol);set_string(api,"signature",sig);set_string(api,"contract",str(fn,3));
            char *example=NULL;size_t en=0;FILE *ef=open_memstream(&example,&en);
            fprintf(ef,"%scall @%s(",strcmp(str(fn,1),"void")?"%result = ":"",symbol);
            for(size_t k=0;k<json_object_array_length(params);k++) fprintf(ef,"%s%%%s",k?", ":"",str(json_object_array_get_idx(params,k),0));
            fputs(")",ef);fclose(ef);set_string(api,"call",example);free(example);
            json_object_array_add(apis,api);
            json_object *api3=json_object_new_object();set_string(api3,"name",symbol);
            char *sig3=v3_names(sig);set_string(api3,"signature",sig3);free(sig3);set_string(api3,"contract",str(fn,3));
            char *call3=NULL;size_t c3n=0;FILE *c3=open_memstream(&call3,&c3n);
            fprintf(c3,"%s%s(",strcmp(str(fn,1),"void")?"result = ":"",symbol);
            for(size_t k=0;k<json_object_array_length(params);k++)fprintf(c3,"%s%s",k?", ":"",str(json_object_array_get_idx(params,k),0));
            fputs(")",c3);fclose(c3);set_string(api3,"call",call3);free(call3);json_object_array_add(apis3,api3);
            char *v3label=format("library_api3_%zu_%zu",i,j);asm_string(a,v3label,json_object_to_json_string_ext(api3,JSON_C_TO_STRING_PLAIN));free(v3label);
            char *label=format("library_api_%zu_%zu",i,j);asm_string(a,label,json_object_to_json_string_ext(api,JSON_C_TO_STRING_PLAIN));free(label);
            label=format("library_symbol_%zu_%zu",i,j);asm_string(a,label,symbol);free(label);
            fprintf(d,"\n```text\n%s\n```\n%s\n",sig,str(fn,3));free(sig);free(symbol);
        }
        fclose(sf);json_object_object_add(desc,"functions",apis);
        char *label=format("library_name_%zu",i);asm_string(a,label,name);free(label);
        label=format("library_source_%zu",i);asm_string(a,label,src);free(label);
        label=format("library_description_%zu",i);asm_string(a,label,json_object_to_json_string_ext(desc,JSON_C_TO_STRING_PLAIN));free(label);
        json_object_object_add(desc,"functions",apis3);
        label=format("library_description3_%zu",i);asm_string(a,label,json_object_to_json_string_ext(desc,JSON_C_TO_STRING_PLAIN));free(label);
        fprintf(a,".align 8\nlibrary_apis_%zu:\n",i);
        for(size_t j=0;j<json_object_array_length(fns);j++)fprintf(a,".quad library_symbol_%zu_%zu,library_api_%zu_%zu\n",i,j,i,j);
        fputs(".quad 0,0\n",a);
        fprintf(a,".align 8\nlibrary_apis3_%zu:\n",i);
        for(size_t j=0;j<json_object_array_length(fns);j++)fprintf(a,".quad library_symbol_%zu_%zu,library_api3_%zu_%zu\n",i,j,i,j);
        fputs(".quad 0,0\n",a);
        json_object *item=json_object_new_object();set_string(item,"module",name);set_string(item,"summary",string_member(m,"summary"));json_object_array_add(listing,item);
        char *file=format("%s.lmi",name);FILE *interface=open_output(argv[2],file);fprintf(interface,"module %s version 2\n%s",name,src);fclose(interface);free(file);free(src);json_object_put(desc);
    }
    json_object *list=json_object_new_object();set_bool(list,"ok",1);set_string(list,"catalogue",hash);json_object_object_add(list,"modules",listing);
    asm_string(a,"library_listing",json_object_to_json_string_ext(list,JSON_C_TO_STRING_PLAIN));
    fprintf(a,".equ LIBRARY_COUNT,%zu\n.align 8\nlibrary_catalog:\n",count);
    for(size_t i=0;i<count;i++)fprintf(a,".quad library_name_%zu,library_source_%zu,library_description_%zu,library_apis_%zu\n",i,i,i,i);
    fputs(".align 8\nlibrary_catalog_v3:\n",a);
    for(size_t i=0;i<count;i++)fprintf(a,".quad library_name_%zu,library_source_%zu,library_description3_%zu,library_apis3_%zu\n",i,i,i,i);
    fputs(".text\n",a);fputs("#endif\n",h);
    if(fclose(a)||fclose(h)||fclose(d))die("Cannot finish library artifacts");
    FILE *identity=open_output(argv[2],"catalog.id");fprintf(identity,"%s\n",hash);fclose(identity);
    // Publish only after the entire catalogue validates; the compiler include
    // is committed last so interrupted generation is retried by make.
    while(outputs) {
        Output *next=outputs->next;
        write_text(outputs->path,outputs->text);
        free(outputs->path);free(outputs->text);free(outputs);outputs=next;
    }
    free(type_source);free(raw);json_object_put(cat);json_object_put(list);return 0;
}
