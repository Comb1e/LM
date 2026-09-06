/* Included by eval.c to share its artifact, count, and process interfaces. */
static void export_requests(json_object *row,const char *base,const char *version) {
    json_object *requests;
    if(!json_object_object_get_ex(row,"requests",&requests))return;
    if(!json_object_is_type(requests,json_type_array)||!json_object_array_length(requests))die("Requests must be a nonempty ordered array");
    for(size_t i=0;i<json_object_array_length(requests);i++) {
        json_object *r=json_object_array_get_idx(requests,i);
        const char *fields[]={"input","response"};
        for(size_t j=0;j<2;j++)artifact(format("%s.request.%zu.%s",base,i,fields[j]),base,version,"request_text",string_member(r,fields[j]));
        json_object *usage;
        if(json_object_object_get_ex(r,"provider_usage",&usage)) {
            if(integer_member(usage,"input_tokens")<0||integer_member(usage,"output_tokens")<0)die("Negative provider usage");
        }
        json_object *tools;
        if(json_object_object_get_ex(r,"tools",&tools)&&!json_object_is_type(tools,json_type_array))die("Tools must be an ordered array");
    }
}
static int64_t request_tokens(json_object *row,json_object *counts,const char *base,const char *field) {
    json_object *requests;int64_t total=0;
    if(!comparison||!json_object_object_get_ex(row,"requests",&requests))return measured(counts,format("%s.%s",base,field));
    if(!json_object_is_type(requests,json_type_array)||!json_object_array_length(requests))die("Invalid requests");
    for(size_t i=0;i<json_object_array_length(requests);i++) {
        int64_t n=measured(counts,format("%s.request.%zu.%s",base,i,field));
        if(n<0)return -1;
        if(total>INT64_MAX-n)die("Request token total overflow");
        total+=n;
    }
    return total;
}
static void ordinary_cases(void) {
    json_object_object_foreach(cases, task, rows) {
        for (size_t i = json_object_array_length(rows); i-- > 0;) {
            json_object *row = json_object_array_get_idx(rows, i);
            json_object *data = member(row, "data", json_type_array);
            int remove = (!strcmp(task, "factorial") && integer_member(row, "key") > 20) ||
                         (!strcmp(task, "fibonacci") && integer_member(row, "key") > 90);
            for (size_t j = 0; j < json_object_array_length(data); j++) {
                int64_t x = json_object_get_int64(json_object_array_get_idx(data, j));
                remove |= x < -1000000 || x > 1000000;
            }
            if (remove) json_object_array_del_idx(rows, i, 1);
        }
    }
}

static void export_comparison(void) {
    paired("guidance","v3","guidance",read_text("docs/llm.md"));
    paired("guidance","python","guidance",read_text("docs/python-evaluation.md"));
    json_object *refs = read_json("evaluation/python.json");
    json_object_object_foreach(refs, task, source) {
        paired(format("reference.%s", task), "python", "reference_source", json_object_get_string(source));
        paired(format("task.%s.prompt", task), "python", "generation_prompt",
            format("%s\nImplement solve(data, key). data is a list of signed integers. Return an integer and preserve the list unless mutation is requested. Standard library allowed. All required intermediate arithmetic fits signed i64; popcount uses the 64-bit representation. Return complete source.\nPublic example: %s\n",
                json_object_get_string(member(tasks, task, json_type_string)),
                json_object_to_json_string_ext(json_object_array_get_idx(member(cases, task, json_type_array), 0), JSON_C_TO_STRING_PLAIN)));
    }
    json_object_put(refs);
    refs=read_json("evaluation/v3.json");
    json_object_object_foreach(refs, task3, source3) {
        paired(format("reference.%s",task3),"v3","reference_source",json_object_get_string(source3));
        json_object *source_artifact=json_object_array_get_idx(artifacts,json_object_array_length(artifacts)-1);
        char *context=require_native(native_call("inspect",artifact_path(source_artifact),"--function","solve","--view","compact",NULL));
        paired(format("reference.%s.context",task3),"v3","context_compact",context);
        json_object *parsed=parse_json(context);
        paired(format("reference.%s.replacement",task3),"v3","replacement",string_member(parsed,"source"));
        json_object_put(parsed);free(context);
        paired(format("task.%s.prompt",task3),"v3","generation_prompt",format("LM0 version 3. %s\nexport c fn solve(data:slice<i64>, key:i64) -> i64\nStandard library allowed. Preserve input unless mutation is requested. All required intermediate arithmetic fits signed i64; popcount uses the 64-bit representation. Return complete source.\nPublic example: %s\n",
            json_object_get_string(member(tasks,task3,json_type_string)),json_object_to_json_string_ext(json_object_array_get_idx(member(cases,task3,json_type_array),0),JSON_C_TO_STRING_PLAIN)));
    }
    json_object_put(refs);
    json_object *apps=read_json("evaluation/applications.json");
    for(size_t i=0;i<json_object_array_length(apps);i++) {
        json_object *app=json_object_array_get_idx(apps,i);const char *id=string_member(app,"id"),*name=string_member(app,"name");
        json_object_object_add(cases,id,json_object_get(member(app,"cases",json_type_array)));
        const char *versions[]={"v2","v3","python"};
        for(size_t v=0;v<3;v++) {
            char *path=v==0?format("examples/stdlib/%s.lm0",name):v==1?format("examples/v3/%s.lm0",name):format("evaluation/python/%s.py",name);
            char *src=read_text(path);paired(format("reference.%s",id),versions[v],"application_source",src);free(src);free(path);
            if(v<2) {
                json_object *source_artifact=json_object_array_get_idx(artifacts,json_object_array_length(artifacts)-1);
                char *context=require_native(native_call("inspect",artifact_path(source_artifact),"--function","main","--view","compact",NULL));
                paired(format("reference.%s.context",id),versions[v],"context_compact",context);
                json_object *parsed=parse_json(context);
                paired(format("reference.%s.replacement",id),versions[v],"replacement",string_member(parsed,"source"));
                json_object_put(parsed);free(context);
            }
            paired(format("task.%s.prompt",id),versions[v],"application_prompt",format("%s\nLanguage: %s. Return a complete program.\n",string_member(app,"task"),versions[v]));
        }
    }
    json_object_put(apps);
    json_object *repairs=read_json("evaluation/comparison_repairs.json");
    const char *repair_versions[]={"python","v3"};
    for(size_t i=0;i<json_object_array_length(repairs);i++) {
        json_object *repair=json_object_array_get_idx(repairs,i);const char *id=string_member(repair,"id");
        for(size_t v=0;v<2;v++) {
            const char *version=repair_versions[v],*src=string_member(repair,version);
            paired(format("comparison.repair.%s.source",id),version,"repair_source",src);
            paired(format("comparison.repair.%s.prompt",id),version,"repair_prompt",format("Repair solve to return the sum without mutating data. %s. Return the complete corrected function.\n%s",string_member(repair,"task"),src));
            json_object *correct=read_json(v?"evaluation/v3.json":"evaluation/python.json");
            paired(format("comparison.repair.%s.corrected",id),version,"repair_corrected",string_member(correct,"sum"));json_object_put(correct);
        }
    }
    json_object_put(repairs);
}

static void report_comparison(json_object *manifest, json_object *index, json_object *counts,json_object *report) {
    json_object *measurements=json_object_new_array(),*comparisons=json_object_new_array();
    for(size_t i=0;i<json_object_array_length(artifacts);i++) {
        json_object *a=json_object_array_get_idx(artifacts,i);
        const char *kind=string_member(a,"kind"),*id=string_member(a,"id"),*variant=string_member(a,"variant");
        if(!strncmp(kind,"attempt_",8)||!strcmp(kind,"request_text"))continue;
        json_object *m=json_object_new_object();set_string(m,"artifact",id);set_string(m,"kind",kind);set_integer(m,"bytes",integer_member(a,"bytes"));nullable_count(m,"tokens",measured(counts,id));json_object_array_add(measurements,m);
        if(strcmp(variant,"v3"))continue;
        const char *baselines[]={"python","v2"};
        for(size_t b=0;b<2;b++) {
            json_object *base;
            if(!json_object_object_get_ex(index,format("%s.%s",string_member(a,"pair"),baselines[b]),&base))continue;
            json_object *c=json_object_new_object();set_string(c,"pair",string_member(a,"pair"));set_string(c,"baseline",baselines[b]);
            int64_t before=measured(counts,string_member(base,"id")),after=measured(counts,id);
            set_integer(c,"baseline_bytes",integer_member(base,"bytes"));set_integer(c,"v3_bytes",integer_member(a,"bytes"));
            nullable_count(c,"baseline_tokens",before);nullable_count(c,"v3_tokens",after);
            json_object_object_add(c,"ratio",before>0&&after>=0?json_object_new_double((double)after/before):NULL);
            json_object_array_add(comparisons,c);
        }
    }
    json_object_object_add(report,"artifact_measurements",measurements);json_object_object_add(report,"comparisons",comparisons);
    json_object *summary = json_object_new_object();
    json_object *groups = member(report, "tasks", json_type_object);
    json_object *rows = member(manifest, "attempts", json_type_array);
    json_object *configurations=json_object_new_object();
    int comparable=json_object_array_length(rows)>0;
    const char *model_identity=NULL;
    for(size_t i=0;i<json_object_array_length(rows);i++) {
        json_object *row=json_object_array_get_idx(rows,i),*model;
        const char *key=group_base(row);
        const char *identity="unrecorded";
        if(!json_object_object_get_ex(row,"model",&model))comparable=0;
        else {identity=json_object_to_json_string_ext(model,JSON_C_TO_STRING_PLAIN);if(!model_identity)model_identity=identity;else if(strcmp(model_identity,identity))comparable=0;}
        json_object *previous;
        if(json_object_object_get_ex(configurations,key,&previous)&&strcmp(json_object_get_string(previous),identity))die("Model configuration changed within a trial");
        set_string(configurations,key,identity);
    }
    json_object_object_foreach(groups, id, group) {
        (void)id;
        const char *version = string_member(group, "version");
        json_object *sum;
        if (!json_object_object_get_ex(summary, version, &sum)) {
            sum = json_object_new_object();
            set_integer(sum, "trials", 0); set_integer(sum, "successes", 0);
            set_integer(sum, "measured_trials", 0); set_integer(sum, "reported_tokens", 0);
            json_object_object_add(summary, version, sum);
        }
        set_integer(sum, "trials", integer_member(sum, "trials") + 1);
        json_object *success = NULL, *in = NULL, *out = NULL;
        json_object_object_get_ex(group, "attempts_to_success", &success);
        json_object_object_get_ex(group, "total_input_tokens", &in);
        json_object_object_get_ex(group, "total_output_tokens", &out);
        if (success) set_integer(sum, "successes", integer_member(sum, "successes") + 1);
        if (in && out) {
            int64_t a = json_object_get_int64(in), b = json_object_get_int64(out), total = integer_member(sum, "reported_tokens");
            if (a > INT64_MAX-b || total > INT64_MAX-a-b) die("Token total overflow");
            set_integer(sum, "reported_tokens", total+a+b);
            set_integer(sum, "measured_trials", integer_member(sum, "measured_trials")+1);
        }
    }
    json_object_object_foreach(summary, version, sum) {
        (void)version;
        int64_t n = integer_member(sum, "trials"), solved = integer_member(sum, "successes");
        int complete = n == integer_member(sum, "measured_trials");
        int executed = json_object_get_boolean(member(report, "executed", json_type_boolean));
        json_object_object_add(sum, "success_rate", executed ? json_object_new_double((double)solved/n) : NULL);
        json_object_object_add(sum, "tokens_per_success", executed && solved && complete ? json_object_new_double((double)integer_member(sum, "reported_tokens")/solved) : NULL);
    }
    /* Parity requires identical task/trial coverage across languages, with
     * complete counts and matching recorded model policies. */
    json_object *python=NULL,*v3=NULL;
    json_object_object_get_ex(summary,"python",&python);json_object_object_get_ex(summary,"v3",&v3);
    if(!python||!v3)comparable=0;
    json_object_object_foreach(groups,gid,g) {
        (void)gid;const char *v=string_member(g,"version");if(strcmp(v,"python")&&strcmp(v,"v3"))continue;
        json_object *mate;
        if(!json_object_object_get_ex(groups,format("%s.%s.%lld",string_member(g,"task"),!strcmp(v,"python")?"v3":"python",(long long)trial_number(g)),&mate))comparable=0;
    }
    set_string(report, "suite", "ordinary-i64-python");
    set_string(report, "numeric_conformance", "Use the default V1/V2 suite for wrapping and overflow; ordinary references do not establish numeric equivalence outside the declared domain.");
    set_integer(report, "recorded_attempts", (int64_t)json_object_array_length(rows));
    json_object_object_add(report, "workflow_by_language", summary);
    set_bool(report,"matching_trial_configuration",comparable);
    json_object *pt=NULL,*vt=NULL,*ps=NULL,*vs=NULL;
    if(python){json_object_object_get_ex(python,"tokens_per_success",&pt);json_object_object_get_ex(python,"success_rate",&ps);}
    if(v3){json_object_object_get_ex(v3,"tokens_per_success",&vt);json_object_object_get_ex(v3,"success_rate",&vs);}
    int measured_ratio=comparable&&pt&&vt&&ps&&vs&&json_object_get_double(pt)>0;
    double ratio=measured_ratio?json_object_get_double(vt)/json_object_get_double(pt):0;
    json_object_object_add(report,"lm0_python_ratio",measured_ratio?json_object_new_double(ratio):NULL);
    json_object_object_add(report,"python_parity",measured_ratio?json_object_new_boolean(ratio<=1&&json_object_get_double(vs)>=json_object_get_double(ps)):NULL);
    json_object_put(configurations);
}
