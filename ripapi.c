#include <stdio.h>
#include <stdlib.h>
#include <clang-c/Index.h>
#include <clang-c/CXCompilationDatabase.h>

static void
emit_generic(CXCursor c, const char* kind){
    const char* filenamestr;
    unsigned line, col;
    CXString s;
    CXFile src;
    CXString srcname;
    CXSourceLocation l = clang_getCursorLocation(c);
    clang_getSpellingLocation(l, &src, &line, &col, 0);
    srcname = clang_getFileName(src);
    s = clang_getCursorSpelling(c);
    filenamestr = clang_getCString(srcname);
    printf("%s|%s|%s|%d|%d\n",kind,clang_getCString(s), 
           filenamestr ? filenamestr : "" ,
           line, col);
    clang_disposeString(s);
    clang_disposeString(srcname);
}

static void 
emit_macrodef(CXCursor c){
    if(clang_Cursor_isMacroFunctionLike(c)){
        emit_generic(c, "mF");
    }else{
        emit_generic(c, "mN");
    }
}

/* Yuni type information */
enum yunivaluetype {
    YVT_VOID,             /* v */
    YVT_ALIAS,            /* a */ /* Type alias introduced with C typedef */
    YVT_SIGNED_INTEGER,   /* s */
    YVT_UNSIGNED_INTEGER, /* u */
    YVT_FLONUM,           /* f */
    YVT_BLOB              /* b */
};

struct yunitypeinfo_s {
    CXType basetype;
    enum yunivaluetype type;
/* FIXME: We don't support 2D or moreD array here */
#define YTI_ARRAY_LENGTH_NOTANARRAY -2
#define YTI_ARRAY_LENGTH_INCOMPLETE -1
    int array_length;
    int pointer_depth;
    int is_const;         /* c */
    int is_funcptr;
};

typedef struct yunitypeinfo_s yunitypeinfo_t;

#define YTI_TYPESTR_LEN 256
static void
calc_yunitypeinfo_str(yunitypeinfo_t* yti, char* out_str){
    CXString basename;
    char* typesym = "X";
    out_str[0] = 0;

    switch(yti->type){
        case YVT_VOID:
            typesym = "v";
            break;
        case YVT_ALIAS:
            typesym = "a";
            break;
        case YVT_SIGNED_INTEGER:
            typesym = "s";
            break;
        case YVT_UNSIGNED_INTEGER:
            typesym = "u";
            break;
        case YVT_FLONUM:
            typesym = "f";
            break;
        case YVT_BLOB:
            typesym = "b";
            break;
    }
    basename = clang_getTypeSpelling(yti->basetype);
    snprintf(out_str, YTI_TYPESTR_LEN,
             "%s|%s%s|%d|%d",
             (yti->is_funcptr ? "" : clang_getCString(basename)),
             yti->is_const ? "c" : "",
             typesym,
             yti->pointer_depth,
             yti->array_length);
    clang_disposeString(basename);
}


static void
strip_pointer_type(CXType t, CXType* out_t, int* out_depth){
    int depth = 0;
    while(t.kind == CXType_Pointer){
        t = clang_getPointeeType(t);
        depth++;
    }
    *out_t = t;
    *out_depth = depth;
}

static void
calc_yunitypeinfo(CXType t, yunitypeinfo_t* out_yti){

    CXType rtype;
    yunitypeinfo_t yti;

    /* Check for Array type first */
    switch(t.kind){
        case CXType_IncompleteArray:
            yti.array_length = YTI_ARRAY_LENGTH_INCOMPLETE;
            t = clang_getArrayElementType(t);
            break;
        case CXType_ConstantArray:
            yti.array_length = clang_getArraySize(t);
            t = clang_getArrayElementType(t);
        default:
            yti.array_length = YTI_ARRAY_LENGTH_NOTANARRAY;
            break;
    }


    strip_pointer_type(t, &yti.basetype, &yti.pointer_depth);
    rtype = clang_getResultType(yti.basetype);
    yti.is_funcptr = (rtype.kind == CXType_Invalid) ? 0 : 1;

    switch(yti.basetype.kind){
        case CXType_Elaborated: /* struct theStruct_s* etc */
        case CXType_Typedef:
            yti.type = YVT_ALIAS;
            break;
        case CXType_Unexposed:
        case CXType_Void:
            yti.type = YVT_VOID;
            break;
        //case CXType_Half:
        case CXType_LongDouble:
        case CXType_Double:
        case CXType_Float:
            yti.type = YVT_FLONUM;
            break;
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_UInt128:
        case CXType_Char_U:
            yti.type = YVT_UNSIGNED_INTEGER;
            break;
        default:
            printf("! UNKNOWN: %s\n", clang_getCString(clang_getTypeKindSpelling(yti.basetype.kind)));
            /* FALLTHROUGH */
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
        case CXType_Int128:
        case CXType_Char_S:
            yti.type = YVT_SIGNED_INTEGER;
            break;
    }

    yti.is_const = clang_isConstQualifiedType(yti.basetype) ? 1 : 0;

    *out_yti = yti;
};

static void emit_callback(CXType,CXCursor);
static void
emit_field(CXType t, CXCursor c){
    char typestr[YTI_TYPESTR_LEN];
    CXString nam;
    CXString id;
    yunitypeinfo_t yti;
    id = clang_getCursorSpelling(c);
    nam = clang_getTypeSpelling(t);
    calc_yunitypeinfo(t, &yti);
    calc_yunitypeinfo_str(&yti, typestr);
    printf("*|%s|%s|%s\n",clang_getCString(id),typestr,clang_getCString(nam));
    if(yti.is_funcptr){
        emit_callback(t,c);
    }
    clang_disposeString(nam);
    clang_disposeString(id);
}

static void
emit_callback(CXType t, CXCursor c){
    int i;
    int args_count;
    CXType base;
    CXType a;
    base = clang_getPointeeType(t);
    printf(">cb\n");
    if(clang_isFunctionTypeVariadic(t)){
        printf("fV\n");
    }else{
        printf("fN\n");
    }
    a = clang_getResultType(base);
    emit_field(a,c);
    i = 0;
    while(1){
        a = clang_getArgType(base, i);
        if(a.kind != CXType_Invalid){
            emit_field(a, clang_getNullCursor());
        }else{
            break;
        }
        i++;
    }
    printf("<cb\n");
}

static void
emit_enumentry(CXCursor c){
    CXString nam;
    nam = clang_getCursorSpelling(c);
    printf("*|%s\n",clang_getCString(nam));
    clang_disposeString(nam);
}

static enum CXChildVisitResult
visitor_composite(CXCursor c, CXCursor p, CXClientData bogus){
    (void) bogus;
    CXType t;
    enum CXCursorKind k;

    (void) bogus;
    k = clang_getCursorKind(c);
    t = clang_getCursorType(c);
    switch(k){
        case CXCursor_FieldDecl:
            emit_field(t, c);
            break;
        case CXCursor_EnumConstantDecl:
            emit_enumentry(c);
            break;
        default:
            break;
    }
    return CXChildVisit_Continue;
}

static void
emit_composite(CXCursor c, const char* kind){
    emit_generic(c, kind);
    (void) clang_visitChildren(c, visitor_composite, NULL);
}

static int
funcptr_p(CXType t){
    CXType base;
    CXType res;
    if(t.kind == CXType_Pointer){
        base = clang_getPointeeType(t);
        res = clang_getResultType(base);
        if(res.kind == CXType_Invalid){
            return 0;
        }else{
            return 1;
        }
    }
    return 0;
}

static void
emit_func(CXCursor c){
    CXType t;
    CXType arg;
    CXCursor a;
    int i;
    int args_count;
    args_count = clang_Cursor_getNumArguments(c);
    t = clang_getCursorType(c);
    if(clang_isFunctionTypeVariadic(t)){
        emit_generic(c, "fV");
    }else{
        emit_generic(c, "fN");
    }
    /* 0th field is the return type */
    arg = clang_getCursorResultType(c);
    emit_field(arg,c);

    /* Input arguments */
    for(i=0;i<args_count;i++){
        a = clang_Cursor_getArgument(c, i);
        arg = clang_getArgType(t, i);
        emit_field(arg, a);
        if(funcptr_p(arg)){
            /* FIXME: No nested callback types */
            emit_callback(arg, a);
        }
    }
}

static void
emit_typedef(CXCursor c){
    CXType t;
    emit_generic(c, "tD");
    t = clang_getTypedefDeclUnderlyingType(c);
    emit_field(t,c);
}

static enum CXChildVisitResult 
visitor_root(CXCursor c, CXCursor parent, CXClientData bogus){
    enum CXCursorKind k;

    (void) bogus;
    k = clang_getCursorKind(c);
    switch(k){
        /* Types */
        case CXCursor_EnumDecl:
            emit_composite(c, "eN");
            break;
        case CXCursor_StructDecl:
            emit_composite(c, "cS");
            break;
        case CXCursor_UnionDecl:
            emit_composite(c, "cU");
            break;
        case CXCursor_TypedefDecl:
            emit_typedef(c);
            break;
            /* APIs */
        case CXCursor_FunctionDecl:
            emit_func(c);
            break;
            /* Macro */
        case CXCursor_MacroDefinition:
            emit_macrodef(c);
            break;
        case CXCursor_MacroExpansion: /* = CXCursor_MacroInstantiation */
        default:
            /* Ignore this entry */
            break;
    }
    return CXChildVisit_Continue;
}

static void
proccmd(CXCompileCommand cmd){
    CXIndex idx;
    CXTranslationUnit tu;
    CXString* cx_argv;
    const char** argv;
    size_t argc;
    int i;

#ifdef HOSTINCDIR
    /* 
     * libclang sometimes does not properly configured and it will say:
     * "/usr/include/sys/cdefs.h:45:10: fatal error: 'stddef.h' file not found"
     * We'd have to manually add a include directory for it.
     */
    const char* hostincdir = HOSTINCDIR;
#else
    const char* hostincdir = NULL;
#endif

    const int argoffset = hostincdir ? 2 : 0;

    /* Instantiate Index */
    idx = clang_createIndex(0, 1);

    /* Collect options from CompileCommand */
    argc = clang_CompileCommand_getNumArgs(cmd);
    argv = calloc(argc, sizeof(char*));
    cx_argv = calloc(argc, sizeof(CXString*));
    for(i=1; i < argc; i++){
        cx_argv[i+argoffset] = clang_CompileCommand_getArg(cmd, i);
        argv[i+argoffset] = clang_getCString(cx_argv[i+argoffset]);
        //printf("ARG %d: %s\n",i+argoffset,argv[i+argoffset]);
    }

    argv[0] = "clang";

    if(hostincdir){
        argv[1] = "-I";
        argv[2] = hostincdir;
    }

    /* Instantiate TU */
    tu = clang_parseTranslationUnit(idx,
                                    NULL,
                                    argv,
                                    argc + argoffset,
                                    NULL,
                                    0,
                                    CXTranslationUnit_DetailedPreprocessingRecord|CXTranslationUnit_SkipFunctionBodies);

    if(!tu){
        printf("Error.\n");
        exit(1);
    }

    //printf("Done %p.\n", tu);

    (void)clang_visitChildren(clang_getTranslationUnitCursor(tu),
                              visitor_root,
                              NULL);

    for(i=1 /* argv0 is not used */; i < argc; i++){
        clang_disposeString(cx_argv[i+argoffset]);
    }
    clang_disposeIndex(idx);
}

int
main(int ac, char** av){
    CXCompilationDatabase_Error err;
    CXCompilationDatabase db;
    CXCompileCommands cmds;
    int cmds_count;
    CXIndex idx;
    int i;
    db = clang_CompilationDatabase_fromDirectory(".", &err);

    if(err != CXCompilationDatabase_NoError){
        printf("Database load error.\n");
        exit(1);
    }

    cmds = clang_CompilationDatabase_getAllCompileCommands(db);
    cmds_count = clang_CompileCommands_getSize(cmds);

    for(i=0; i < cmds_count; i++){
        proccmd(clang_CompileCommands_getCommand(cmds, i));
    }

    clang_CompilationDatabase_dispose(db);
    return 0;
}
