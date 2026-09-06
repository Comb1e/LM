/* Application fixtures execute in independent directories. No input in the
 * checkout is edited, and every observed file is compared with its oracle. */
static int application_task(const char *task) {
    return !strcmp(task,"app_word_count")||!strcmp(task,"app_json_transform")||!strcmp(task,"app_statistics");
}
static int line_order(const void *a,const void *b) {return strcmp(*(char *const *)a,*(char *const *)b);}
static char *sorted_lines(const char *s) {
    char *copy=strdup(s),**lines=allocate((strlen(s)+1)*sizeof(*lines));size_t n=0;char *save;
    for(char *p=strtok_r(copy,"\n",&save);p;p=strtok_r(NULL,"\n",&save))lines[n++]=p;
    qsort(lines,n,sizeof(*lines),line_order);char *out=NULL;size_t size=0;FILE *f=open_memstream(&out,&size);
    for(size_t i=0;i<n;i++)fprintf(f,"%s\n",lines[i]);
    fclose(f);free(lines);free(copy);return out;
}
static Process run_application(const char *source_path,const char *binary,int is_python,const char *task) {
    json_object *rows=member(cases,task,json_type_array),*observations=json_object_new_array();
    int correct=1,timed=0,limited=0;char *python=realpath(python_driver,NULL),*source_abs=realpath(source_path,NULL);
    if(is_python&&!python)die("Build the optional Python driver with make eval-python");
    for(size_t i=0;i<json_object_array_length(rows);i++) {
        json_object *row=json_object_array_get_idx(rows,i),*input;
        char temp[]="/tmp/lm0-eval-application-XXXXXX";if(!mkdtemp(temp))die("Cannot create application directory");
        char *examples=format("%s/examples",temp),*stdlib_path=format("%s/examples/stdlib",temp),*build=format("%s/build",temp);
        if(mkdir(examples,0700)||mkdir(stdlib_path,0700)||mkdir(build,0700))die("Cannot create application fixtures");
        char *words=format("%s/words.txt",stdlib_path),*json=format("%s/input.json",stdlib_path),*output=format("%s/stdlib-transformed.json",build);
        if(json_object_object_get_ex(row,"words",&input))write_text(words,json_object_get_string(input));
        if(json_object_object_get_ex(row,"json",&input))write_text(json,json_object_get_string(input));
        char *native[]={(char *)binary,NULL},*py[]={python,"application",source_abs,"-","-",NULL};
        Process p=process_in(is_python?py:native,run_timeout,temp);
        int ok=!p.timed_out&&!p.limited&&p.code==integer_member(row,"exit_code");
        const char *expected=string_member(row,"stdout");
        if(!strcmp(task,"app_word_count")){char *actual=sorted_lines(p.out),*wanted=sorted_lines(expected);ok&=!strcmp(actual,wanted);free(actual);free(wanted);}
        else ok&=!strcmp(p.out,expected);
        if(!strcmp(task,"app_json_transform")) {
            if(integer_member(row,"exit_code")==0){if(access(output,R_OK))ok=0;else{char *actual=read_text(output);ok&=!strcmp(actual,expected);free(actual);}}
            else if(!access(output,F_OK))ok=0;
        }
        correct&=ok;timed|=p.timed_out;limited|=p.limited;
        json_object *o=json_object_new_object();set_bool(o,"correct",ok);set_integer(o,"exit_code",p.code);set_string(o,"stdout",p.out);set_string(o,"stderr",p.err);json_object_array_add(observations,o);
        process_free(&p);unlink(words);unlink(json);unlink(output);rmdir(stdlib_path);rmdir(examples);rmdir(build);rmdir(temp);
        free(words);free(json);free(output);free(stdlib_path);free(examples);free(build);
    }
    json_object *result=json_object_new_object();set_bool(result,"correct",correct);set_integer(result,"cases",(int64_t)json_object_array_length(rows));json_object_object_add(result,"observations",observations);
    Process p={.out=strdup(json_object_to_json_string_ext(result,JSON_C_TO_STRING_PLAIN)),.err=strdup(""),.code=correct?0:1,.timed_out=timed,.limited=limited};
    json_object_put(result);free(python);free(source_abs);return p;
}
