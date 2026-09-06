#define _GNU_SOURCE
#include "eval_common.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *compiler, *driver, *directory;
static json_object *artifacts, *tasks, *cases;
static int comparison;
static const char *python_driver = "build/lm0-eval-python";
static void export_comparison(void);
static void report_comparison(json_object *, json_object *, json_object *, json_object *);
static void ordinary_cases(void);
static void export_requests(json_object *,const char *,const char *);
static int64_t request_tokens(json_object *,json_object *,const char *,const char *);
static int application_task(const char *);
static Process run_application(const char *,const char *,int,const char *);
static int64_t trial_number(json_object *row) {
    json_object *value;
    return json_object_object_get_ex(row, "trial", &value) ? integer_member(row, "trial") : 0;
}
static const char *group_base(json_object *row) {
    return comparison ? format("%s.%s.%lld", string_member(row, "task"), string_member(row, "version"), (long long)trial_number(row))
                      : format("%s.%s", string_member(row, "task"), string_member(row, "version"));
}

static Process native_call(const char *command, const char *source, ...) {
    char *argv[32] = {(char *)compiler, (char *)command, (char *)source};
    size_t n = source ? 3 : 2;
    va_list args;
    va_start(args, source);
    const char *arg;
    while ((arg = va_arg(args, const char *))) {
        if (n >= 31) die("Too many compiler arguments");
        argv[n++] = (char *)arg;
    }
    va_end(args);
    argv[n] = NULL;
    return process(argv, process_timeout);
}

static char *require_native(Process result) {
    if (result.code || result.timed_out || result.limited) die(format("Native command failed: %s%s", result.out, result.err));
    free(result.err);
    return result.out;
}

static json_object *artifact(const char *id, const char *pair, const char *variant, const char *kind, const char *text) {
    json_object *record = json_object_new_object();
    char hash[65];
    size_t length = strlen(text);
    if (length > file_limit || !lm0_sha256(text, length, hash)) die("Cannot hash artifact");
    const char *file = format("%04zu.txt", json_object_array_length(artifacts));
    set_string(record, "id", id);
    set_string(record, "pair", pair);
    set_string(record, "variant", variant);
    set_string(record, "kind", kind);
    set_string(record, "file", file);
    set_string(record, "sha256", hash);
    set_integer(record, "bytes", (int64_t)length);
    write_text(format("%s/%s", directory, file), text);
    json_object_array_add(artifacts, record);
    return record;
}

static char *artifact_path(json_object *record) {
    const char *file = string_member(record, "file");
    if (!*file || strchr(file, '/') || !strcmp(file, ".") || !strcmp(file, "..")) die("Invalid artifact filename");
    return format("%s/%s", directory, file);
}

static void paired(const char *pair, const char *version, const char *kind, const char *text) {
    artifact(format("%s.%s", pair, version), pair, version, kind, text);
}

static void export_corpus(json_object *corpus) {
    for (size_t i = 0; i < json_object_array_length(corpus); i++) {
        json_object *item = json_object_array_get_idx(corpus, i);
        const char *id = string_member(item, "id"), *fn = string_member(item, "function"), *block = string_member(item, "block");
        char *baseline = read_text(string_member(item, "path"));
        for (int version = 1; version <= 2; version++) {
            const char *v = version == 1 ? "v1" : "v2";
            char *path = format("%s/corpus-%s-%s.lm0", directory, id, v);
            if (version == 1) write_text(path, baseline);
            else free(require_native(native_call("migrate", string_member(item, "path"), "-o", path, NULL)));
            char *source = read_text(path);
            paired(format("%s.source", id), v, "source", source);
            free(source);
            char *full = require_native(native_call("inspect", path, "--function", fn, "--block", block, NULL));
            char *compact = require_native(native_call("inspect", path, "--function", fn, "--block", block, "--view", "compact", NULL));
            paired(format("%s.context.full", id), v, "context_full", full);
            paired(format("%s.context.compact", id), v, "context_compact", compact);
            paired(format("%s.context.workflow", id), v, "context_workflow", version == 1 ? full : compact);
            json_object *context = parse_json(compact);
            paired(format("%s.replacement", id), v, "replacement", string_member(context, "source"));
            json_object_put(context);
            free(full); free(compact); free(path);
        }
        free(baseline);
    }
}

static char *replace_once(const char *source, const char *old, const char *replacement) {
    const char *at = strstr(source, old);
    if (!at || !*old || strstr(at + strlen(old), old)) die("Repair fixture must match exactly once");
    return format("%.*s%s%s", (int)(at-source), source, replacement, at+strlen(old));
}

static void export_prompts(void) {
    json_object_object_foreach(tasks, task, description) {
        json_object *example = json_object_array_get_idx(member(cases, task, json_type_array), 0);
        for (int version = 1; version <= 2; version++) {
            char *prompt = format("LM0 version %d. %s\nexport c fn @solve(%%data:ptr<i64>, %%n:u64, %%key:i64) -> i64\n"
                "All data and results are i64. n is the element count. Arithmetic wraps. Preserve input unless mutation is requested. "
                "No main, imports, I/O, or global state. Return complete source.\nPublic example: %s\n",
                version, json_object_get_string(description), json_object_to_json_string_ext(example, JSON_C_TO_STRING_PLAIN));
            if(comparison && version==2) {
                free(prompt);
                prompt=format("LM0 version 2. %s\nexport c fn @solve(%%data:ptr<i64>, %%n:u64, %%key:i64) -> i64\nStandard library allowed. No main or I/O. Preserve input unless mutation is requested. All required intermediate arithmetic fits signed i64; popcount uses the 64-bit representation. Return complete source.\nPublic example: %s\n",json_object_get_string(description),json_object_to_json_string_ext(example,JSON_C_TO_STRING_PLAIN));
            }
            paired(format("task.%s.prompt", task), version == 1 ? "v1" : "v2", "generation_prompt", prompt);
            free(prompt);
        }
    }
    json_object *repairs = read_json("evaluation/repairs.json");
    char *baseline = read_text("evaluation/sum.v1.lm0");
    char *migrated = format("%s/sum.v2.lm0", directory);
    free(require_native(native_call("migrate", "evaluation/sum.v1.lm0", "-o", migrated, NULL)));
    char *v2 = read_text(migrated);
    for (size_t i = 0; i < json_object_array_length(repairs); i++) {
        json_object *repair = json_object_array_get_idx(repairs, i);
        const char *id = string_member(repair, "id"), *block = string_member(repair, "block");
        for (int version = 1; version <= 2; version++) {
            const char *v = version == 1 ? "v1" : "v2";
            json_object *change = member(repair, v, json_type_array);
            char *source = replace_once(version == 1 ? baseline : v2,
                json_object_get_string(json_object_array_get_idx(change, 0)), json_object_get_string(json_object_array_get_idx(change, 1)));
            char *path = format("%s/repair-%s-%s.lm0", directory, id, v);
            write_text(path, source);
            paired(format("repair.%s.source", id), v, "repair_source", source);
            Process context = native_call("inspect", path, "--function", "solve", "--block", block, "--view", "compact", NULL);
            if (context.timed_out || context.limited || (context.code != 0 && context.code != 2) || !*context.out) die("Repair inspection failed");
            json_object *parsed = parse_json(context.out);
            json_object *prompt = json_object_new_object();
            set_string(prompt, "task", json_object_get_string(member(tasks, "sum", json_type_string)));
            set_string(prompt, "source", source);
            json_object_object_add(prompt, "feedback", json_object_get(parsed));
            paired(format("repair.%s.full", id), v, "repair_full", json_object_to_json_string_ext(prompt, JSON_C_TO_STRING_PLAIN));
            json_object_object_del(prompt, "source");
            set_string(prompt, "function", "solve");
            set_string(prompt, "block", block);
            if (context.code) set_string(prompt, "source", source);
            set_string(prompt, "replacement_unit", context.code ? "module" : "block");
            paired(format("repair.%s.local", id), v, "repair_local", json_object_to_json_string_ext(prompt, JSON_C_TO_STRING_PLAIN));
            char *valid_context = require_native(native_call("inspect", version == 1 ? "evaluation/sum.v1.lm0" : migrated,
                "--function", "solve", "--block", block, "--view", "compact", NULL));
            json_object *valid = parse_json(valid_context);
            paired(format("repair.%s.replacement", id), v, "repair_replacement", string_member(valid, "source"));
            paired(format("repair.%s.corrected", id), v, "repair_corrected", version == 1 ? baseline : v2);
            free(valid_context); json_object_put(valid);
            json_object_put(prompt); json_object_put(parsed);
            process_free(&context); free(source); free(path);
        }
    }
    free(baseline); free(v2); free(migrated); json_object_put(repairs);
}

static const char *attempt_base(json_object *row) {
    const char *task = string_member(row, "task"), *version = string_member(row, "version");
    int64_t attempt = integer_member(row, "attempt");
    json_object *description;
    if (!json_object_object_get_ex(tasks, task, &description) && !(comparison && application_task(task))) {
        if (strncmp(task, "repair_", 7)) die("Unknown attempt task");
        json_object *repairs = read_json(comparison && (!strcmp(version,"python")||!strcmp(version,"v3")) ? "evaluation/comparison_repairs.json" : "evaluation/repairs.json");
        int found = 0;
        for (size_t i = 0; i < json_object_array_length(repairs); i++)
            found |= !strcmp(task+7, string_member(json_object_array_get_idx(repairs, i), "id"));
        json_object_put(repairs);
        if (!found) die("Unknown repair task");
    }
    if ((strcmp(version, "v1") && strcmp(version, "v2") && (!comparison || (strcmp(version, "v3") && strcmp(version, "python")))) || attempt < 0 || (uint64_t)attempt > max_repairs || trial_number(row) < 0) die("Invalid attempt version or number");
    return format("attempt.%s.%lld", group_base(row), (long long)attempt);
}

static void validate_attempts(json_object *rows) {
    if (!json_object_is_type(rows, json_type_array)) die("Attempts must be a JSON array");
    json_object *seen = json_object_new_object();
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i), *previous;
        const char *base = attempt_base(row);
        if (json_object_object_get_ex(seen, base, &previous)) die("Duplicate attempt");
        set_bool(seen, base, 1);
        string_member(row, "input"); string_member(row, "response"); string_member(row, "source");
        json_object *model;
        if (comparison && json_object_object_get_ex(row,"model",&model)) {
            if (!*string_member(model,"id") || !*string_member(model,"version") || !*string_member(model,"feedback_policy")) die("Model identity, version and feedback policy are required");
            member(model,"decoding",json_type_object);
            string_member(row,"language_version");
            const char *language=string_member(row,"language");
            if (strcmp(language,!strcmp(string_member(row,"version"),"python")?"python":"lm0")) die("Language and variant disagree");
            member(row,"requests",json_type_array);
        }
    }
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i), *previous;
        for (int64_t a = 0; a < integer_member(row, "attempt"); a++)
            if (!json_object_object_get_ex(seen, format("attempt.%s.%lld", group_base(row), (long long)a), &previous))
                die("Attempt sequence must start at zero without gaps");
    }
    json_object_put(seen);
}

static json_object *export_attempts(const char *path) {
    json_object *rows = path ? read_json(path) : json_object_new_array();
    validate_attempts(rows);
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        const char *base = attempt_base(row), *version = string_member(row, "version");
        const char *fields[] = {"input", "response", "source"};
        for (size_t f = 0; f < 3; f++)
            artifact(format("%s.%s", base, fields[f]), base, version, format("attempt_%s", fields[f]), string_member(row, fields[f]));
        if (comparison) {
            artifact(format("%s.record", base), base, version, "attempt_record", json_object_to_json_string_ext(row, JSON_C_TO_STRING_PLAIN));
            export_requests(row,base,version);
        }
    }
    return rows;
}

static void export_all(json_object *settings, const char *attempt_path) {
    if (mkdir(directory, 0755)) die("Export requires a new directory under an existing parent");
    artifacts = json_object_new_array();
    json_object *manifest = json_object_new_object();
    set_integer(manifest, "schema", comparison ? 2 : 1);
    set_string(manifest, "compiler", require_native(native_call("--version", NULL, NULL)));
    export_corpus(read_json(string_member(settings, "corpus")));
    paired("guidance", "v1", "guidance", read_text("docs/llm-v1.md"));
    paired("guidance", "v2", "guidance", read_text("docs/llm-v2.md"));
    export_prompts();
    if (comparison) export_comparison();
    json_object_object_add(manifest, "artifacts", artifacts);
    json_object_object_add(manifest, "attempts", export_attempts(attempt_path));
    json_object_object_add(manifest, "tasks", json_object_get(tasks));
    write_json(format("%s/evaluation-cases.json", directory), cases);
    char hash[65];
    char *case_text = read_text(format("%s/evaluation-cases.json", directory));
    if (!lm0_sha256(case_text, strlen(case_text), hash)) die("Cannot hash cases");
    set_string(manifest, "cases_sha256", hash);
    free(case_text);
    write_json(format("%s/manifest.json", directory), manifest);
    printf("{\"ok\":true,\"artifacts\":%zu}\n", json_object_array_length(artifacts));
    json_object_put(manifest);
}

static json_object *index_artifacts(void) {
    json_object *index = json_object_new_object();
    for (size_t i = 0; i < json_object_array_length(artifacts); i++) {
        json_object *record = json_object_array_get_idx(artifacts, i), *previous;
        const char *id = string_member(record, "id");
        if (json_object_object_get_ex(index, id, &previous)) die("Duplicate artifact id");
        char *text = read_text(artifact_path(record)), hash[65];
        if (!lm0_sha256(text, strlen(text), hash) || strcmp(hash, string_member(record, "sha256")) ||
            (int64_t)strlen(text) != integer_member(record, "bytes")) die("Artifact content does not match its manifest");
        free(text);
        json_object_object_add(index, id, json_object_get(record));
    }
    return index;
}

static json_object *import_counts(const char *path, json_object *index, json_object *report) {
    json_object *map = json_object_new_object();
    if (!path) { json_object_object_add(report, "tokenizer", NULL); return map; }
    json_object *input = read_json(path), *tokenizer = member(input, "tokenizer", json_type_object);
    if (!*string_member(tokenizer, "id") || !*string_member(tokenizer, "version")) die("Tokenizer identity and version are required");
    member(tokenizer, "settings", json_type_object);
    json_object_object_add(report, "tokenizer", json_object_get(tokenizer));
    json_object *counts = member(input, "counts", json_type_array);
    for (size_t i = 0; i < json_object_array_length(counts); i++) {
        json_object *row = json_object_array_get_idx(counts, i), *record, *previous;
        const char *id = string_member(row, "artifact");
        if (!json_object_object_get_ex(index, id, &record) || json_object_object_get_ex(map, id, &previous)) die("Unknown or duplicate count artifact");
        if (strcmp(string_member(record, "sha256"), string_member(row, "sha256"))) die("Token count hash mismatch");
        int64_t tokens = integer_member(row, "tokens");
        if (tokens < 0) die("Token counts must be nonnegative integers");
        json_object_object_add(map, id, json_object_new_int64(tokens));
    }
    json_object_put(input);
    return map;
}

static int64_t measured(json_object *counts, const char *id) {
    json_object *value;
    return json_object_object_get_ex(counts, id, &value) ? json_object_get_int64(value) : -1;
}

static void nullable_count(json_object *object, const char *key, int64_t value) {
    if (value < 0) json_object_object_add(object, key, NULL);
    else set_integer(object, key, value);
}

static json_object *compare_pairs(json_object *index, json_object *counts) {
    json_object *pairs = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(artifacts); i++) {
        json_object *v1 = json_object_array_get_idx(artifacts, i), *v2;
        if (strcmp(string_member(v1, "variant"), "v1") || !strncmp(string_member(v1, "kind"), "attempt_", 8)) continue;
        const char *pair = string_member(v1, "pair");
        if (!json_object_object_get_ex(index, format("%s.v2", pair), &v2)) die("Unpaired baseline artifact");
        json_object *row = json_object_new_object();
        set_string(row, "pair", pair);
        set_string(row, "kind", string_member(v1, "kind"));
        int64_t before = integer_member(v1, "bytes"), after = integer_member(v2, "bytes");
        set_integer(row, "v1_bytes", before); set_integer(row, "v2_bytes", after);
        set_integer(row, "bytes_saved", before-after);
        int64_t t1 = measured(counts, string_member(v1, "id")), t2 = measured(counts, string_member(v2, "id"));
        nullable_count(row, "v1_tokens", t1); nullable_count(row, "v2_tokens", t2);
        if (t1 < 0 || t2 < 0) json_object_object_add(row, "tokens_saved", NULL);
        else set_integer(row, "tokens_saved", t1-t2);
        json_object_array_add(pairs, row);
    }
    return pairs;
}

static int valid_interface(json_object *module) {
    json_object *functions = member(module, "functions", json_type_array);
    if (json_object_array_length(member(module, "data", json_type_array))) return 0;
    int found = 0;
    const char *types[] = {"ptr<i64>", "u64", "i64"};
    for (size_t i = 0; i < json_object_array_length(functions); i++) {
        json_object *fn = json_object_array_get_idx(functions, i);
        const char *name = string_member(fn, "name");
        if ((json_object_get_boolean(member(fn, "external", json_type_boolean)) && !(comparison && !strncmp(name,"std_",4))) || !strcmp(name, "main")) return 0;
        if (strcmp(name, "solve")) continue;
        json_object *params = member(fn, "params", json_type_array);
        if (integer_member(module, "version") == 3 && json_object_array_length(params) == 2 &&
            !strcmp(string_member(fn,"returns"),"i64") && json_object_get_boolean(member(fn,"exported",json_type_boolean)) &&
            !strcmp(string_member(json_object_array_get_idx(params,0),"type"),"slice<i64>") &&
            !strcmp(string_member(json_object_array_get_idx(params,1),"type"),"i64")) {found++;continue;}
        if (strcmp(string_member(fn, "returns"), "i64") || json_object_array_length(params) != 3 ||
            !json_object_get_boolean(member(fn, "exported", json_type_boolean))) return 0;
        for (size_t p = 0; p < 3; p++) if (strcmp(string_member(json_object_array_get_idx(params, p), "type"), types[p])) return 0;
        found++;
    }
    return found == 1;
}

static json_object *grade_attempts(json_object *rows, json_object *index, json_object *counts, int execute, json_object *report) {
    validate_attempts(rows);
    json_object *results = json_object_new_array(), *groups = json_object_new_object();
    char temp[] = "/tmp/lm0-eval-XXXXXX";
    if (!mkdtemp(temp)) die("Cannot create evaluation directory");
    char *library = format("%s/candidate.so", temp);
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        const char *base = attempt_base(row), *task = string_member(row, "task"), *version = string_member(row, "version");
        const char *fields[] = {"input", "response", "source"};
        json_object *source_record = NULL;
        for (size_t f = 0; f < 3; f++) {
            json_object *record = member(index, format("%s.%s", base, fields[f]), json_type_object);
            char *text = read_text(artifact_path(record));
            if (strcmp(text, string_member(row, fields[f]))) die("Attempt differs from its hashed artifact");
            free(text);
            if (f == 2) source_record = record;
        }
        char *source = artifact_path(source_record);
        int is_python = !strcmp(version, "python");
        int is_application = comparison && application_task(task);
        char *pycheck[] = {(char *)python_driver, "check", source, "-", "-", NULL};
        Process checked = is_python ? process(pycheck, process_timeout) : native_call("check", source, NULL);
        if (checked.timed_out || checked.limited || (checked.code != 0 && checked.code != 2)) die(format("Compiler failed while grading %s (%d): %s%s",base,checked.code,checked.out,checked.err));
        json_object *validation = parse_json(checked.out), *result = json_object_new_object();
        set_string(result, "task", task); set_string(result, "version", version);
        if (comparison) {
            set_integer(result, "trial", trial_number(row));
            json_object *record = member(index, format("%s.record", base), json_type_object);
            char *saved = read_text(artifact_path(record));
            if (strcmp(saved, json_object_to_json_string_ext(row, JSON_C_TO_STRING_PLAIN))) die("Attempt metadata changed");
            free(saved);
            json_object *requests;
            if(json_object_object_get_ex(row,"requests",&requests))for(size_t q=0;q<json_object_array_length(requests);q++) {
                json_object *request=json_object_array_get_idx(requests,q);
                const char *parts[]={"input","response"};
                for(size_t p=0;p<2;p++) {
                    json_object *part=member(index,format("%s.request.%zu.%s",base,q,parts[p]),json_type_object);
                    char *text=read_text(artifact_path(part));
                    if(strcmp(text,string_member(request,parts[p])))die("Request differs from its hashed artifact");
                    free(text);
                }
            }
            int64_t provider_in=0,provider_out=0;int provider_complete=1;
            if(!json_object_object_get_ex(row,"requests",&requests))provider_complete=0;
            else {
                set_integer(result,"request_count",(int64_t)json_object_array_length(requests));
                for(size_t q=0;q<json_object_array_length(requests);q++) {
                    json_object *request=json_object_array_get_idx(requests,q),*usage;
                    if(!json_object_object_get_ex(request,"provider_usage",&usage)){provider_complete=0;continue;}
                    int64_t a=integer_member(usage,"input_tokens"),b=integer_member(usage,"output_tokens");
                    if(a<0||b<0||provider_in>INT64_MAX-a||provider_out>INT64_MAX-b)die("Invalid provider usage total");
                    provider_in+=a;provider_out+=b;
                }
            }
            nullable_count(result,"provider_input_tokens",provider_complete?provider_in:-1);
            nullable_count(result,"provider_output_tokens",provider_complete?provider_out:-1);
        }
        int64_t number = integer_member(row, "attempt");
        set_integer(result, "attempt", number);
        set_bool(result, "verified", checked.code == 0);
        json_object_object_add(result, "validation", validation);
        int correct = -1, eligible = 0;
        if (!checked.code && !is_python) {
            char *inspection = require_native(native_call("inspect", source, "--module", NULL));
            json_object *module = parse_json(inspection);
            int64_t language_version = integer_member(module, "version");
            eligible = (is_application || valid_interface(module)) && language_version == (!strcmp(version, "v1") ? 1 : !strcmp(version, "v3") ? 3 : 2);
            free(inspection); json_object_put(module);
        }
        if (is_python) eligible = !checked.code;
        set_bool(result, "task_interface", eligible);
        if (execute) {
            correct = 0;
            if (eligible) {
                Process built = is_python ? (Process){.out=strdup(""), .err=strdup("")} : is_application ? native_call("build",source,"-o",library,NULL) : native_call("build", source, "--kind", "shared", "-o", library, NULL);
                if (!built.code && !built.timed_out && !built.limited) {
                    char *argv[] = {(char *)driver, library, format("%s/evaluation-cases.json", directory), (char *)(!strncmp(task, "repair_", 7) ? "sum" : task), NULL};
                    char *pyargv[] = {(char *)python_driver, "execute", source, argv[2], argv[3], NULL};
                    Process run = is_application ? run_application(source,library,is_python,task) : process(is_python ? pyargv : argv, run_timeout);
                    correct = run.code == 0 && !run.timed_out && !run.limited;
                    if (correct) {
                        json_object *observation = try_parse_json(run.out),*verdict=NULL,*observed_cases=NULL;
                        correct = observation && json_object_object_get_ex(observation,"correct",&verdict) &&
                            json_object_is_type(verdict,json_type_boolean) && json_object_get_boolean(verdict) &&
                            json_object_object_get_ex(observation,"cases",&observed_cases) && json_object_is_type(observed_cases,json_type_int) &&
                            json_object_get_int64(observed_cases) == (int64_t)json_object_array_length(member(cases, argv[3], json_type_array));
                        json_object_put(observation);
                    }
                    set_string(result, "execution_stdout", run.out);
                    set_string(result, "execution_stderr", run.err);
                    set_integer(result, "exit_code", run.code);
                    set_bool(result, "timed_out", run.timed_out);
                    set_bool(result, "output_limited", run.limited);
                    process_free(&run); free(argv[2]);
                } else set_string(result, "build_output", built.out);
                process_free(&built);
                unlink(library);
            }
        }
        if (correct < 0) json_object_object_add(result, "correct", NULL);
        else set_bool(result, "correct", correct);
        int64_t in = request_tokens(row,counts,base,"input"), out = request_tokens(row,counts,base,"response");
        nullable_count(result, "input_tokens", in); nullable_count(result, "output_tokens", out);
        json_object_array_add(results, result);
        const char *group_id = group_base(row);
        json_object *group;
        if (!json_object_object_get_ex(groups, group_id, &group)) {
            group = json_object_new_object();
            set_string(group, "task", task); set_string(group, "version", version);
            if (comparison) set_integer(group, "trial", trial_number(row));
            json_object_object_add(group, "attempts_to_success", NULL);
            json_object_object_add(groups, group_id, group);
        }
        json_object *success = NULL;
        json_object_object_get_ex(group, "attempts_to_success", &success);
        if (correct == 1 && (!success || number+1 < json_object_get_int64(success))) set_integer(group, "attempts_to_success", number+1);
        process_free(&checked); free(source);
    }
    json_object_object_foreach(groups, id, group) {
        (void)id;
        int64_t total_in = 0, total_out = 0, covered = 0, attempts = 0;
        json_object *success = NULL;
        json_object_object_get_ex(group, "attempts_to_success", &success);
        for (size_t i = 0; i < json_object_array_length(results); i++) {
            json_object *row = json_object_array_get_idx(results, i);
            if (strcmp(string_member(row, "task"), string_member(group, "task")) || strcmp(string_member(row, "version"), string_member(group, "version"))) continue;
            if (comparison && trial_number(row) != trial_number(group)) continue;
            if (success && integer_member(row, "attempt") >= json_object_get_int64(success)) continue;
            attempts++;
            json_object *in = NULL, *out = NULL;
            json_object_object_get_ex(row, "input_tokens", &in); json_object_object_get_ex(row, "output_tokens", &out);
            if (in && out) covered++;
            if (in) { int64_t n = json_object_get_int64(in); if (total_in > INT64_MAX-n) die("Token total overflow"); total_in += n; }
            if (out) { int64_t n = json_object_get_int64(out); if (total_out > INT64_MAX-n) die("Token total overflow"); total_out += n; }
        }
        set_integer(group, "attempts_counted", attempts); set_integer(group, "measured_attempts", covered);
        set_integer(group, "reported_input_tokens", total_in); set_integer(group, "reported_output_tokens", total_out);
        nullable_count(group, "total_input_tokens", covered == attempts ? total_in : -1);
        nullable_count(group, "total_output_tokens", covered == attempts ? total_out : -1);
    }
    json_object_object_add(report, "tasks", groups);
    rmdir(temp); free(library);
    return results;
}

static void report_all(const char *counts_path, const char *output, int execute) {
    json_object *manifest = read_json(format("%s/manifest.json", directory));
    int64_t schema = integer_member(manifest, "schema");
    if (schema != 1 && schema != 2) die("Unsupported manifest schema");
    comparison = schema == 2;
    artifacts = member(manifest, "artifacts", json_type_array);
    char *case_text = read_text(format("%s/evaluation-cases.json", directory)), hash[65];
    if (!lm0_sha256(case_text, strlen(case_text), hash) || strcmp(hash, string_member(manifest, "cases_sha256"))) die("Evaluation cases changed");
    cases = parse_json(case_text);
    free(case_text);
    json_object *index = index_artifacts(), *report = json_object_new_object();
    set_bool(report, "ok", 1);
    set_string(report, "token_measurements", "imported; identity and content hashes checked, counts not independently recomputed");
    set_bool(report, "executed", execute);
    json_object *counts = import_counts(counts_path, index, report);
    json_object *pairs = compare_pairs(index, counts), *sizes = json_object_new_object();
    json_object_object_add(report, "pairs", pairs);
    for (size_t i = 0; i < json_object_array_length(pairs); i++) {
        json_object *pair = json_object_array_get_idx(pairs, i), *group;
        const char *kind = string_member(pair, "kind");
        if (!json_object_object_get_ex(sizes, kind, &group)) {
            group = json_object_new_object();
            set_integer(group, "pairs", 0); set_integer(group, "v1_bytes", 0); set_integer(group, "v2_bytes", 0);
            json_object_object_add(sizes, kind, group);
        }
        set_integer(group, "pairs", integer_member(group, "pairs")+1);
        const char *fields[] = {"v1_bytes", "v2_bytes"};
        for (size_t f = 0; f < 2; f++) {
            int64_t before = integer_member(group, fields[f]), value = integer_member(pair, fields[f]);
            if (value < 0 || before > INT64_MAX-value) die("Artifact byte total overflow");
            set_integer(group, fields[f], before+value);
        }
    }
    json_object_object_add(report, "byte_totals_by_kind", sizes);
    json_object_object_add(report, "attempts", grade_attempts(member(manifest, "attempts", json_type_array), index, counts, execute, report));
    if (comparison) report_comparison(manifest, index, counts, report);
    if (output) write_json(output, report);
    puts(json_object_to_json_string_ext(report, JSON_C_TO_STRING_PLAIN));
    json_object_put(counts); json_object_put(index); json_object_put(report); json_object_put(manifest);
    json_object_put(cases);
}

int main(int argc, char **argv) {
    if (argc < 3) die("Usage: lm0-eval export DIR [--attempts FILE] | report DIR [--counts FILE] [--execute] [-o FILE]; --config FILE --compiler FILE --driver FILE");
    directory = argv[2];
    const char *config = "evaluation/settings.json", *attempts = NULL, *counts = NULL, *output = NULL;
    int execute = 0;
    for (int i = 3; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--execute")) { execute = 1; continue; }
        if (++i >= argc) die("Missing option value");
        if (!strcmp(arg, "--config")) config = argv[i];
        else if (!strcmp(arg, "--compiler")) compiler = argv[i];
        else if (!strcmp(arg, "--driver")) driver = argv[i];
        else if (!strcmp(arg, "--attempts")) attempts = argv[i];
        else if (!strcmp(arg, "--counts")) counts = argv[i];
        else if (!strcmp(arg, "--python-driver")) python_driver = argv[i];
        else if (!strcmp(arg, "--suite") && !strcmp(argv[i], "python")) comparison = 1;
        else if (!strcmp(arg, "-o")) output = argv[i];
        else die("Unknown option");
    }
    json_object *settings = read_json(config);
    int64_t files = integer_member(settings, "file_bytes"), outputs = integer_member(settings, "output_bytes");
    int64_t deadline = integer_member(settings, "process_timeout_seconds"), run = integer_member(settings, "run_timeout_seconds"), repairs = integer_member(settings, "max_repairs");
    if (files <= 0 || files > INT_MAX || outputs <= 0 || outputs > INT_MAX || deadline <= 0 || deadline > UINT_MAX || run <= 0 || run > UINT_MAX || repairs < 0 || repairs > 1000) die("Invalid evaluation limits");
    file_limit = (size_t)files; output_limit = (size_t)outputs; process_timeout = (unsigned)deadline; run_timeout = (unsigned)run; max_repairs = (unsigned)repairs;
    if (!compiler) compiler = string_member(settings, "compiler");
    if (!driver) driver = string_member(settings, "driver");
    tasks = read_json(string_member(settings, "tasks"));
    if (!strcmp(argv[1], "export")) {
        if (counts || output || execute) die("Invalid export option");
        cases = evaluation_cases(tasks);
        if (comparison) ordinary_cases();
        export_all(settings, attempts);
        json_object_put(cases);
    } else if (!strcmp(argv[1], "report")) {
        if (attempts) die("Supply attempts when exporting their exact texts");
        report_all(counts, output, execute);
    } else die("Unknown evaluation command");
    json_object_put(tasks); json_object_put(settings);
    return 0;
}

#include "eval_comparison.c"
#include "eval_applications.c"
