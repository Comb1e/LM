#define _GNU_SOURCE
#include "eval_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void run(char *const argv[]) {
    Process p=process(argv,60);
    if(p.code||p.timed_out||p.limited)die(format("Command failed (%d): %s%s",p.code,p.out,p.err));
    process_free(&p);
}
int main(void) {
    char root[]="/tmp/lm0-comparison-test-XXXXXX";
    if(!mkdtemp(root))die("mkdtemp failed");
    char *attempts_path=format("%s/attempts.json",root),*export_path=format("%s/export",root),*report_path=format("%s/report.json",root);
    json_object *rows=json_object_new_array();
    const char *versions[]={"python","v3"};
    for(size_t v=0;v<2;v++) {
        json_object *refs=read_json(format("evaluation/%s.json",versions[v]));
        json_object_object_foreach(refs,task,source) {
            json_object *row=json_object_new_object();set_string(row,"task",task);set_string(row,"version",versions[v]);set_integer(row,"trial",0);set_integer(row,"attempt",0);
            set_string(row,"input","Synthetic evaluator regression fixture; not a model trial");
            set_string(row,"response",json_object_get_string(source));set_string(row,"source",json_object_get_string(source));json_object_array_add(rows,row);
        }
        json_object_put(refs);
    }
    json_object *apps=read_json("evaluation/applications.json");
    const char *app_versions[]={"v2","v3","python"};
    for(size_t i=0;i<json_object_array_length(apps);i++) {
        json_object *app=json_object_array_get_idx(apps,i);const char *name=string_member(app,"name");
        for(size_t v=0;v<3;v++) {
            char *path=v==0?format("examples/stdlib/%s.lm0",name):v==1?format("examples/v3/%s.lm0",name):format("evaluation/python/%s.py",name);
            char *src=read_text(path);json_object *row=json_object_new_object();set_string(row,"task",string_member(app,"id"));set_string(row,"version",app_versions[v]);set_integer(row,"trial",0);set_integer(row,"attempt",0);
            set_string(row,"input","Synthetic application fixture");set_string(row,"response",src);set_string(row,"source",src);json_object_array_add(rows,row);free(src);free(path);
        }
    }
    json_object_put(apps);
    json_object *repairs=read_json("evaluation/comparison_repairs.json");
    for(size_t i=0;i<json_object_array_length(repairs);i++) {
        json_object *repair=json_object_array_get_idx(repairs,i);
        for(size_t v=0;v<2;v++)for(int attempt=0;attempt<2;attempt++) {
            json_object *refs=read_json(format("evaluation/%s.json",versions[v]));
            const char *src=attempt?string_member(refs,"sum"):string_member(repair,versions[v]);
            json_object *row=json_object_new_object();set_string(row,"task",format("repair_%s",string_member(repair,"id")));set_string(row,"version",versions[v]);set_integer(row,"trial",0);set_integer(row,"attempt",attempt);
            set_string(row,"input","Synthetic repair regression fixture");set_string(row,"response",src);set_string(row,"source",src);set_bool(row,"synthetic_expected_correct",attempt);json_object_array_add(rows,row);json_object_put(refs);
        }
    }
    json_object_put(repairs);
    write_json(attempts_path,rows);
    char *export_argv[]={"build/lm0-eval","export",export_path,"--suite","python","--attempts",attempts_path,NULL};run(export_argv);
    char *report_argv[]={"build/lm0-eval","report",export_path,"--execute","-o",report_path,NULL};run(report_argv);
    json_object *report=read_json(report_path),*results=member(report,"attempts",json_type_array);
    for(size_t i=0;i<json_object_array_length(results);i++) {
        json_object *r=json_object_array_get_idx(results,i);
        json_object *wanted=NULL,*input=json_object_array_get_idx(rows,i);
        int expected=!json_object_object_get_ex(input,"synthetic_expected_correct",&wanted)||json_object_get_boolean(wanted);
        if(json_object_get_boolean(member(r,"correct",json_type_boolean))!=expected)die(json_object_to_json_string_ext(r,JSON_C_TO_STRING_PRETTY));
    }
    printf("Verified %zu reference and repair attempts (49 reference programs and 40 broken/corrected repairs).\n",json_object_array_length(results));
    json_object_put(rows);json_object_put(report);
    rows=json_object_new_array();
    json_object *pyrefs=read_json("evaluation/python.json"),*lmrefs=read_json("evaluation/v3.json");
    for(int language=0;language<2;language++)for(int trial=0;trial<2;trial++)for(int attempt=0;attempt<(language&&trial==0?2:1);attempt++) {
        json_object *row=json_object_new_object();set_string(row,"task","sum");set_string(row,"language",language?"lm0":"python");set_string(row,"language_version",language?"3":"test-runtime");
        set_string(row,"version",language?"v3":"python");set_integer(row,"trial",trial);set_integer(row,"attempt",attempt);
        const char *src=string_member(language?lmrefs:pyrefs,"sum");
        if(language&&attempt==0)src="module wrong version 3\nexport c fn solve(data:slice<i64>, key:i64) -> i64:\n    return 0\n";
        set_string(row,"source",src);set_string(row,"input","Do not count this convenience copy when requests exist");set_string(row,"response",src);
        json_object *model=json_object_new_object();set_string(model,"id","synthetic-regression");set_string(model,"version","1");set_string(model,"feedback_policy","same-case-feedback");json_object_object_add(model,"decoding",json_object_new_object());json_object_object_add(row,"model",model);
        json_object *requests=json_object_new_array();
        for(int q=0;q<2;q++){json_object *request=json_object_new_object();set_string(request,"input","Full synthetic input including repeated context and preceding tool output");set_string(request,"response",q?src:"synthetic tool request");json_object_array_add(requests,request);}
        json_object_object_add(row,"requests",requests);json_object_array_add(rows,row);
    }
    write_json(attempts_path,rows);export_path=format("%s/traces",root);export_argv[2]=export_path;run(export_argv);
    json_object *manifest=read_json(format("%s/manifest.json",export_path)),*artifacts=member(manifest,"artifacts",json_type_array);
    json_object *counts=json_object_new_object(),*tokenizer=json_object_new_object(),*measurements=json_object_new_array();
    set_string(tokenizer,"id","synthetic-regression-counts");set_string(tokenizer,"version","1");json_object_object_add(tokenizer,"settings",json_object_new_object());json_object_object_add(counts,"tokenizer",tokenizer);json_object_object_add(counts,"counts",measurements);
    for(size_t i=0;i<json_object_array_length(artifacts);i++) {
        json_object *a=json_object_array_get_idx(artifacts,i);const char *id=string_member(a,"id");
        if(!strstr(id,".request."))continue;
        json_object *c=json_object_new_object();set_string(c,"artifact",id);set_string(c,"sha256",string_member(a,"sha256"));set_integer(c,"tokens",strstr(id,".input")?10:5);json_object_array_add(measurements,c);
    }
    char *counts_path=format("%s/counts.json",root);write_json(counts_path,counts);
    char *traces[]={"build/lm0-eval","report",export_path,"--execute","--counts",counts_path,"-o",report_path,NULL};run(traces);report=read_json(report_path);
    json_object *summary=member(report,"workflow_by_language",json_type_object),*v3=member(summary,"v3",json_type_object);
    if(integer_member(v3,"successes")!=1||integer_member(v3,"reported_tokens")!=90||json_object_get_double(member(report,"lm0_python_ratio",json_type_double))!=3.0||json_object_get_boolean(member(report,"python_parity",json_type_boolean)))die("Trace accounting excluded failures, duplicated context, or claimed parity");
    json_object_put(report);
    json_object_array_del_idx(measurements,0,1);write_json(counts_path,counts);run(traces);report=read_json(report_path);
    json_object *ratio=NULL;json_object_object_get_ex(report,"lm0_python_ratio",&ratio);if(ratio)die("Incomplete traces produced a measured ratio");
    puts("Verified multi-request accounting, failed-trial cost, matched policies and incomplete coverage.");
    /* Artifacts are retained in /tmp for investigating a failed run. */
    return 0;
}
