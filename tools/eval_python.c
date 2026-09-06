#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "eval_common.h"
#include <stdio.h>
#include <string.h>

/* Only the candidate is Python. Loading, isolation orchestration, and all
 * observations are implemented in C; the parent imposes resource limits. */
int main(int argc, char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){printf("{\"implementation\":\"CPython\",\"version\":\"%s\"}\n",PY_VERSION);return 0;}
    if (argc != 5) die("Usage: lm0-eval-python check|execute SOURCE CASES TASK");
    char *source = read_text(argv[2]);
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.write_bytecode = 0;
    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) die("Cannot initialize CPython");
    PyObject *code = Py_CompileString(source, argv[2], Py_file_input);
    if (!code) {
        PyErr_Print();
        puts("{\"ok\":false,\"correct\":false,\"cases\":0}");
        return 2;
    }
    if (!strcmp(argv[1], "check")) {
        puts("{\"ok\":true}");
        Py_DECREF(code);
        Py_FinalizeEx();
        return 0;
    }
    int application = !strcmp(argv[1], "application");
    if (strcmp(argv[1], "execute") && !application) die("Unknown Python driver command");
    json_object *all = application ? NULL : read_json(argv[3]);
    json_object *cases = application ? NULL : member(all, argv[4], json_type_array);
    PyObject *globals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
    PyObject *name = PyUnicode_FromString(application ? "__main__" : "candidate");
    PyDict_SetItemString(globals, "__name__", name);
    Py_DECREF(name);
    PyObject *loaded = PyEval_EvalCode(code, globals, globals);
    if (application) {
        int result=0;
        if(!loaded) {
            if(PyErr_ExceptionMatches(PyExc_SystemExit)) {
                PyObject *type=NULL,*value=NULL,*traceback=NULL;
                PyErr_Fetch(&type,&value,&traceback);PyErr_NormalizeException(&type,&value,&traceback);
                PyObject *exitcode=PyObject_GetAttrString(value,"code");
                result=exitcode==Py_None?0:PyLong_Check(exitcode)?(int)PyLong_AsLong(exitcode):1;
                Py_XDECREF(exitcode);Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(traceback);
            } else {PyErr_Print();result=1;}
        }
        Py_XDECREF(loaded);Py_DECREF(globals);Py_DECREF(code);Py_FinalizeEx();return result;
    }
    PyObject *solve = loaded ? PyDict_GetItemString(globals, "solve") : NULL;
    int correct = solve && PyCallable_Check(solve);
    size_t count = json_object_array_length(cases), completed = 0;
    for (size_t c = 0; correct && c < count; c++) {
        json_object *row = json_object_array_get_idx(cases, c);
        json_object *values = member(row, "data", json_type_array);
        json_object *expected = member(row, "expected", json_type_array);
        size_t n = json_object_array_length(values);
        PyObject *data = PyList_New((Py_ssize_t)n);
        for (size_t i = 0; i < n; i++)
            PyList_SET_ITEM(data, (Py_ssize_t)i, PyLong_FromLongLong(json_object_get_int64(json_object_array_get_idx(values, i))));
        PyObject *key = PyLong_FromLongLong(integer_member(row, "key"));
        PyObject *answer = PyObject_CallFunctionObjArgs(solve, data, key, NULL);
        correct = answer && PyLong_Check(answer);
        if (correct) correct = PyLong_AsLongLong(answer) == json_object_get_int64(json_object_array_get_idx(expected, 0)) && !PyErr_Occurred();
        json_object *after = json_object_array_get_idx(expected, 1);
        correct &= PyList_Size(data) == (Py_ssize_t)json_object_array_length(after);
        for (size_t i = 0; correct && i < n; i++) {
            PyObject *value = PyList_GET_ITEM(data, (Py_ssize_t)i);
            correct = PyLong_Check(value) && PyLong_AsLongLong(value) == json_object_get_int64(json_object_array_get_idx(after, i)) && !PyErr_Occurred();
        }
        completed++;
        Py_XDECREF(answer); Py_DECREF(data); Py_DECREF(key);
    }
    if (PyErr_Occurred()) PyErr_Print();
    printf("{\"ok\":true,\"correct\":%s,\"cases\":%zu}\n", correct ? "true" : "false", completed);
    Py_XDECREF(loaded); Py_DECREF(globals); Py_DECREF(code);
    json_object_put(all);
    Py_FinalizeEx();
    return correct ? 0 : 1;
}
