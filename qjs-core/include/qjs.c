//
// Created by benpeng.jiang on 2021/5/26.
//

#include "qjs-runtime.h"

int main_entry(int argc, char **argv) {
    int dump_memory = 1;

    JSRuntime *rt;
    JSContext *ctx;
    rt = JS_NewRuntime();

    ctx = JS_NewCustomContext(rt);
    if (dump_memory) {
        JSMemoryUsage stats;
        JS_ComputeMemoryUsage(rt, &stats);
        JS_DumpMemoryUsage(stdout, &stats, rt);
    }
}