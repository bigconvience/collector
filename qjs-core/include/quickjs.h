//
// Created by benpeng.jiang on 2021/6/2.
//

#ifndef TUTORIAL_QUICKJS_H
#define TUTORIAL_QUICKJS_H

/* JS_Eval() flags */
#define JS_EVAL_TYPE_GLOBAL   (0 << 0) /* global code (default) */
#define JS_EVAL_TYPE_MODULE   (1 << 0) /* module code */
#define JS_EVAL_TYPE_DIRECT   (2 << 0) /* direct call (internal use) */
#define JS_EVAL_TYPE_INDIRECT (3 << 0) /* indirect call (internal use) */
#define JS_EVAL_TYPE_MASK     (3 << 0)



#endif //TUTORIAL_QUICKJS_H
