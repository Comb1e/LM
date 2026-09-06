#define _GNU_SOURCE
#include "v3.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* V3 is a source frontend. Its output is typed V2 and passes the same verifier
 * and backend as handwritten V2. No subprocess or Python frontend is involved. */
extern char *source, *filename, *cli_block, *cli_function, *cli_output;
extern char *cli_replacement, *cli_revision, *diag_phase;
extern size_t source_len, cfg_source, cfg_type_depth;
extern size_t cfg_v3_expansion;
extern CoreToken *tokens, *diag_tok;
extern FILE *jout;
extern void fail(const char *, const char *) __attribute__((noreturn));
extern void json_string(const char *);
extern const char *v3_catalog_source(const char *);
extern char *lm0_sha256(const void *, size_t, char[65]);
long v3_active;

typedef struct Token { char *s; size_t start,end,line,col; } Token;
typedef struct Expr Expr;
struct Expr { char *op, *type; Token *at; Expr *a,*b,*args,*next; };
typedef struct Stmt Stmt;
struct Stmt { char *op,*type; Token *at; Expr *a,*b; Stmt *body,*other,*next; };
typedef struct Param { char *name,*type; struct Param *next; } Param;
typedef struct Function {
    char *name,*type; Param *params; Stmt *body; Token *at; size_t end;
    int flags; struct Function *next;
} Function;
typedef struct Map { size_t start,end; Token *at; struct Map *next; } Map;
typedef struct Var { char *name,*type,*slot,*length; int assigned; struct Var *next; } Var;
typedef struct Value { char *s,*type,*length; } Value;
typedef struct Loop { char *done,*step; struct Loop *parent; } Loop;
typedef struct Out { char *text; size_t size; FILE *file; Map *maps,*tail; char **buffer; size_t *length; } Out;
typedef struct Field { char *owner,*name,*type; struct Field *next; } Field;
typedef struct Temp { char *name,*type,*slot; struct Temp *next; } Temp;
static Temp *temps;
static Token *ts; static size_t nt,pos;
static Function *fns,*lastfn,*current;
static Field *fields;
static Var *vars;
static Loop *loop;
static Out declarations, output, body, slots;
static char *original; static size_t original_size;
static unsigned serial, depth;
static int terminated, lowering;
static jmp_buf semantic_jump;
static const char *pending_code,*pending_message;
static Token *pending_at;
static CoreToken error_token;

static void *mem(size_t n) {
    void *p=calloc(n?n:1,1);
    if(!p) fail("E_TOOL","V3 allocation failed");
    return p;
}
static char *fmt(const char *f,...) {
    va_list a; va_start(a,f); char *s;
    if(vasprintf(&s,f,a)<0) fail("E_TOOL","V3 allocation failed");
    va_end(a); return s;
}
static void locate(Token *t) {
    if(!t) t=ts;
    error_token=(CoreToken){.start=t->start,.end=t->end,.line=t->line,.column=t->col,
        .end_line=t->line,.end_column=t->col+(t->end-t->start)};
    diag_tok=&error_token;
}
static void error(Token *t,const char *code,const char *message) __attribute__((noreturn));
static void error(Token *t,const char *code,const char *message) {
    if(lowering) { pending_code=code;pending_message=message;pending_at=t;longjmp(semantic_jump,1); }
    locate(t);fail(code,message);
}
static void enter(Token *t) { if(++depth>cfg_type_depth) error(t,"E_LIMIT","V3 syntax nesting limit exceeded"); }
static void leave(void) { --depth; }
static Out stream(void) { Out o={0};o.buffer=mem(sizeof(*o.buffer));o.length=mem(sizeof(*o.length));o.file=open_memstream(o.buffer,o.length);if(!o.file) fail("E_TOOL","V3 stream failed");return o; }
static void sync_out(Out *o) {fflush(o->file);o->text=*o->buffer;o->size=*o->length;}
static void emit(Out *o,Token *at,const char *f,...) {
    sync_out(o);size_t start=o->size;
    va_list a;va_start(a,f);vfprintf(o->file,f,a);va_end(a);sync_out(o);
    if(o->size>cfg_source*cfg_v3_expansion) error(at,"E_LIMIT","V3 lowering exceeds source expansion limit");
    Map *m=mem(sizeof(*m));*m=(Map){start,o->size,at,NULL};
    if(o->tail)o->tail->next=m;else o->maps=m;o->tail=m;
}
static void append_out(Out *to,Out *from) {
    sync_out(from);sync_out(to);size_t offset=to->size;
    fputs(from->text?from->text:"",to->file);sync_out(to);
    for(Map *m=from->maps;m;m=m->next) {
        Map *n=mem(sizeof(*n));*n=*m;n->start+=offset;n->end+=offset;n->next=NULL;
        if(to->tail)to->tail->next=n;else to->maps=n;to->tail=n;
    }
}
static void add_token(const char *s,size_t n,size_t start,size_t line,size_t col) {
    ts[nt++]=(Token){strndup(s,n),start,start+n,line,col};
}
static void tokenize(void) {
    ts=mem((original_size*2+8)*sizeof(*ts));nt=pos=0;
    size_t i=0,line=1,col=1,*levels=mem((cfg_type_depth+1)*sizeof(*levels)),level=0;
    while(i<original_size) {
        size_t first=i,spaces=0;
        while(original[i]==' ') {i++;spaces++;}
        if(original[i]=='\t') {Token t={.start=i,.end=i+1,.line=line,.col=spaces+1};error(&t,"E_SYNTAX","V3 indentation uses spaces, not tabs");}
        if(original[i]=='#'||original[i]=='\n'||original[i]=='\r') {
            while(i<original_size&&original[i]!='\n')i++;
            if(i<original_size)i++;
            line++;continue;
        }
        if(i==original_size)break;
        if(spaces>levels[level]) {if(level>=cfg_type_depth)error(ts,"E_LIMIT","Indentation limit exceeded");levels[++level]=spaces;add_token("INDENT",6,i,line,spaces+1);}
        while(spaces<levels[level]) {level--;add_token("DEDENT",6,i,line,spaces+1);}
        if(spaces!=levels[level])error(&ts[nt-1],"E_SYNTAX","Inconsistent indentation");
        col=i-first+1;
        while(i<original_size&&original[i]!='\n'&&original[i]!='\r') {
            unsigned char ch=original[i];size_t start=i,startcol=col;
            if(ch==' '||ch=='\t') {i++;col++;continue;}
            if(ch=='#') {while(i<original_size&&original[i]!='\n')i++;break;}
            if(isalpha(ch)||ch=='_') {do{i++;}while(isalnum((unsigned char)original[i])||original[i]=='_');}
            else if(isdigit(ch)) {
                i++;while(isalnum((unsigned char)original[i])||original[i]=='.'||((original[i]=='+'||original[i]=='-')&&(original[i-1]=='e'||original[i-1]=='E')))i++;
            } else if(ch=='"') {
                i++;while(i<original_size&&original[i]!='"'&&original[i]!='\n') {if(original[i]=='\\'&&i+1<original_size)i++;i++;}
                if(original[i]!='"') {Token t={.start=start,.end=i,.line=line,.col=col};error(&t,"E_SYNTAX","Unterminated string");}i++;
            } else {
                const char *pairs[]={"->","==","!=","<=",">=","<<",">>","//","+=","-=","*=",NULL};
                int two=0;for(int k=0;pairs[k];k++)two|=!strncmp(original+i,pairs[k],2);
                i+=two?2:1;
            }
            add_token(original+start,i-start,start,line,startcol);col+=i-start;
        }
        add_token("\n",1,i,line,col);
        if(original[i]=='\r')i++;if(original[i]=='\n')i++;
        line++;
    }
    while(level--)add_token("DEDENT",6,original_size,line,1);
    add_token("EOF",3,original_size,line,1);
}
static Token *peek(void) {return &ts[pos<nt?pos:nt-1];}
static int is(const char *s) {return !strcmp(peek()->s,s);}
static int accept(const char *s) {if(is(s)){pos++;return 1;}return 0;}
static Token *take(void) {Token *t=peek();if(pos>=nt||!strcmp(t->s,"EOF"))error(t,"E_SYNTAX","Unexpected end of V3 source");pos++;return t;}
static Token *expect(const char *s) {Token *t=peek();if(!accept(s))error(t,"E_SYNTAX",fmt("Expected '%s'",s));return t;}
static char *name(void) {Token *t=take();if(!(isalpha((unsigned char)*t->s)||*t->s=='_')||!strcmp(t->s,"EOF"))error(t,"E_SYNTAX","Expected identifier");if(!strncmp(t->s,"__lm0_",6))error(t,"E_DUPLICATE","The __lm0_ prefix is reserved");return t->s;}
static int starts(const char *s,const char *p) {return !strncmp(s,p,strlen(p));}
static int intrinsic(const char *s) {
    const char *names[]={"len","view","range","cast","stack","alloc","sizeof","alignof","inttoptr","address","load","offset","field","store","copy","move","free","trap","ptrtoint",NULL};
    for(size_t i=0;names[i];i++)if(!strcmp(s,names[i]))return 1;
    return 0;
}
static char *inner(const char *t) {const char *s=strchr(t,'<');return s?strndup(s+1,strlen(s)-2):NULL;}
static char *type(void) {
    Token *at=peek();enter(at);char *s;
    if(accept("[")){char *t=type();expect(";");char *n=take()->s;expect("]");s=fmt("[%s;%s]",t,n);}
    else {s=name();if(!strcmp(s,"ptr")||!strcmp(s,"slice")){expect("<");char *t=type();if(is(">>")){peek()->s=">";}else expect(">");s=fmt("%s<%s>",s,t);}}
    leave();return s;
}
static Expr *expression(int minimum);
static Expr *expr(const char *op,Token *t,Expr *a,Expr *b) {Expr *e=mem(sizeof(*e));e->op=(char *)op;e->at=t;e->a=a;e->b=b;return e;}
static int precedence(const char *s) {
    if(!strcmp(s,"or"))return 1;if(!strcmp(s,"and"))return 2;
    if(!strcmp(s,"==")||!strcmp(s,"!=")||!strcmp(s,"<")||!strcmp(s,"<=")||!strcmp(s,">")||!strcmp(s,">="))return 3;
    if(!strcmp(s,"|"))return 4;if(!strcmp(s,"^"))return 5;if(!strcmp(s,"&"))return 6;
    if(!strcmp(s,"<<")||!strcmp(s,">>"))return 7;
    if(!strcmp(s,"+")||!strcmp(s,"-"))return 8;
    if(!strcmp(s,"*")||!strcmp(s,"/")||!strcmp(s,"//")||!strcmp(s,"%"))return 9;
    return 0;
}
static Expr *expression(int minimum) {
    Token *t=peek();enter(t);Expr *e;
    if(accept("-")||accept("~")||accept("not"))e=expr(t->s,t,expression(!strcmp(t->s,"not")?3:10),NULL);
    else if(accept("(")){e=expression(1);expect(")");}
    else {
        take();
        if(!(isalnum((unsigned char)*t->s)||*t->s=='_'||*t->s=='"')||!strcmp(t->s,"INDENT")||!strcmp(t->s,"DEDENT"))error(t,"E_SYNTAX","Expected expression");
        e=expr("atom",t,NULL,NULL);
        if((!strcmp(t->s,"cast")||!strcmp(t->s,"stack")||!strcmp(t->s,"alloc")||!strcmp(t->s,"sizeof")||!strcmp(t->s,"alignof")||!strcmp(t->s,"inttoptr"))&&accept("<")) {e->type=type();expect(">");}
        if(accept("(")) {
            e->op="call";Expr **tail=&e->args;
            if(!is(")"))do{*tail=expression(1);tail=&(*tail)->next;}while(accept(","));
            expect(")");
        } else if(is(":")&&pos+1<nt&&strcmp(ts[pos+1].s,"\n")) {take();e->type=type();}
    }
    while(accept("[")){Token *at=&ts[pos-1];Expr *index=expression(1);expect("]");e=expr("index",at,e,index);}
    while(precedence(peek()->s)>=minimum) {t=take();e=expr(t->s,t,e,expression(precedence(t->s)+1));}
    leave();return e;
}
static Stmt *suite(void);
static Stmt *statement(void) {
    Token *at=peek();Stmt *s=mem(sizeof(*s));s->at=at;s->op=at->s;
    if(accept("if")||accept("elif")||accept("while")) {
        s->a=expression(1);expect(":");s->body=suite();
        if(!strcmp(s->op,"if")||!strcmp(s->op,"elif")) {
            if(is("elif"))s->other=statement();
            else if(accept("else")){expect(":");s->other=suite();}
        }
        return s;
    }
    if(accept("for")) {s->a=expr("atom",peek(),NULL,NULL);name();expect("in");s->b=expression(1);expect(":");s->body=suite();return s;}
    if(accept("return")){if(!is("\n"))s->a=expression(1);}
    else if(accept("break")||accept("continue")||accept("pass")){}
    else {
        s->op="expression";s->a=expression(1);
        if(is("=")||is("+=")||is("-=")||is("*=")){s->op=take()->s;s->type=s->a->type;s->a->type=NULL;s->b=expression(1);}
    }
    expect("\n");return s;
}
static Stmt *suite(void) {
    enter(peek());expect("\n");expect("INDENT");Stmt *head=NULL,**tail=&head;
    while(!is("DEDENT")&&!is("EOF")){*tail=statement();tail=&(*tail)->next;}
    expect("DEDENT");leave();return head;
}
static void register_fn(Function *f) {
    if(intrinsic(f->name))error(f->at,"E_DUPLICATE","Intrinsic function names are reserved");
    for(Function *x=fns;x;x=x->next)if(!strcmp(x->name,f->name))error(f->at,"E_DUPLICATE","Duplicate function");
    if(lastfn)lastfn->next=f;else fns=f;lastfn=f;
}
/* Parse catalogue ABI declarations without copying their contracts or exposing
 * private layouts as public convenience operations. */
static void library(const char *module,Token *at) {
    const char *src=v3_catalog_source(module);if(!src)error(at,"E_LIBRARY","Unknown standard library module");
    for(Function *f=fns;f;f=f->next)if(f->flags==3&&!strcmp(f->name,module))return;
    Function *mark=mem(sizeof(*mark));mark->flags=3;mark->name=(char *)module;mark->at=at;register_fn(mark);
    char *copy=strdup(src),*save;
    for(char *line=strtok_r(copy,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        if(starts(line,"use ")){library(line+4,at);continue;}
        if(!starts(line,"extern c fn @"))continue;
        char *p=line+13,*open=strchr(p,'('),*close=strchr(p,')');if(!open||!close)continue;
        Function *f=mem(sizeof(*f));f->name=strndup(p,(size_t)(open-p));f->type=strdup(close+5);f->flags=1;f->at=at;
        Param **tail=&f->params;p=open+1;
        while(p<close){while(*p==' '||*p==',')p++;if(p==close)break;if(*p=='%')p++;char *colon=strchr(p,':'),*end=strchr(colon,',');if(!end||end>close)end=close;
            Param *q=mem(sizeof(*q));q->name=strndup(p,(size_t)(colon-p));q->type=strndup(colon+1,(size_t)(end-colon-1));*tail=q;tail=&q->next;p=end;}
        register_fn(f);
    }
}
static void parse(void) {
    expect("module");char *module=name();expect("version");expect("3");expect("\n");
    declarations=stream();emit(&declarations,ts,"module %s version 2\n",module);
    while(!is("EOF")) {
        Token *at=peek();
        if(accept("use")){char *m=name();expect("\n");library(m,at);emit(&declarations,at,"use %s\n",m);continue;}
        if(accept("data")){char *n=name();expect("=");Token *str=take();if(*str->s!='"')error(str,"E_SYNTAX","Data requires a JSON string");expect("\n");emit(&declarations,at,"data @%s = %s\n",n,str->s);continue;}
        if(accept("struct")) {
            char *owner=name();expect(":");expect("\n");expect("INDENT");emit(&declarations,at,"struct %s {\n",owner);
            while(!is("DEDENT")){Token *a=peek();char *n=name();expect(":");char *t=type();expect("\n");Field *f=mem(sizeof(*f));*f=(Field){owner,n,t,fields};fields=f;emit(&declarations,a,"%s:%s\n",n,t);}
            expect("DEDENT");emit(&declarations,at,"}\n");continue;
        }
        Function *f=mem(sizeof(*f));f->at=at;
        if(accept("export")){expect("c");f->flags=2;}else if(accept("extern")){expect("c");f->flags=1;}
        expect("fn");f->name=name();expect("(");Param **tail=&f->params;
        if(!is(")"))do{Param *p=mem(sizeof(*p));p->name=name();expect(":");p->type=type();for(Param *q=f->params;q;q=q->next)if(!strcmp(q->name,p->name))error(at,"E_DUPLICATE","Duplicate parameter");*tail=p;tail=&p->next;}while(accept(","));
        expect(")");expect("->");f->type=type();
        if(starts(f->type,"slice<"))error(at,"E_UNSUPPORTED","V3 slice returns are not supported");
        if(f->flags==1)expect("\n");else {expect(":");f->body=suite();}
        f->end=peek()->start;register_fn(f);
    }
}

static char *fresh(void) {return fmt("__lm0_%u",++serial);}
static char *label(void) {return fresh();}
static Value value(char *s,char *t) {return (Value){s,t,NULL};}
static Value instruction(Token *at,char *t,const char *f,...) {
    va_list a;va_start(a,f);char *s;if(vasprintf(&s,f,a)<0)fail("E_TOOL","Allocation failed");va_end(a);
    char *r=fresh(),*slot=fresh();Temp *tmp=mem(sizeof(*tmp));*tmp=(Temp){r,t,slot,temps};temps=tmp;
    emit(&slots,at,"%%%s = stack %s, 1\n",slot,t);
    emit(&body,at,"%%%s:%s = %s\n",r,t,s);emit(&body,at,"store %%%s, %%%s\n",slot,r);
    return value(fmt("%%%s",r),t);
}
static void same(Token *at,const char *a,const char *b) {if(!a||!b||strcmp(a,b))error(at,"E_TYPE",fmt("Expected %s, got %s",b?b:"a scalar",a?a:"unknown"));}
static void numeric(Token *at,const char *t) {if(!strchr("iuf",*t)||!isdigit((unsigned char)t[1]))error(at,"E_TYPE","Expected numeric operand");}
static Var *findvar(const char *n) {for(Var *v=vars;v;v=v->next)if(!strcmp(v->name,n))return v;return NULL;}
static Var *variable(Token *at,const char *n,char *t) {
    Var *v=findvar(n);if(v){same(at,t,v->type);return v;}
    v=mem(sizeof(*v));v->name=(char *)n;v->type=t;v->slot=fresh();v->next=vars;vars=v;
    if(starts(t,"slice<")){emit(&slots,at,"%%%s = stack ptr<%s>, 1\n",v->slot,inner(t));v->length=fresh();emit(&slots,at,"%%%s = stack u64, 1\n",v->length);}
    else emit(&slots,at,"%%%s = stack %s, 1\n",v->slot,t);
    return v;
}
/* Slot pointers are entry values, so carry them as hidden function-local block
 * parameters. Every edge passes these pointers, never uninitialized contents. */
static void mark(const char *name) {emit(&body,current->at,"^%s:\n",name);terminated=0;}
static void jump(const char *name) {if(!terminated){emit(&body,current->at,"jump ^%s()\n",name);terminated=1;}}
static void branch(Token *at,Value c,const char *yes,const char *no) {same(at,c.type,"bool");emit(&body,at,"branch %s, ^%s(), ^%s()\n",c.s,yes,no);terminated=1;}
static void guard(Token *at,Value c) {char *good=label(),*bad=label();branch(at,c,good,bad);mark(bad);emit(&body,at,"trap\n");terminated=1;mark(good);}
static Value loadvar(Token *at,Var *v) {
    if(!v||!v->assigned)error(at,"E_REGISTER","Variable is not definitely assigned");
    if(starts(v->type,"slice<")){Value p=instruction(at,fmt("ptr<%s>",inner(v->type)),"load %%%s",v->slot);Value n=instruction(at,"u64","load %%%s",v->length);p.type=v->type;p.length=n.s;return p;}
    return instruction(at,v->type,"load %%%s",v->slot);
}
static void storevar(Token *at,Var *v,Value x) {same(at,x.type,v->type);emit(&body,at,"store %%%s, %s\n",v->slot,x.s);if(v->length)emit(&body,at,"store %%%s, %s\n",v->length,x.length);v->assigned=1;}
static Value lower(Expr *,char *);
static char *hint(Expr *e) {
    if(!e)return NULL;if(e->type)return e->type;
    if(!strcmp(e->op,"atom")){Var *v=findvar(e->at->s);return v?v->type:NULL;}
    if(!strcmp(e->op,"index")){char *t=hint(e->a);return t?inner(t):NULL;}
    if(!strcmp(e->op,"call")){for(Function *f=fns;f;f=f->next)if(f->flags!=3&&!strcmp(e->at->s,f->name))return f->type;}
    return NULL;
}
static Value index_address(Expr *e) {
    Value p=lower(e->a,NULL);if(!starts(p.type,"slice<"))error(e->at,"E_TYPE","Indexing requires slice<T>; use view(pointer, length)");
    Value i=lower(e->b,"i64");same(e->at,i.type,"i64");
    guard(e->at,instruction(e->at,"bool","ge %s, 0:i64",i.s));
    Value u=instruction(e->at,"u64","cast %s",i.s);
    guard(e->at,instruction(e->at,"bool","lt %s, %s",u.s,p.length));
    return instruction(e->at,fmt("ptr<%s>",inner(p.type)),"offset %s, %s",p.s,i.s);
}
static int nargs(Expr *e) {int n=0;for(Expr *x=e->args;x;x=x->next)n++;return n;}
static void arity(Expr *e,int n) {if(nargs(e)!=n)error(e->at,"E_ARITY","Incorrect argument count");}
static Value call(Expr *e,char *context) {
    char *n=e->at->s;Expr *a=e->args;int count=nargs(e);
    if(!strcmp(n,"len")){arity(e,1);Value p=lower(a,NULL);if(!p.length)error(e->at,"E_TYPE","len requires a slice");return instruction(e->at,"i64","cast %s",p.length);}
    if(!strcmp(n,"view")){arity(e,2);Value p=lower(a,NULL);if(!starts(p.type,"ptr<")||!strcmp(p.type,"ptr<void>"))error(e->at,"E_TYPE","view requires a storage pointer");Value l=lower(a->next,"u64");same(e->at,l.type,"u64");guard(e->at,instruction(e->at,"bool","le %s, 9223372036854775807:u64",l.s));p.type=fmt("slice<%s>",inner(p.type));p.length=l.s;return p;}
    if(!strcmp(n,"cast")||!strcmp(n,"inttoptr")){arity(e,1);if(!e->type)error(e->at,"E_INFER","Conversion requires <destination type>");Value x=lower(a,NULL);return instruction(e->at,e->type,"%s %s",n,x.s);}
    if(!strcmp(n,"stack")||!strcmp(n,"alloc")){arity(e,1);if(!e->type)error(e->at,"E_INFER","Allocation requires <element type>");
        if(!strcmp(n,"stack")){if(strcmp(a->op,"atom")||!isdigit((unsigned char)*a->at->s))error(e->at,"E_STACK","stack count must be a positive integer literal");char *r=fresh();emit(&slots,e->at,"%%%s = stack %s, %s\n",r,e->type,a->at->s);return value(fmt("%%%s",r),fmt("ptr<%s>",e->type));}
        Value x=lower(a,"u64");same(e->at,x.type,"u64");return instruction(e->at,fmt("ptr<%s>",e->type),"alloc %s, %s",e->type,x.s);}
    if(!strcmp(n,"sizeof")||!strcmp(n,"alignof")){arity(e,0);if(!e->type)error(e->at,"E_INFER","Layout query requires <type>");return instruction(e->at,"u64","%s %s",n,e->type);}
    if(!strcmp(n,"address")){arity(e,1);if(strcmp(a->op,"atom"))error(e->at,"E_SYNTAX","address requires a data name");return instruction(e->at,"ptr<u8>","address @%s",a->at->s);}
    if(!strcmp(n,"load")){arity(e,1);Value p=lower(a,NULL);if(!starts(p.type,"ptr<"))error(e->at,"E_TYPE","load requires a pointer");return instruction(e->at,inner(p.type),"load %s",p.s);}
    if(!strcmp(n,"offset")){arity(e,2);Value p=lower(a,NULL),i=lower(a->next,"i64");same(e->at,i.type,"i64");return instruction(e->at,p.type,"offset %s, %s",p.s,i.s);}
    if(!strcmp(n,"field")){arity(e,2);if(strcmp(a->next->op,"atom"))error(e->at,"E_SYNTAX","field requires a field name");Value p=lower(a,NULL);char *owner=inner(p.type),*t=NULL;
        for(Field *f=fields;f;f=f->next)if(owner&&!strcmp(owner,f->owner)&&!strcmp(a->next->at->s,f->name))t=f->type;
        if(!t)error(e->at,"E_FIELD","Unknown field (library handles are private)");return instruction(e->at,fmt("ptr<%s>",t),"field %s, %s",p.s,a->next->at->s);}
    if(!strcmp(n,"store")){arity(e,2);Value p=lower(a,NULL);char *t=inner(p.type);if(!t)error(e->at,"E_TYPE","store requires a pointer");Value x=lower(a->next,t);same(e->at,x.type,t);emit(&body,e->at,"store %s, %s\n",p.s,x.s);return value("","void");}
    if(!strcmp(n,"copy")||!strcmp(n,"move")){arity(e,3);Value p=lower(a,NULL),q=lower(a->next,NULL),c=lower(a->next->next,"u64");same(e->at,c.type,"u64");emit(&body,e->at,"%s %s, %s, %s\n",n,p.s,q.s,c.s);return value("","void");}
    if(!strcmp(n,"free")){arity(e,1);Value p=lower(a,NULL);emit(&body,e->at,"free %s\n",p.s);return value("","void");}
    if(!strcmp(n,"trap")){arity(e,0);emit(&body,e->at,"trap\n");terminated=1;return value("","void");}
    if(!strcmp(n,"ptrtoint")){arity(e,1);Value p=lower(a,NULL);return instruction(e->at,"u64","ptrtoint %s",p.s);}
    Function *f;for(f=fns;f;f=f->next)if(f->flags!=3&&!strcmp(n,f->name))break;
    if(!f)error(e->at,"E_FUNCTION",fmt("Unknown function '%s'",n));
    char *args=NULL;size_t size=0;FILE *out=open_memstream(&args,&size);Param *p=f->params;int k=0;
    for(;a&&p;a=a->next,p=p->next){Value v=lower(a,p->type);same(e->at,v.type,p->type);fprintf(out,"%s%s",k++?", ":"",v.s);if(v.length)fprintf(out,", %s",v.length);}
    if(a||p)error(e->at,"E_ARITY",fmt("Incorrect arguments to %s (%d supplied)",n,count));fclose(out);
    (void)context;
    if(!strcmp(f->type,"void")){emit(&body,e->at,"call @%s(%s)\n",n,args);return value("","void");}
    return instruction(e->at,f->type,"call @%s(%s)",n,args);
}
static const char *opcode(const char *s) {
    static const char *ops[][2]={{"+","add"},{"-","sub"},{"*","mul"},{"/","div"},{"//","div"},{"%","rem"},{"&","and"},{"|","or"},{"^","xor"},{"<<","shl"},{">>","shr"},{"==","eq"},{"!=","ne"},{"<","lt"},{"<=","le"},{">","gt"},{">=","ge"}};
    for(size_t i=0;i<sizeof(ops)/sizeof(*ops);i++)if(!strcmp(s,ops[i][0]))return ops[i][1];return NULL;
}
static Value lower(Expr *e,char *context) {
    enter(e->at);Value v;
    if(!strcmp(e->op,"atom")) {
        char *s=e->at->s,*t=e->type?e->type:context;
        if(!strcmp(s,"true")||!strcmp(s,"false")){v=value(s,"bool");}
        else if(isdigit((unsigned char)*s)||!strcmp(s,"inf")||!strcmp(s,"nan")){if(!t)t=!starts(s,"0x")&&!starts(s,"0X")&&(strchr(s,'.')||strchr(s,'e')||strchr(s,'E')||!strcmp(s,"inf")||!strcmp(s,"nan"))?"f64":"i64";numeric(e->at,t);v=value(fmt("%s:%s",s,t),t);}
        else if(!strcmp(s,"null")){if(!t||!starts(t,"ptr<"))error(e->at,"E_INFER","null needs a pointer type");v=value(fmt("null:%s",t),t);}
        else {v=loadvar(e->at,findvar(s));if(e->type)same(e->at,v.type,e->type);}
    } else if(!strcmp(e->op,"call"))v=call(e,context);
    else if(!strcmp(e->op,"index")){Value p=index_address(e);v=instruction(e->at,inner(p.type),"load %s",p.s);}
    else if(!e->b) {
        if(!strcmp(e->op,"-")&&!strcmp(e->a->op,"atom")&&isdigit((unsigned char)*e->a->at->s)){char *t=e->a->type?e->a->type:context;char *s=e->a->at->s;if(!t)t=!starts(s,"0x")&&!starts(s,"0X")&&(strchr(s,'.')||strchr(s,'e')||strchr(s,'E'))?"f64":"i64";numeric(e->at,t);v=value(fmt("-%s:%s",s,t),t);}
        else {Value a=lower(e->a,!strcmp(e->op,"not")?"bool":context);if(!strcmp(e->op,"not"))same(e->at,a.type,"bool");else numeric(e->at,a.type);v=instruction(e->at,a.type,"%s %s",!strcmp(e->op,"-")?"neg":"not",a.s);}
    } else if(!strcmp(e->op,"and")||!strcmp(e->op,"or")) {
        Value a=lower(e->a,"bool");same(e->at,a.type,"bool");Var *vslot=variable(e->at,fresh(),"bool");storevar(e->at,vslot,a);
        char *rhs=label(),*end=label();branch(e->at,a,!strcmp(e->op,"and")?rhs:end,!strcmp(e->op,"and")?end:rhs);mark(rhs);
        Value b=lower(e->b,"bool");same(e->at,b.type,"bool");storevar(e->at,vslot,b);jump(end);mark(end);v=loadvar(e->at,vslot);
    } else {
        int comparison_op=precedence(e->op)==3;
        char *t=hint(e->a);if(!t)t=hint(e->b);if(!t&&!comparison_op)t=context;
        Value a=lower(e->a,t),b=lower(e->b,a.type);same(e->at,b.type,a.type);
        if(!comparison_op)numeric(e->at,a.type);
        v=instruction(e->at,comparison_op?"bool":a.type,"%s %s, %s",opcode(e->op),a.s,b.s);
    }
    leave();return v;
}

typedef struct State { Var *v; int assigned; struct State *next; } State;
static State *state(void) {State *s=NULL;for(Var *v=vars;v;v=v->next){State *x=mem(sizeof(*x));*x=(State){v,v->assigned,s};s=x;}return s;}
static int assigned(State *s,Var *v) {for(;s;s=s->next)if(s->v==v)return s->assigned;return 0;}
static void restore(State *s) {for(Var *v=vars;v;v=v->next)v->assigned=assigned(s,v);}
static void statements(Stmt *s);
static void conditional(Stmt *s) {
    Value c=lower(s->a,"bool");State *before=state();char *yes=label(),*no=label(),*end=label();branch(s->at,c,yes,no);
    mark(yes);statements(s->body);int yes_exit=terminated;State *after_yes=state();jump(end);
    restore(before);mark(no);statements(s->other);int no_exit=terminated;State *after_no=state();jump(end);
    for(Var *v=vars;v;v=v->next)v->assigned=yes_exit?assigned(after_no,v):no_exit?assigned(after_yes,v):assigned(after_yes,v)&&assigned(after_no,v);
    if(yes_exit&&no_exit)terminated=1;else mark(end);
}
static void iteration(Stmt *s) {
    State *before=state();Loop l={label(),label(),loop};loop=&l;char *check=label(),*step=label();
    Var *index=NULL,*bound=NULL,*stride=NULL,*sequence=NULL;int range=0;
    if(!strcmp(s->op,"for")) {
        range=!strcmp(s->b->op,"call")&&!strcmp(s->b->at->s,"range");
        Value start=value("0:i64","i64"),stop,increment=value("1:i64","i64");
        if(range){int n=nargs(s->b);if(n<1||n>3)error(s->at,"E_ARITY","range accepts stop or start, stop[, positive step]");Expr *a=s->b->args;if(n>=2){start=lower(a,"i64");a=a->next;}stop=lower(a,"i64");if(n==3)increment=lower(a->next,"i64");same(s->at,start.type,"i64");same(s->at,stop.type,"i64");same(s->at,increment.type,"i64");guard(s->at,instruction(s->at,"bool","gt %s, 0:i64",increment.s));}
        else {Value x=lower(s->b,NULL);if(!x.length)error(s->at,"E_TYPE","for requires a slice or range");sequence=variable(s->at,fresh(),x.type);storevar(s->at,sequence,x);stop=instruction(s->at,"i64","cast %s",x.length);}
        index=variable(s->at,fresh(),"i64");bound=variable(s->at,fresh(),"i64");stride=variable(s->at,fresh(),"i64");storevar(s->at,index,start);storevar(s->at,bound,stop);storevar(s->at,stride,increment);
    }
    jump(check);mark(check);
    if(index){Value i=loadvar(s->at,index),n=loadvar(s->at,bound);branch(s->at,instruction(s->at,"bool","lt %s, %s",i.s,n.s),step,l.done);}
    else branch(s->at,lower(s->a,"bool"),step,l.done);
    mark(step);
    if(index){Value x=loadvar(s->at,index);if(!range){Value p=loadvar(s->at,sequence);Value addr=instruction(s->at,fmt("ptr<%s>",inner(p.type)),"offset %s, %s",p.s,x.s);x=instruction(s->at,inner(p.type),"load %s",addr.s);}Var *v=variable(s->at,s->a->at->s,x.type);storevar(s->at,v,x);}
    statements(s->body);jump(l.step);mark(l.step);
    if(index){Value i=loadvar(s->at,index),inc=loadvar(s->at,stride);Value next=instruction(s->at,"i64","add %s, %s",i.s,inc.s);/* Positive overflow crosses the endpoint, so stop rather than wrap. */
        char *advance=label();branch(s->at,instruction(s->at,"bool","gt %s, %s",next.s,i.s),advance,l.done);mark(advance);storevar(s->at,index,next);}
    jump(check);mark(l.done);loop=l.parent;restore(before);
}
static void statements(Stmt *s) {
    for(;s;s=s->next) {
        if(terminated)error(s->at,"E_TERMINATOR","Statement is unreachable after termination");
        if(!strcmp(s->op,"if")||!strcmp(s->op,"elif")){conditional(s);continue;}
        if(!strcmp(s->op,"while")||!strcmp(s->op,"for")){iteration(s);continue;}
        if(!strcmp(s->op,"break")||!strcmp(s->op,"continue")){if(!loop)error(s->at,"E_BLOCK","Loop control outside a loop");jump(!strcmp(s->op,"break")?loop->done:loop->step);continue;}
        if(!strcmp(s->op,"return")){if(!s->a){same(s->at,current->type,"void");emit(&body,s->at,"return\n");}else{Value x=lower(s->a,current->type);same(s->at,x.type,current->type);emit(&body,s->at,"return %s\n",x.s);}terminated=1;continue;}
        if(!strcmp(s->op,"pass"))continue;
        if(!strcmp(s->op,"expression")){Value x=lower(s->a,NULL);if(strcmp(x.type,"void"))error(s->at,"E_RESULT","A non-void result must be assigned");continue;}
        Value pointer={0},x;Var *v=NULL;char *t=s->type;
        if(!strcmp(s->a->op,"index")){pointer=index_address(s->a);t=inner(pointer.type);}
        else if(!strcmp(s->a->op,"atom")){v=findvar(s->a->at->s);if(v){if(t)same(s->at,t,v->type);t=v->type;}}
        else error(s->at,"E_SYNTAX","Assignment requires a variable or indexed element");
        if(strcmp(s->op,"=")){Value old=pointer.s?instruction(s->at,t,"load %s",pointer.s):loadvar(s->at,v);Value rhs=lower(s->b,t);same(s->at,rhs.type,old.type);x=instruction(s->at,old.type,"%s %s, %s",s->op[0]=='+'?"add":s->op[0]=='-'?"sub":"mul",old.s,rhs.s);}
        else x=lower(s->b,t);
        if(t)same(s->at,x.type,t);if(!strcmp(x.type,"void"))error(s->at,"E_RESULT","Cannot assign void");
        if(pointer.s)emit(&body,s->at,"store %s, %s\n",pointer.s,x.s);
        else {if(!v)v=variable(s->at,s->a->at->s,x.type);storevar(s->at,v,x);}
    }
}

/* V2 block-local scope requires slot addresses on every edge. Rewrite only
 * generated block headers and edge spellings, carrying entry stack pointers. */
static void carried_body(Out *to) {
    char *params=NULL,*args=NULL;size_t pn=0,an=0;FILE *p=open_memstream(&params,&pn),*a=open_memstream(&args,&an);int n=0;
    sync_out(&slots);
    for(char *line=slots.text;line&&*line;) {
        char *end=strchr(line,'\n');char *eq=strstr(line," = stack ");
        if(eq&&(!end||eq<end)){char *comma=strstr(eq+9,", ");char *name=strndup(line+1,(size_t)(eq-line-1)),*t=strndup(eq+9,(size_t)(comma-eq-9));fprintf(p,"%s%%%s:ptr<%s>",n?", ":"",name,t);fprintf(a,"%s%%%s",n++?", ":"",name);}
        line=end?end+1:NULL;
    }
    fclose(p);fclose(a);sync_out(&body);
    char *defined=strdup("|");
    for(Map *m=body.maps;m;m=m->next) {
        char *s=strndup(body.text+m->start,m->end-m->start);
        if(*s=='^'){char *colon=strchr(s,':');if(colon){*colon=0;emit(to,m->at,"%s(%s):\n",s,params);defined=strdup("|");continue;}}
        char *rewritten=NULL;size_t rn=0;FILE *r=open_memstream(&rewritten,&rn);
        for(char *scan=s;*scan;) {
            if(*scan=='%') {
                char *end=scan+1;while(isalnum((unsigned char)*end)||*end=='_')end++;
                char *name=strndup(scan+1,(size_t)(end-scan-1));Temp *tmp;
                for(tmp=temps;tmp;tmp=tmp->next)if(!strcmp(tmp->name,name))break;
                if(tmp&&scan==s&&*end==':')defined=fmt("%s%s|",defined,name);
                if(tmp&&!strstr(defined,fmt("|%s|",name))){char *load=fresh();emit(to,m->at,"%%%s:%s = load %%%s\n",load,tmp->type,tmp->slot);fprintf(r,"%%%s",load);}
                else fprintf(r,"%%%s",name);
                scan=end;
            } else if((starts(s,"jump ")||starts(s,"branch "))&&starts(scan,"()")){fprintf(r,"(%s)",args);scan+=2;}
            else fputc(*scan++,r);
        }
        fclose(r);emit(to,m->at,"%s",rewritten);
    }
}
static void signature(Out *out,Function *f) {
    emit(out,f->at,"%sfn @%s(",f->flags==2?"export c ":f->flags==1?"extern c ":"",f->name);int n=0;
    for(Param *p=f->params;p;p=p->next) {
        if(starts(p->type,"slice<"))emit(out,f->at,"%s%%%s:ptr<%s>, %%%s__length:u64",n++?", ":"",p->name,inner(p->type),p->name);
        else emit(out,f->at,"%s%%%s:%s",n++?", ":"",p->name,p->type);
    }
    emit(out,f->at,") -> %s",f->type);
}
static void function(Function *f) {
    current=f;vars=NULL;temps=NULL;loop=NULL;terminated=0;body=stream();slots=stream();depth=0;
    for(Param *p=f->params;p;p=p->next){Var *v=variable(f->at,p->name,p->type);Value x=value(fmt("%%%s",p->name),p->type);if(starts(p->type,"slice<"))x.length=fmt("%%%s__length",p->name);storevar(f->at,v,x);}
    /* Initial stores use parameter values; they execute in entry before edges. */
    Out initial=body;body=stream();
    for(Param *p=f->params;p;p=p->next)if(starts(p->type,"slice<")){Value x=loadvar(f->at,findvar(p->name));guard(f->at,instruction(f->at,"bool","le %s, 9223372036854775807:u64",x.length));}
    statements(f->body);
    if(!terminated){if(strcmp(f->type,"void"))error(f->at,"E_TERMINATOR","Not every path returns a value");emit(&body,f->at,"return\n");}
    signature(&output,f);emit(&output,f->at," {\n^entry:\n");append_out(&output,&slots);append_out(&output,&initial);
    char *start=label();
    /* A synthetic entry edge shares the exact pointer list used by body edges. */
    Out saved=body;body=stream();emit(&body,f->at,"jump ^%s()\n",start);mark(start);append_out(&body,&saved);carried_body(&output);
    Token *end=mem(sizeof(*end));*end=*f->at;end->start=end->end=f->end;emit(&output,end,"}\n");
}

void v3_prepare(void) {
    original=source;original_size=source_len;v3_active=0;pending_code=pending_message=NULL;pending_at=NULL;depth=0;
    /* Only an exact version-3 module header selects the new grammar. */
    const char *p=source;while(*p){while(isspace((unsigned char)*p))p++;if(*p!='#')break;while(*p&&*p!='\n')p++;}
    char mod[128];int version=0;if(sscanf(p,"module %127s version %d",mod,&version)!=2||version!=3)return;
    v3_active=1;fns=lastfn=NULL;fields=NULL;serial=0;lowering=0;
    tokenize();parse();output=stream();append_out(&output,&declarations);
    if(setjmp(semantic_jump)==0){lowering=1;for(Function *f=fns;f;f=f->next)if(f->flags!=3){if(f->flags==1){/* Imported functions already arrive through use. */if(starts(f->at->s,"extern")){signature(&output,f);emit(&output,f->at,"\n");}}else function(f);}lowering=0;}
    else {
        lowering=0;depth=0;output=stream();append_out(&output,&declarations);
        /* Keep parseable source selectable even when high-level validation
         * fails. v3_verify replays the original diagnostic before core verify. */
        for(Function *f=fns;f;f=f->next)if(f->flags!=3&&f->flags!=1){signature(&output,f);emit(&output,f->at," {\n^entry:\ntrap\n");Token *end=mem(sizeof(*end));*end=*f->at;end->start=end->end=f->end;emit(&output,end,"}\n");}
    }
    sync_out(&output);source=output.text;source_len=output.size;
}
void v3_finish_lex(void) {
    if(!v3_active)return;
    Map *m=output.maps;
    for(CoreToken *t=tokens;t;t=t->next){while(m&&m->next&&t->start>=m->end)m=m->next;Token *at=m?m->at:ts;
        t->start=at->start;t->end=at->end;t->line=at->line;t->column=at->col;t->end_line=at->line;t->end_column=at->col+(at->end-at->start);}
    source=original;source_len=original_size;
}
void v3_verify(void) {if(v3_active&&pending_code){locate(pending_at);diag_phase="verify";fail(pending_code,pending_message);}}
int v3_signature(void *record) {
    if(!v3_active)return 0;char *name=*(char **)((char *)record+8);Function *f;
    for(f=fns;f;f=f->next)if(f->flags!=3&&!strcmp(f->name,name))break;if(!f)return 0;
    fputs("{\"name\":",jout);json_string(f->name);fputs(",\"params\":[",jout);int n=0;
    for(Param *p=f->params;p;p=p->next){fprintf(jout,"%s{\"name\":",n++?",":"");json_string(p->name);fputs(",\"type\":",jout);json_string(p->type);fputc('}',jout);}
    fputs("],\"returns\":",jout);json_string(f->type);fprintf(jout,",\"external\":%s,\"exported\":%s}",f->flags==1?"true":"false",f->flags==2?"true":"false");return 1;
}

void v3_unresolved(void) {
    extern void unresolved_dependency(const char *,const char *);
    if(!v3_active||!pending_code)return;
    if(!strcmp(pending_code,"E_FUNCTION"))unresolved_dependency("function",pending_at->s);
    else if(!strcmp(pending_code,"E_REGISTER"))unresolved_dependency("variable",pending_at->s);
}

static void dependency(const char *name,int data) {
    extern void *functions,*data_nodes;
    for(char *n=data?data_nodes:functions;n;n=*(char **)n)if(!strcmp(*(char **)(n+8),name)){*(uint64_t *)(n+72)=1;break;}
}
static void expression_dependencies(Expr *e) {
    if(!e)return;
    if(!strcmp(e->op,"call")) {
        if(!strcmp(e->at->s,"address")&&e->args)dependency(e->args->at->s,1);
        else dependency(e->at->s,0);
    }
    expression_dependencies(e->a);expression_dependencies(e->b);expression_dependencies(e->args);expression_dependencies(e->next);
}
static void statement_dependencies(Stmt *s) {
    for(;s;s=s->next){expression_dependencies(s->a);expression_dependencies(s->b);statement_dependencies(s->body);statement_dependencies(s->other);}
}
void v3_dependencies(void) {
    if(!v3_active||!cli_function)return;
    for(Function *f=fns;f;f=f->next)if(f->body&&!strcmp(f->name,cli_function))statement_dependencies(f->body);
}

void v3_replace(void) {
    extern void parse_module(void),verify(void),write_source(void);
    if(cli_block)fail("E_UNSUPPORTED","V3 replacement selects a function");
    if(!cli_function||!cli_output||!cli_replacement)fail("E_REPLACE","Function, replacement and output are required");
    char hash[65];if(!lm0_sha256(source,source_len,hash))fail("E_TOOL","Cannot hash source");
    if(cli_revision&&strcmp(hash,cli_revision))fail("E_STALE","Source revision changed; inspect again");
    Function *selected;for(selected=fns;selected;selected=selected->next)if(selected->flags!=3&&!strcmp(selected->name,cli_function))break;
    if(!selected||selected->flags==1)fail("E_REPLACE","Selected function has no editable body");
    size_t start=selected->at->start,end=selected->end,old_size=source_len;char *old=source;
    FILE *file=fopen(cli_replacement,"rb");if(!file)fail("E_TOOL","Cannot read replacement");
    char *fragment=mem(cfg_source+2);size_t n=fread(fragment,1,cfg_source+1,file);
    if(ferror(file)||n>cfg_source||memchr(fragment,0,n))fail("E_LIMIT","Invalid or oversized replacement");fclose(file);
    source=fmt("module fragment version 3\n%s\n",fragment);source_len=strlen(source);v3_prepare();
    int count=0;Function *replacement=NULL;for(Function *f=fns;f;f=f->next){count++;replacement=f;}
    if(count!=1||!replacement||strcmp(replacement->name,cli_function)||replacement->flags==1||fields||strstr(declarations.text,"\nuse ")||strstr(declarations.text,"\ndata "))fail("E_REPLACE","Replacement must contain exactly the selected function");
    if(old_size-end+start+n+1>cfg_source)fail("E_LIMIT","Replacement exceeds source limit");
    source=fmt("%.*s%s\n%s",(int)start,old,fragment,old+end);source_len=strlen(source);
    parse_module();verify();write_source();
}
