//
// Created by benpeng.jiang on 2021/6/20.
//

#include "qjs-operation.h"

static BOOL is_safe_integer(double d)
{
    return isfinite(d) && floor(d) == d &&
           fabs(d) <= (double)MAX_SAFE_INTEGER;
}

 JSValue js_number_isSafeInteger(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    double d;
    if (!JS_IsNumber(argv[0]))
        return JS_FALSE;
    if (unlikely(JS_ToFloat64(ctx, &d, argv[0])))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, is_safe_integer(d));
}

static inline BOOL JS_IsHTMLDDA(JSContext *ctx, JSValueConst obj)
{
    JSObject *p;
    if (JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT)
        return FALSE;
    p = JS_VALUE_GET_OBJ(obj);
    return p->is_HTMLDDA;
}

inline int JS_ToUint32Free(JSContext *ctx, uint32_t *pres, JSValue val)
{
    return JS_ToInt32Free(ctx, (int32_t *)pres, val);
}

 double js_pow(double a, double b)
{
    if (unlikely(!isfinite(b)) && fabs(a) == 1) {
        /* not compatible with IEEE 754 */
        return JS_FLOAT64_NAN;
    } else {
        return pow(a, b);
    }
}

#ifdef CONFIG_BIGNUM

JSValue JS_NewBigInt64_1(JSContext *ctx, int64_t v)
{
    JSValue val;
    bf_t *a;
    val = JS_NewBigInt(ctx);
    if (JS_IsException(val))
        return val;
    a = JS_GetBigInt(val);
    if (bf_set_si(a, v)) {
        JS_FreeValue(ctx, val);
        return JS_ThrowOutOfMemory(ctx);
    }
    return val;
}

JSValue JS_NewBigInt64(JSContext *ctx, int64_t v)
{
    if (is_math_mode(ctx) &&
        v >= -MAX_SAFE_INTEGER && v <= MAX_SAFE_INTEGER) {
        return JS_NewInt64(ctx, v);
    } else {
        return JS_NewBigInt64_1(ctx, v);
    }
}

JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v)
{
    JSValue val;
    if (is_math_mode(ctx) && v <= MAX_SAFE_INTEGER) {
        val = JS_NewInt64(ctx, v);
    } else {
        bf_t *a;
        val = JS_NewBigInt(ctx);
        if (JS_IsException(val))
            return val;
        a = JS_GetBigInt(val);
        if (bf_set_ui(a, v)) {
            JS_FreeValue(ctx, val);
            return JS_ThrowOutOfMemory(ctx);
        }
    }
    return val;
}

/* if the returned bigfloat is allocated it is equal to
   'buf'. Otherwise it is a pointer to the bigfloat in 'val'. Return
   NULL in case of error. */
 bf_t *JS_ToBigFloat(JSContext *ctx, bf_t *buf, JSValueConst val)
{
    uint32_t tag;
    bf_t *r;
    JSBigFloat *p;

    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_BOOL:
    case JS_TAG_NULL:
        r = buf;
        bf_init(ctx->bf_ctx, r);
        if (bf_set_si(r, JS_VALUE_GET_INT(val)))
            goto fail;
        break;
    case JS_TAG_FLOAT64:
        r = buf;
        bf_init(ctx->bf_ctx, r);
        if (bf_set_float64(r, JS_VALUE_GET_FLOAT64(val))) {
        fail:
            bf_delete(r);
            return NULL;
        }
        break;
    case JS_TAG_BIG_INT:
    case JS_TAG_BIG_FLOAT:
        p = JS_VALUE_GET_PTR(val);
        r = &p->num;
        break;
    case JS_TAG_UNDEFINED:
    default:
        r = buf;
        bf_init(ctx->bf_ctx, r);
        bf_set_nan(r);
        break;
    }
    return r;
}

/* return NULL if invalid type */
 bfdec_t *JS_ToBigDecimal(JSContext *ctx, JSValueConst val)
{
    uint32_t tag;
    JSBigDecimal *p;
    bfdec_t *r;

    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_BIG_DECIMAL:
        p = JS_VALUE_GET_PTR(val);
        r = &p->num;
        break;
    default:
        JS_ThrowTypeError(ctx, "bigdecimal expected");
        r = NULL;
        break;
    }
    return r;
}

/* return NaN if bad bigint literal */
static JSValue JS_StringToBigInt(JSContext *ctx, JSValue val)
{
    const char *str, *p;
    size_t len;
    int flags;

    str = JS_ToCStringLen(ctx, &len, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return JS_EXCEPTION;
    p = str;
    p += skip_spaces(p);
    if ((p - str) == len) {
        val = JS_NewBigInt64(ctx, 0);
    } else {
        flags = ATOD_INT_ONLY | ATOD_ACCEPT_BIN_OCT | ATOD_TYPE_BIG_INT;
        if (is_math_mode(ctx))
            flags |= ATOD_MODE_BIGINT;
        val = js_atof(ctx, p, &p, 0, flags);
        p += skip_spaces(p);
        if (!JS_IsException(val)) {
            if ((p - str) != len) {
                JS_FreeValue(ctx, val);
                val = JS_NAN;
            }
        }
    }
    JS_FreeCString(ctx, str);
    return val;
}

 JSValue JS_StringToBigIntErr(JSContext *ctx, JSValue val)
{
    val = JS_StringToBigInt(ctx, val);
    if (JS_VALUE_IS_NAN(val))
        return JS_ThrowSyntaxError(ctx, "invalid bigint literal");
    return val;
}

/* if the returned bigfloat is allocated it is equal to
   'buf'. Otherwise it is a pointer to the bigfloat in 'val'. */
 bf_t *JS_ToBigIntFree(JSContext *ctx, bf_t *buf, JSValue val)
{
    uint32_t tag;
    bf_t *r;
    JSBigFloat *p;

 redo:
    tag = JS_VALUE_GET_NORM_TAG(val);
    switch(tag) {
    case JS_TAG_INT:
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
        if (!is_math_mode(ctx))
            goto fail;
        /* fall tru */
    case JS_TAG_BOOL:
        r = buf;
        bf_init(ctx->bf_ctx, r);
        bf_set_si(r, JS_VALUE_GET_INT(val));
        break;
    case JS_TAG_FLOAT64:
        {
            double d = JS_VALUE_GET_FLOAT64(val);
            if (!is_math_mode(ctx))
                goto fail;
            if (!isfinite(d))
                goto fail;
            r = buf;
            bf_init(ctx->bf_ctx, r);
            d = trunc(d);
            bf_set_float64(r, d);
        }
        break;
    case JS_TAG_BIG_INT:
        p = JS_VALUE_GET_PTR(val);
        r = &p->num;
        break;
    case JS_TAG_BIG_FLOAT:
        if (!is_math_mode(ctx))
            goto fail;
        p = JS_VALUE_GET_PTR(val);
        if (!bf_is_finite(&p->num))
            goto fail;
        r = buf;
        bf_init(ctx->bf_ctx, r);
        bf_set(r, &p->num);
        bf_rint(r, BF_RNDZ);
        JS_FreeValue(ctx, val);
        break;
    case JS_TAG_STRING:
        val = JS_StringToBigIntErr(ctx, val);
        if (JS_IsException(val))
            return NULL;
        goto redo;
    case JS_TAG_OBJECT:
        val = JS_ToPrimitiveFree(ctx, val, HINT_NUMBER);
        if (JS_IsException(val))
            return NULL;
        goto redo;
    default:
    fail:
        JS_FreeValue(ctx, val);
        JS_ThrowTypeError(ctx, "cannot convert to bigint");
        return NULL;
    }
    return r;
}

 bf_t *JS_ToBigInt(JSContext *ctx, bf_t *buf, JSValueConst val)
{
    return JS_ToBigIntFree(ctx, buf, JS_DupValue(ctx, val));
}

 __maybe_unused JSValue JS_ToBigIntValueFree(JSContext *ctx, JSValue val)
{
    if (JS_VALUE_GET_TAG(val) == JS_TAG_BIG_INT) {
        return val;
    } else {
        bf_t a_s, *a, *r;
        int ret;
        JSValue res;

        res = JS_NewBigInt(ctx);
        if (JS_IsException(res))
            return JS_EXCEPTION;
        a = JS_ToBigIntFree(ctx, &a_s, val);
        if (!a) {
            JS_FreeValue(ctx, res);
            return JS_EXCEPTION;
        }
        r = JS_GetBigInt(res);
        ret = bf_set(r, a);
        JS_FreeBigInt(ctx, a, &a_s);
        if (ret) {
            JS_FreeValue(ctx, res);
            return JS_ThrowOutOfMemory(ctx);
        }
        return JS_CompactBigInt(ctx, res);
    }
}

/* free the bf_t allocated by JS_ToBigInt */
 void JS_FreeBigInt(JSContext *ctx, bf_t *a, bf_t *buf)
{
    if (a == buf) {
        bf_delete(a);
    } else {
        JSBigFloat *p = (JSBigFloat *)((uint8_t *)a -
                                       offsetof(JSBigFloat, num));
        JS_FreeValue(ctx, JS_MKPTR(JS_TAG_BIG_FLOAT, p));
    }
}

/* XXX: merge with JS_ToInt64Free with a specific flag */
 int JS_ToBigInt64Free(JSContext *ctx, int64_t *pres, JSValue val)
{
    bf_t a_s, *a;

    a = JS_ToBigIntFree(ctx, &a_s, val);
    if (!a) {
        *pres = 0;
        return -1;
    }
    bf_get_int64(pres, a, BF_GET_INT_MOD);
    JS_FreeBigInt(ctx, a, &a_s);
    return 0;
}

int JS_ToBigInt64(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    return JS_ToBigInt64Free(ctx, pres, JS_DupValue(ctx, val));
}

 JSBigFloat *js_new_bf(JSContext *ctx)
{
    JSBigFloat *p;
    p = js_malloc(ctx, sizeof(*p));
    if (!p)
        return NULL;
    p->header.ref_count = 1;
    bf_init(ctx->bf_ctx, &p->num);
    return p;
}

 JSValue JS_NewBigFloat(JSContext *ctx)
{
    JSBigFloat *p;
    p = js_malloc(ctx, sizeof(*p));
    if (!p)
        return JS_EXCEPTION;
    p->header.ref_count = 1;
    bf_init(ctx->bf_ctx, &p->num);
    return JS_MKPTR(JS_TAG_BIG_FLOAT, p);
}

 JSValue JS_NewBigDecimal(JSContext *ctx)
{
    JSBigDecimal *p;
    p = js_malloc(ctx, sizeof(*p));
    if (!p)
        return JS_EXCEPTION;
    p->header.ref_count = 1;
    bfdec_init(ctx->bf_ctx, &p->num);
    return JS_MKPTR(JS_TAG_BIG_DECIMAL, p);
}

 JSValue JS_NewBigInt(JSContext *ctx)
{
    JSBigFloat *p;
    p = js_malloc(ctx, sizeof(*p));
    if (!p)
        return JS_EXCEPTION;
    p->header.ref_count = 1;
    bf_init(ctx->bf_ctx, &p->num);
    return JS_MKPTR(JS_TAG_BIG_INT, p);
}

 JSValue JS_CompactBigInt1(JSContext *ctx, JSValue val,
                                 BOOL convert_to_safe_integer)
{
    int64_t v;
    bf_t *a;

    if (JS_VALUE_GET_TAG(val) != JS_TAG_BIG_INT)
        return val; /* fail safe */
    a = JS_GetBigInt(val);
    if (convert_to_safe_integer && bf_get_int64(&v, a, 0) == 0 &&
        v >= -MAX_SAFE_INTEGER && v <= MAX_SAFE_INTEGER) {
        JS_FreeValue(ctx, val);
        return JS_NewInt64(ctx, v);
    } else if (a->expn == BF_EXP_ZERO && a->sign) {
        JSBigFloat *p = JS_VALUE_GET_PTR(val);
        assert(p->header.ref_count == 1);
        a->sign = 0;
    }
    return val;
}

/* Convert the big int to a safe integer if in math mode. normalize
   the zero representation. Could also be used to convert the bigint
   to a short bigint value. The reference count of the value must be
   1. Cannot fail */
 JSValue JS_CompactBigInt(JSContext *ctx, JSValue val)
{
    return JS_CompactBigInt1(ctx, val, is_math_mode(ctx));
}


static int get_ovop_from_opcode(OPCodeEnum op)
{
    switch(op) {
    case OP_add:
        return JS_OVOP_ADD;
    case OP_sub:
        return JS_OVOP_SUB;
    case OP_mul:
        return JS_OVOP_MUL;
    case OP_div:
        return JS_OVOP_DIV;
    case OP_mod:
    case OP_math_mod:
        return JS_OVOP_MOD;
    case OP_pow:
        return JS_OVOP_POW;
    case OP_or:
        return JS_OVOP_OR;
    case OP_and:
        return JS_OVOP_AND;
    case OP_xor:
        return JS_OVOP_XOR;
    case OP_shl:
        return JS_OVOP_SHL;
    case OP_sar:
        return JS_OVOP_SAR;
    case OP_shr:
        return JS_OVOP_SHR;
    case OP_eq:
    case OP_neq:
        return JS_OVOP_EQ;
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
        return JS_OVOP_LESS;
    case OP_plus:
        return JS_OVOP_POS;
    case OP_neg:
        return JS_OVOP_NEG;
    case OP_inc:
        return JS_OVOP_INC;
    case OP_dec:
        return JS_OVOP_DEC;
    default:
        abort();
    }
}

/* return NULL if not present */
static JSObject *find_binary_op(JSBinaryOperatorDef *def,
                                uint32_t operator_index,
                                JSOverloadableOperatorEnum op)
{
    JSBinaryOperatorDefEntry *ent;
    int i;
    for(i = 0; i < def->count; i++) {
        ent = &def->tab[i];
        if (ent->operator_index == operator_index)
            return ent->ops[op];
    }
    return NULL;
}

/* return -1 if exception, 0 if no operator overloading, 1 if
   overloaded operator called */
static __exception int js_call_binary_op_fallback(JSContext *ctx,
                                                  JSValue *pret,
                                                  JSValueConst op1,
                                                  JSValueConst op2,
                                                  OPCodeEnum op,
                                                  BOOL is_numeric,
                                                  int hint)
{
    JSValue opset1_obj, opset2_obj, method, ret, new_op1, new_op2;
    JSOperatorSetData *opset1, *opset2;
    JSOverloadableOperatorEnum ovop;
    JSObject *p;
    JSValueConst args[2];

    if (!ctx->allow_operator_overloading)
        return 0;

    opset2_obj = JS_UNDEFINED;
    opset1_obj = JS_GetProperty(ctx, op1, JS_ATOM_Symbol_operatorSet);
    if (JS_IsException(opset1_obj))
        goto exception;
    if (JS_IsUndefined(opset1_obj))
        return 0;
    opset1 = JS_GetOpaque2(ctx, opset1_obj, JS_CLASS_OPERATOR_SET);
    if (!opset1)
        goto exception;

    opset2_obj = JS_GetProperty(ctx, op2, JS_ATOM_Symbol_operatorSet);
    if (JS_IsException(opset2_obj))
        goto exception;
    if (JS_IsUndefined(opset2_obj)) {
        JS_FreeValue(ctx, opset1_obj);
        return 0;
    }
    opset2 = JS_GetOpaque2(ctx, opset2_obj, JS_CLASS_OPERATOR_SET);
    if (!opset2)
        goto exception;

    if (opset1->is_primitive && opset2->is_primitive) {
        JS_FreeValue(ctx, opset1_obj);
        JS_FreeValue(ctx, opset2_obj);
        return 0;
    }

    ovop = get_ovop_from_opcode(op);

    if (opset1->operator_counter == opset2->operator_counter) {
        p = opset1->self_ops[ovop];
    } else if (opset1->operator_counter > opset2->operator_counter) {
        p = find_binary_op(&opset1->left, opset2->operator_counter, ovop);
    } else {
        p = find_binary_op(&opset2->right, opset1->operator_counter, ovop);
    }
    if (!p) {
        JS_ThrowTypeError(ctx, "operator %s: no function defined",
                          js_overloadable_operator_names[ovop]);
        goto exception;
    }

    if (opset1->is_primitive) {
        if (is_numeric) {
            new_op1 = JS_ToNumeric(ctx, op1);
        } else {
            new_op1 = JS_ToPrimitive(ctx, op1, hint);
        }
        if (JS_IsException(new_op1))
            goto exception;
    } else {
        new_op1 = JS_DupValue(ctx, op1);
    }

    if (opset2->is_primitive) {
        if (is_numeric) {
            new_op2 = JS_ToNumeric(ctx, op2);
        } else {
            new_op2 = JS_ToPrimitive(ctx, op2, hint);
        }
        if (JS_IsException(new_op2)) {
            JS_FreeValue(ctx, new_op1);
            goto exception;
        }
    } else {
        new_op2 = JS_DupValue(ctx, op2);
    }

    /* XXX: could apply JS_ToPrimitive() if primitive type so that the
       operator function does not get a value object */

    method = JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
    if (ovop == JS_OVOP_LESS && (op == OP_lte || op == OP_gt)) {
        args[0] = new_op2;
        args[1] = new_op1;
    } else {
        args[0] = new_op1;
        args[1] = new_op2;
    }
    ret = JS_CallFree(ctx, method, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, new_op1);
    JS_FreeValue(ctx, new_op2);
    if (JS_IsException(ret))
        goto exception;
    if (ovop == JS_OVOP_EQ) {
        BOOL res = JS_ToBoolFree(ctx, ret);
        if (op == OP_neq)
            res ^= 1;
        ret = JS_NewBool(ctx, res);
    } else if (ovop == JS_OVOP_LESS) {
        if (JS_IsUndefined(ret)) {
            ret = JS_FALSE;
        } else {
            BOOL res = JS_ToBoolFree(ctx, ret);
            if (op == OP_lte || op == OP_gte)
                res ^= 1;
            ret = JS_NewBool(ctx, res);
        }
    }
    JS_FreeValue(ctx, opset1_obj);
    JS_FreeValue(ctx, opset2_obj);
    *pret = ret;
    return 1;
 exception:
    JS_FreeValue(ctx, opset1_obj);
    JS_FreeValue(ctx, opset2_obj);
    *pret = JS_UNDEFINED;
    return -1;
}

/* try to call the operation on the operatorSet field of 'obj'. Only
   used for "/" and "**" on the BigInt prototype in math mode */
static __exception int js_call_binary_op_simple(JSContext *ctx,
                                                JSValue *pret,
                                                JSValueConst obj,
                                                JSValueConst op1,
                                                JSValueConst op2,
                                                OPCodeEnum op)
{
    JSValue opset1_obj, method, ret, new_op1, new_op2;
    JSOperatorSetData *opset1;
    JSOverloadableOperatorEnum ovop;
    JSObject *p;
    JSValueConst args[2];

    opset1_obj = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_operatorSet);
    if (JS_IsException(opset1_obj))
        goto exception;
    if (JS_IsUndefined(opset1_obj))
        return 0;
    opset1 = JS_GetOpaque2(ctx, opset1_obj, JS_CLASS_OPERATOR_SET);
    if (!opset1)
        goto exception;
    ovop = get_ovop_from_opcode(op);

    p = opset1->self_ops[ovop];
    if (!p) {
        JS_FreeValue(ctx, opset1_obj);
        return 0;
    }

    new_op1 = JS_ToNumeric(ctx, op1);
    if (JS_IsException(new_op1))
        goto exception;
    new_op2 = JS_ToNumeric(ctx, op2);
    if (JS_IsException(new_op2)) {
        JS_FreeValue(ctx, new_op1);
        goto exception;
    }

    method = JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
    args[0] = new_op1;
    args[1] = new_op2;
    ret = JS_CallFree(ctx, method, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, new_op1);
    JS_FreeValue(ctx, new_op2);
    if (JS_IsException(ret))
        goto exception;
    JS_FreeValue(ctx, opset1_obj);
    *pret = ret;
    return 1;
 exception:
    JS_FreeValue(ctx, opset1_obj);
    *pret = JS_UNDEFINED;
    return -1;
}

/* return -1 if exception, 0 if no operator overloading, 1 if
   overloaded operator called */
static __exception int js_call_unary_op_fallback(JSContext *ctx,
                                                 JSValue *pret,
                                                 JSValueConst op1,
                                                 OPCodeEnum op)
{
    JSValue opset1_obj, method, ret;
    JSOperatorSetData *opset1;
    JSOverloadableOperatorEnum ovop;
    JSObject *p;

    if (!ctx->allow_operator_overloading)
        return 0;

    opset1_obj = JS_GetProperty(ctx, op1, JS_ATOM_Symbol_operatorSet);
    if (JS_IsException(opset1_obj))
        goto exception;
    if (JS_IsUndefined(opset1_obj))
        return 0;
    opset1 = JS_GetOpaque2(ctx, opset1_obj, JS_CLASS_OPERATOR_SET);
    if (!opset1)
        goto exception;
    if (opset1->is_primitive) {
        JS_FreeValue(ctx, opset1_obj);
        return 0;
    }

    ovop = get_ovop_from_opcode(op);

    p = opset1->self_ops[ovop];
    if (!p) {
        JS_ThrowTypeError(ctx, "no overloaded operator %s",
                          js_overloadable_operator_names[ovop]);
        goto exception;
    }
    method = JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, p));
    ret = JS_CallFree(ctx, method, JS_UNDEFINED, 1, &op1);
    if (JS_IsException(ret))
        goto exception;
    JS_FreeValue(ctx, opset1_obj);
    *pret = ret;
    return 1;
 exception:
    JS_FreeValue(ctx, opset1_obj);
    *pret = JS_UNDEFINED;
    return -1;
}

 JSValue throw_bf_exception(JSContext *ctx, int status)
{
    const char *str;
    if (status & BF_ST_MEM_ERROR)
        return JS_ThrowOutOfMemory(ctx);
    if (status & BF_ST_DIVIDE_ZERO) {
        str = "division by zero";
    } else if (status & BF_ST_INVALID_OP) {
        str = "invalid operation";
    } else {
        str = "integer overflow";
    }
    return JS_ThrowRangeError(ctx, "%s", str);
}

 int js_unary_arith_bigint(JSContext *ctx,
                                 JSValue *pres, OPCodeEnum op, JSValue op1)
{
    bf_t a_s, *r, *a;
    int ret, v;
    JSValue res;

    if (op == OP_plus && !is_math_mode(ctx)) {
        JS_ThrowTypeError(ctx, "bigint argument with unary +");
        JS_FreeValue(ctx, op1);
        return -1;
    }
    res = JS_NewBigInt(ctx);
    if (JS_IsException(res)) {
        JS_FreeValue(ctx, op1);
        return -1;
    }
    r = JS_GetBigInt(res);
    a = JS_ToBigInt(ctx, &a_s, op1);
    ret = 0;
    switch(op) {
    case OP_inc:
    case OP_dec:
        v = 2 * (op - OP_dec) - 1;
        ret = bf_add_si(r, a, v, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_plus:
        ret = bf_set(r, a);
        break;
    case OP_neg:
        ret = bf_set(r, a);
        bf_neg(r);
        break;
    case OP_not:
        ret = bf_add_si(r, a, 1, BF_PREC_INF, BF_RNDZ);
        bf_neg(r);
        break;
    default:
        abort();
    }
    JS_FreeBigInt(ctx, a, &a_s);
    JS_FreeValue(ctx, op1);
    if (unlikely(ret)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    res = JS_CompactBigInt(ctx, res);
    *pres = res;
    return 0;
}

 int js_unary_arith_bigfloat(JSContext *ctx,
                                   JSValue *pres, OPCodeEnum op, JSValue op1)
{
    bf_t a_s, *r, *a;
    int ret, v;
    JSValue res;

    if (op == OP_plus && !is_math_mode(ctx)) {
        JS_ThrowTypeError(ctx, "bigfloat argument with unary +");
        JS_FreeValue(ctx, op1);
        return -1;
    }

    res = JS_NewBigFloat(ctx);
    if (JS_IsException(res)) {
        JS_FreeValue(ctx, op1);
        return -1;
    }
    r = JS_GetBigFloat(res);
    a = JS_ToBigFloat(ctx, &a_s, op1);
    ret = 0;
    switch(op) {
    case OP_inc:
    case OP_dec:
        v = 2 * (op - OP_dec) - 1;
        ret = bf_add_si(r, a, v, ctx->fp_env.prec, ctx->fp_env.flags);
        break;
    case OP_plus:
        ret = bf_set(r, a);
        break;
    case OP_neg:
        ret = bf_set(r, a);
        bf_neg(r);
        break;
    default:
        abort();
    }
    if (a == &a_s)
        bf_delete(a);
    JS_FreeValue(ctx, op1);
    if (unlikely(ret & BF_ST_MEM_ERROR)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    *pres = res;
    return 0;
}

 int js_unary_arith_bigdecimal(JSContext *ctx,
                                     JSValue *pres, OPCodeEnum op, JSValue op1)
{
    bfdec_t *r, *a;
    int ret, v;
    JSValue res;

    if (op == OP_plus && !is_math_mode(ctx)) {
        JS_ThrowTypeError(ctx, "bigdecimal argument with unary +");
        JS_FreeValue(ctx, op1);
        return -1;
    }

    res = JS_NewBigDecimal(ctx);
    if (JS_IsException(res)) {
        JS_FreeValue(ctx, op1);
        return -1;
    }
    r = JS_GetBigDecimal(res);
    a = JS_ToBigDecimal(ctx, op1);
    ret = 0;
    switch(op) {
    case OP_inc:
    case OP_dec:
        v = 2 * (op - OP_dec) - 1;
        ret = bfdec_add_si(r, a, v, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_plus:
        ret = bfdec_set(r, a);
        break;
    case OP_neg:
        ret = bfdec_set(r, a);
        bfdec_neg(r);
        break;
    default:
        abort();
    }
    JS_FreeValue(ctx, op1);
    if (unlikely(ret)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    *pres = res;
    return 0;
}

 no_inline __exception int js_unary_arith_slow(JSContext *ctx,
                                                     JSValue *sp,
                                                     OPCodeEnum op)
{
    JSValue op1, val;
    int v, ret;
    uint32_t tag;

    op1 = sp[-1];
    /* fast path for float64 */
    if (JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(op1)))
        goto handle_float64;
    if (JS_IsObject(op1)) {
        ret = js_call_unary_op_fallback(ctx, &val, op1, op);
        if (ret < 0)
            return -1;
        if (ret) {
            JS_FreeValue(ctx, op1);
            sp[-1] = val;
            return 0;
        }
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1))
        goto exception;
    tag = JS_VALUE_GET_TAG(op1);
    switch(tag) {
    case JS_TAG_INT:
        {
            int64_t v64;
            v64 = JS_VALUE_GET_INT(op1);
            switch(op) {
            case OP_inc:
            case OP_dec:
                v = 2 * (op - OP_dec) - 1;
                v64 += v;
                break;
            case OP_plus:
                break;
            case OP_neg:
                if (v64 == 0) {
                    sp[-1] = __JS_NewFloat64(ctx, -0.0);
                    return 0;
                } else {
                    v64 = -v64;
                }
                break;
            default:
                abort();
            }
            sp[-1] = JS_NewInt64(ctx, v64);
        }
        break;
    case JS_TAG_BIG_INT:
    handle_bigint:
        if (ctx->rt->bigint_ops.unary_arith(ctx, sp - 1, op, op1))
            goto exception;
        break;
    case JS_TAG_BIG_FLOAT:
        if (ctx->rt->bigfloat_ops.unary_arith(ctx, sp - 1, op, op1))
            goto exception;
        break;
    case JS_TAG_BIG_DECIMAL:
        if (ctx->rt->bigdecimal_ops.unary_arith(ctx, sp - 1, op, op1))
            goto exception;
        break;
    default:
    handle_float64:
        {
            double d;
            if (is_math_mode(ctx))
                goto handle_bigint;
            d = JS_VALUE_GET_FLOAT64(op1);
            switch(op) {
            case OP_inc:
            case OP_dec:
                v = 2 * (op - OP_dec) - 1;
                d += v;
                break;
            case OP_plus:
                break;
            case OP_neg:
                d = -d;
                break;
            default:
                abort();
            }
            sp[-1] = __JS_NewFloat64(ctx, d);
        }
        break;
    }
    return 0;
 exception:
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 __exception int js_post_inc_slow(JSContext *ctx,
                                        JSValue *sp, OPCodeEnum op)
{
    JSValue op1;

    /* XXX: allow custom operators */
    op1 = sp[-1];
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        sp[-1] = JS_UNDEFINED;
        return -1;
    }
    sp[-1] = op1;
    sp[0] = JS_DupValue(ctx, op1);
    return js_unary_arith_slow(ctx, sp + 1, op - OP_post_dec + OP_dec);
}

 no_inline int js_not_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, val;
    int ret;

    op1 = sp[-1];
    if (JS_IsObject(op1)) {
        ret = js_call_unary_op_fallback(ctx, &val, op1, OP_not);
        if (ret < 0)
            return -1;
        if (ret) {
            JS_FreeValue(ctx, op1);
            sp[-1] = val;
            return 0;
        }
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1))
        goto exception;
    if (is_math_mode(ctx) || JS_VALUE_GET_TAG(op1) == JS_TAG_BIG_INT) {
        if (ctx->rt->bigint_ops.unary_arith(ctx, sp - 1, OP_not, op1))
            goto exception;
    } else {
        int32_t v1;
        if (unlikely(JS_ToInt32Free(ctx, &v1, op1)))
            goto exception;
        sp[-1] = JS_NewInt32(ctx, ~v1);
    }
    return 0;
 exception:
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 int js_binary_arith_bigfloat(JSContext *ctx, OPCodeEnum op,
                                    JSValue *pres, JSValue op1, JSValue op2)
{
    bf_t a_s, b_s, *r, *a, *b;
    int ret;
    JSValue res;

    res = JS_NewBigFloat(ctx);
    if (JS_IsException(res)) {
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        return -1;
    }
    r = JS_GetBigFloat(res);
    a = JS_ToBigFloat(ctx, &a_s, op1);
    b = JS_ToBigFloat(ctx, &b_s, op2);
    bf_init(ctx->bf_ctx, r);
    switch(op) {
    case OP_add:
        ret = bf_add(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags);
        break;
    case OP_sub:
        ret = bf_sub(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags);
        break;
    case OP_mul:
        ret = bf_mul(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags);
        break;
    case OP_div:
        ret = bf_div(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags);
        break;
    case OP_math_mod:
        /* Euclidian remainder */
        ret = bf_rem(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags,
                     BF_DIVREM_EUCLIDIAN);
        break;
    case OP_mod:
        ret = bf_rem(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags,
                     BF_RNDZ);
        break;
    case OP_pow:
        ret = bf_pow(r, a, b, ctx->fp_env.prec,
                     ctx->fp_env.flags | BF_POW_JS_QUIRKS);
        break;
    default:
        abort();
    }
    if (a == &a_s)
        bf_delete(a);
    if (b == &b_s)
        bf_delete(b);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    if (unlikely(ret & BF_ST_MEM_ERROR)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    *pres = res;
    return 0;
}

 int js_binary_arith_bigint(JSContext *ctx, OPCodeEnum op,
                                  JSValue *pres, JSValue op1, JSValue op2)
{
    bf_t a_s, b_s, *r, *a, *b;
    int ret;
    JSValue res;

    res = JS_NewBigInt(ctx);
    if (JS_IsException(res))
        goto fail;
    a = JS_ToBigInt(ctx, &a_s, op1);
    if (!a)
        goto fail;
    b = JS_ToBigInt(ctx, &b_s, op2);
    if (!b) {
        JS_FreeBigInt(ctx, a, &a_s);
        goto fail;
    }
    r = JS_GetBigInt(res);
    ret = 0;
    switch(op) {
    case OP_add:
        ret = bf_add(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_sub:
        ret = bf_sub(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_mul:
        ret = bf_mul(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_div:
        if (!is_math_mode(ctx)) {
            bf_t rem_s, *rem = &rem_s;
            bf_init(ctx->bf_ctx, rem);
            ret = bf_divrem(r, rem, a, b, BF_PREC_INF, BF_RNDZ,
                            BF_RNDZ);
            bf_delete(rem);
        } else {
            goto math_mode_div_pow;
        }
        break;
    case OP_math_mod:
        /* Euclidian remainder */
        ret = bf_rem(r, a, b, BF_PREC_INF, BF_RNDZ,
                     BF_DIVREM_EUCLIDIAN) & BF_ST_INVALID_OP;
        break;
    case OP_mod:
        ret = bf_rem(r, a, b, BF_PREC_INF, BF_RNDZ,
                     BF_RNDZ) & BF_ST_INVALID_OP;
        break;
    case OP_pow:
        if (b->sign) {
            if (!is_math_mode(ctx)) {
                ret = BF_ST_INVALID_OP;
            } else {
            math_mode_div_pow:
                JS_FreeValue(ctx, res);
                ret = js_call_binary_op_simple(ctx, &res, ctx->class_proto[JS_CLASS_BIG_INT], op1, op2, op);
                if (ret != 0) {
                    JS_FreeBigInt(ctx, a, &a_s);
                    JS_FreeBigInt(ctx, b, &b_s);
                    JS_FreeValue(ctx, op1);
                    JS_FreeValue(ctx, op2);
                    if (ret < 0) {
                        return -1;
                    } else {
                        *pres = res;
                        return 0;
                    }
                }
                /* if no BigInt power operator defined, return a
                   bigfloat */
                res = JS_NewBigFloat(ctx);
                if (JS_IsException(res)) {
                    JS_FreeBigInt(ctx, a, &a_s);
                    JS_FreeBigInt(ctx, b, &b_s);
                    goto fail;
                }
                r = JS_GetBigFloat(res);
                if (op == OP_div) {
                    ret = bf_div(r, a, b, ctx->fp_env.prec, ctx->fp_env.flags) & BF_ST_MEM_ERROR;
                } else {
                    ret = bf_pow(r, a, b, ctx->fp_env.prec,
                                 ctx->fp_env.flags | BF_POW_JS_QUIRKS) & BF_ST_MEM_ERROR;
                }
                JS_FreeBigInt(ctx, a, &a_s);
                JS_FreeBigInt(ctx, b, &b_s);
                JS_FreeValue(ctx, op1);
                JS_FreeValue(ctx, op2);
                if (unlikely(ret)) {
                    JS_FreeValue(ctx, res);
                    throw_bf_exception(ctx, ret);
                    return -1;
                }
                *pres = res;
                return 0;
            }
        } else {
            ret = bf_pow(r, a, b, BF_PREC_INF, BF_RNDZ | BF_POW_JS_QUIRKS);
        }
        break;

        /* logical operations */
    case OP_shl:
    case OP_sar:
        {
            slimb_t v2;
#if LIMB_BITS == 32
            bf_get_int32(&v2, b, 0);
            if (v2 == INT32_MIN)
                v2 = INT32_MIN + 1;
#else
            bf_get_int64(&v2, b, 0);
            if (v2 == INT64_MIN)
                v2 = INT64_MIN + 1;
#endif
            if (op == OP_sar)
                v2 = -v2;
            ret = bf_set(r, a);
            ret |= bf_mul_2exp(r, v2, BF_PREC_INF, BF_RNDZ);
            if (v2 < 0) {
                ret |= bf_rint(r, BF_RNDD) & (BF_ST_OVERFLOW | BF_ST_MEM_ERROR);
            }
        }
        break;
    case OP_and:
        ret = bf_logic_and(r, a, b);
        break;
    case OP_or:
        ret = bf_logic_or(r, a, b);
        break;
    case OP_xor:
        ret = bf_logic_xor(r, a, b);
        break;
    default:
        abort();
    }
    JS_FreeBigInt(ctx, a, &a_s);
    JS_FreeBigInt(ctx, b, &b_s);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    if (unlikely(ret)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    *pres = JS_CompactBigInt(ctx, res);
    return 0;
 fail:
    JS_FreeValue(ctx, res);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return -1;
}

/* b must be a positive integer */
static int js_bfdec_pow(bfdec_t *r, const bfdec_t *a, const bfdec_t *b)
{
    bfdec_t b1;
    int32_t b2;
    int ret;

    bfdec_init(b->ctx, &b1);
    ret = bfdec_set(&b1, b);
    if (ret) {
        bfdec_delete(&b1);
        return ret;
    }
    ret = bfdec_rint(&b1, BF_RNDZ);
    if (ret) {
        bfdec_delete(&b1);
        return BF_ST_INVALID_OP; /* must be an integer */
    }
    ret = bfdec_get_int32(&b2, &b1);
    bfdec_delete(&b1);
    if (ret)
        return ret; /* overflow */
    if (b2 < 0)
        return BF_ST_INVALID_OP; /* must be positive */
    return bfdec_pow_ui(r, a, b2);
}

 int js_binary_arith_bigdecimal(JSContext *ctx, OPCodeEnum op,
                                      JSValue *pres, JSValue op1, JSValue op2)
{
    bfdec_t *r, *a, *b;
    int ret;
    JSValue res;

    res = JS_NewBigDecimal(ctx);
    if (JS_IsException(res))
        goto fail;
    r = JS_GetBigDecimal(res);

    a = JS_ToBigDecimal(ctx, op1);
    if (!a)
        goto fail;
    b = JS_ToBigDecimal(ctx, op2);
    if (!b)
        goto fail;
    switch(op) {
    case OP_add:
        ret = bfdec_add(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_sub:
        ret = bfdec_sub(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_mul:
        ret = bfdec_mul(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_div:
        ret = bfdec_div(r, a, b, BF_PREC_INF, BF_RNDZ);
        break;
    case OP_math_mod:
        /* Euclidian remainder */
        ret = bfdec_rem(r, a, b, BF_PREC_INF, BF_RNDZ, BF_DIVREM_EUCLIDIAN);
        break;
    case OP_mod:
        ret = bfdec_rem(r, a, b, BF_PREC_INF, BF_RNDZ, BF_RNDZ);
        break;
    case OP_pow:
        ret = js_bfdec_pow(r, a, b);
        break;
    default:
        abort();
    }
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    if (unlikely(ret)) {
        JS_FreeValue(ctx, res);
        throw_bf_exception(ctx, ret);
        return -1;
    }
    *pres = res;
    return 0;
 fail:
    JS_FreeValue(ctx, res);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return -1;
}

 no_inline __exception int js_binary_arith_slow(JSContext *ctx, JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2, res;
    uint32_t tag1, tag2;
    int ret;
    double d1, d2;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    /* fast path for float operations */
    if (tag1 == JS_TAG_FLOAT64 && tag2 == JS_TAG_FLOAT64) {
        d1 = JS_VALUE_GET_FLOAT64(op1);
        d2 = JS_VALUE_GET_FLOAT64(op2);
        goto handle_float64;
    }

    /* try to call an overloaded operator */
    if ((tag1 == JS_TAG_OBJECT &&
         (tag2 != JS_TAG_NULL && tag2 != JS_TAG_UNDEFINED)) ||
        (tag2 == JS_TAG_OBJECT &&
         (tag1 != JS_TAG_NULL && tag1 != JS_TAG_UNDEFINED))) {
        ret = js_call_binary_op_fallback(ctx, &res, op1, op2, op, TRUE, 0);
        if (ret != 0) {
            JS_FreeValue(ctx, op1);
            JS_FreeValue(ctx, op2);
            if (ret < 0) {
                goto exception;
            } else {
                sp[-2] = res;
                return 0;
            }
        }
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_INT && tag2 == JS_TAG_INT) {
        int32_t v1, v2;
        int64_t v;
        v1 = JS_VALUE_GET_INT(op1);
        v2 = JS_VALUE_GET_INT(op2);
        switch(op) {
        case OP_sub:
            v = (int64_t)v1 - (int64_t)v2;
            break;
        case OP_mul:
            v = (int64_t)v1 * (int64_t)v2;
            if (is_math_mode(ctx) &&
                (v < -MAX_SAFE_INTEGER || v > MAX_SAFE_INTEGER))
                goto handle_bigint;
            if (v == 0 && (v1 | v2) < 0) {
                sp[-2] = __JS_NewFloat64(ctx, -0.0);
                return 0;
            }
            break;
        case OP_div:
            if (is_math_mode(ctx))
                goto handle_bigint;
            sp[-2] = __JS_NewFloat64(ctx, (double)v1 / (double)v2);
            return 0;
        case OP_math_mod:
            if (unlikely(v2 == 0)) {
                throw_bf_exception(ctx, BF_ST_DIVIDE_ZERO);
                goto exception;
            }
            v = (int64_t)v1 % (int64_t)v2;
            if (v < 0) {
                if (v2 < 0)
                    v -= v2;
                else
                    v += v2;
            }
            break;
        case OP_mod:
            if (v1 < 0 || v2 <= 0) {
                sp[-2] = JS_NewFloat64(ctx, fmod(v1, v2));
                return 0;
            } else {
                v = (int64_t)v1 % (int64_t)v2;
            }
            break;
        case OP_pow:
            if (!is_math_mode(ctx)) {
                sp[-2] = JS_NewFloat64(ctx, js_pow(v1, v2));
                return 0;
            } else {
                goto handle_bigint;
            }
            break;
        default:
            abort();
        }
        sp[-2] = JS_NewInt64(ctx, v);
    } else if (tag1 == JS_TAG_BIG_DECIMAL || tag2 == JS_TAG_BIG_DECIMAL) {
        if (ctx->rt->bigdecimal_ops.binary_arith(ctx, op, sp - 2, op1, op2))
            goto exception;
    } else if (tag1 == JS_TAG_BIG_FLOAT || tag2 == JS_TAG_BIG_FLOAT) {
        if (ctx->rt->bigfloat_ops.binary_arith(ctx, op, sp - 2, op1, op2))
            goto exception;
    } else if (tag1 == JS_TAG_BIG_INT || tag2 == JS_TAG_BIG_INT) {
    handle_bigint:
        if (ctx->rt->bigint_ops.binary_arith(ctx, op, sp - 2, op1, op2))
            goto exception;
    } else {
        double dr;
        /* float64 result */
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
    handle_float64:
        if (is_math_mode(ctx) && is_safe_integer(d1) && is_safe_integer(d2))
            goto handle_bigint;
        switch(op) {
        case OP_sub:
            dr = d1 - d2;
            break;
        case OP_mul:
            dr = d1 * d2;
            break;
        case OP_div:
            dr = d1 / d2;
            break;
        case OP_mod:
            dr = fmod(d1, d2);
            break;
        case OP_math_mod:
            d2 = fabs(d2);
            dr = fmod(d1, d2);
            /* XXX: loss of accuracy if dr < 0 */
            if (dr < 0)
                dr += d2;
            break;
        case OP_pow:
            dr = js_pow(d1, d2);
            break;
        default:
            abort();
        }
        sp[-2] = __JS_NewFloat64(ctx, dr);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline __exception int js_add_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2, res;
    uint32_t tag1, tag2;
    int ret;

    op1 = sp[-2];
    op2 = sp[-1];

    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    /* fast path for float64 */
    if (tag1 == JS_TAG_FLOAT64 && tag2 == JS_TAG_FLOAT64) {
        double d1, d2;
        d1 = JS_VALUE_GET_FLOAT64(op1);
        d2 = JS_VALUE_GET_FLOAT64(op2);
        sp[-2] = __JS_NewFloat64(ctx, d1 + d2);
        return 0;
    }

    if (tag1 == JS_TAG_OBJECT || tag2 == JS_TAG_OBJECT) {
        /* try to call an overloaded operator */
        if ((tag1 == JS_TAG_OBJECT &&
             (tag2 != JS_TAG_NULL && tag2 != JS_TAG_UNDEFINED &&
              tag2 != JS_TAG_STRING)) ||
            (tag2 == JS_TAG_OBJECT &&
             (tag1 != JS_TAG_NULL && tag1 != JS_TAG_UNDEFINED &&
              tag1 != JS_TAG_STRING))) {
            ret = js_call_binary_op_fallback(ctx, &res, op1, op2, OP_add,
                                             FALSE, HINT_NONE);
            if (ret != 0) {
                JS_FreeValue(ctx, op1);
                JS_FreeValue(ctx, op2);
                if (ret < 0) {
                    goto exception;
                } else {
                    sp[-2] = res;
                    return 0;
                }
            }
        }

        op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NONE);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }

        op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NONE);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            goto exception;
        }
        tag1 = JS_VALUE_GET_NORM_TAG(op1);
        tag2 = JS_VALUE_GET_NORM_TAG(op2);
    }

    if (tag1 == JS_TAG_STRING || tag2 == JS_TAG_STRING) {
        sp[-2] = JS_ConcatString(ctx, op1, op2);
        if (JS_IsException(sp[-2]))
            goto exception;
        return 0;
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_INT && tag2 == JS_TAG_INT) {
        int32_t v1, v2;
        int64_t v;
        v1 = JS_VALUE_GET_INT(op1);
        v2 = JS_VALUE_GET_INT(op2);
        v = (int64_t)v1 + (int64_t)v2;
        sp[-2] = JS_NewInt64(ctx, v);
    } else if (tag1 == JS_TAG_BIG_DECIMAL || tag2 == JS_TAG_BIG_DECIMAL) {
        if (ctx->rt->bigdecimal_ops.binary_arith(ctx, OP_add, sp - 2, op1, op2))
            goto exception;
    } else if (tag1 == JS_TAG_BIG_FLOAT || tag2 == JS_TAG_BIG_FLOAT) {
        if (ctx->rt->bigfloat_ops.binary_arith(ctx, OP_add, sp - 2, op1, op2))
            goto exception;
    } else if (tag1 == JS_TAG_BIG_INT || tag2 == JS_TAG_BIG_INT) {
    handle_bigint:
        if (ctx->rt->bigint_ops.binary_arith(ctx, OP_add, sp - 2, op1, op2))
            goto exception;
    } else {
        double d1, d2;
        /* float64 result */
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
        if (is_math_mode(ctx) && is_safe_integer(d1) && is_safe_integer(d2))
            goto handle_bigint;
        sp[-2] = __JS_NewFloat64(ctx, d1 + d2);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline __exception int js_binary_logic_slow(JSContext *ctx,
                                                      JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2, res;
    int ret;
    uint32_t tag1, tag2;
    uint32_t v1, v2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    /* try to call an overloaded operator */
    if ((tag1 == JS_TAG_OBJECT &&
         (tag2 != JS_TAG_NULL && tag2 != JS_TAG_UNDEFINED)) ||
        (tag2 == JS_TAG_OBJECT &&
         (tag1 != JS_TAG_NULL && tag1 != JS_TAG_UNDEFINED))) {
        ret = js_call_binary_op_fallback(ctx, &res, op1, op2, op, TRUE, 0);
        if (ret != 0) {
            JS_FreeValue(ctx, op1);
            JS_FreeValue(ctx, op2);
            if (ret < 0) {
                goto exception;
            } else {
                sp[-2] = res;
                return 0;
            }
        }
    }

    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }

    if (is_math_mode(ctx))
        goto bigint_op;

    tag1 = JS_VALUE_GET_TAG(op1);
    tag2 = JS_VALUE_GET_TAG(op2);
    if (tag1 == JS_TAG_BIG_INT || tag2 == JS_TAG_BIG_INT) {
        if (tag1 != tag2) {
            JS_FreeValue(ctx, op1);
            JS_FreeValue(ctx, op2);
            JS_ThrowTypeError(ctx, "both operands must be bigint");
            goto exception;
        } else {
        bigint_op:
            if (ctx->rt->bigint_ops.binary_arith(ctx, op, sp - 2, op1, op2))
                goto exception;
        }
    } else {
        if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v1, op1))) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v2, op2)))
            goto exception;
        switch(op) {
        case OP_shl:
            r = v1 << (v2 & 0x1f);
            break;
        case OP_sar:
            r = (int)v1 >> (v2 & 0x1f);
            break;
        case OP_and:
            r = v1 & v2;
            break;
        case OP_or:
            r = v1 | v2;
            break;
        case OP_xor:
            r = v1 ^ v2;
            break;
        default:
            abort();
        }
        sp[-2] = JS_NewInt32(ctx, r);
    }
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

/* Note: also used for bigint */
 int js_compare_bigfloat(JSContext *ctx, OPCodeEnum op,
                               JSValue op1, JSValue op2)
{
    bf_t a_s, b_s, *a, *b;
    int res;

    a = JS_ToBigFloat(ctx, &a_s, op1);
    if (!a) {
        JS_FreeValue(ctx, op2);
        return -1;
    }
    b = JS_ToBigFloat(ctx, &b_s, op2);
    if (!b) {
        if (a == &a_s)
            bf_delete(a);
        JS_FreeValue(ctx, op1);
        return -1;
    }
    switch(op) {
    case OP_lt:
        res = bf_cmp_lt(a, b); /* if NaN return false */
        break;
    case OP_lte:
        res = bf_cmp_le(a, b); /* if NaN return false */
        break;
    case OP_gt:
        res = bf_cmp_lt(b, a); /* if NaN return false */
        break;
    case OP_gte:
        res = bf_cmp_le(b, a); /* if NaN return false */
        break;
    case OP_eq:
        res = bf_cmp_eq(a, b); /* if NaN return false */
        break;
    default:
        abort();
    }
    if (a == &a_s)
        bf_delete(a);
    if (b == &b_s)
        bf_delete(b);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return res;
}

 int js_compare_bigdecimal(JSContext *ctx, OPCodeEnum op,
                                 JSValue op1, JSValue op2)
{
    bfdec_t *a, *b;
    int res;

    /* Note: binary floats are converted to bigdecimal with
       toString(). It is not mathematically correct but is consistent
       with the BigDecimal() constructor behavior */
    op1 = JS_ToBigDecimalFree(ctx, op1, TRUE);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        return -1;
    }
    op2 = JS_ToBigDecimalFree(ctx, op2, TRUE);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        return -1;
    }
    a = JS_ToBigDecimal(ctx, op1);
    b = JS_ToBigDecimal(ctx, op2);

    switch(op) {
    case OP_lt:
        res = bfdec_cmp_lt(a, b); /* if NaN return false */
        break;
    case OP_lte:
        res = bfdec_cmp_le(a, b); /* if NaN return false */
        break;
    case OP_gt:
        res = bfdec_cmp_lt(b, a); /* if NaN return false */
        break;
    case OP_gte:
        res = bfdec_cmp_le(b, a); /* if NaN return false */
        break;
    case OP_eq:
        res = bfdec_cmp_eq(a, b); /* if NaN return false */
        break;
    default:
        abort();
    }
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    return res;
}

 no_inline int js_relational_slow(JSContext *ctx, JSValue *sp,
                                        OPCodeEnum op)
{
    JSValue op1, op2, ret;
    int res;
    uint32_t tag1, tag2;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    /* try to call an overloaded operator */
    if ((tag1 == JS_TAG_OBJECT &&
         (tag2 != JS_TAG_NULL && tag2 != JS_TAG_UNDEFINED)) ||
        (tag2 == JS_TAG_OBJECT &&
         (tag1 != JS_TAG_NULL && tag1 != JS_TAG_UNDEFINED))) {
        res = js_call_binary_op_fallback(ctx, &ret, op1, op2, op,
                                         FALSE, HINT_NUMBER);
        if (res != 0) {
            JS_FreeValue(ctx, op1);
            JS_FreeValue(ctx, op2);
            if (res < 0) {
                goto exception;
            } else {
                sp[-2] = ret;
                return 0;
            }
        }
    }
    op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NUMBER);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NUMBER);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);

    if (tag1 == JS_TAG_STRING && tag2 == JS_TAG_STRING) {
        JSString *p1, *p2;
        p1 = JS_VALUE_GET_STRING(op1);
        p2 = JS_VALUE_GET_STRING(op2);
        res = js_string_compare(ctx, p1, p2);
        switch(op) {
        case OP_lt:
            res = (res < 0);
            break;
        case OP_lte:
            res = (res <= 0);
            break;
        case OP_gt:
            res = (res > 0);
            break;
        default:
        case OP_gte:
            res = (res >= 0);
            break;
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
    } else if ((tag1 <= JS_TAG_NULL || tag1 == JS_TAG_FLOAT64) &&
               (tag2 <= JS_TAG_NULL || tag2 == JS_TAG_FLOAT64)) {
        /* fast path for float64/int */
        goto float64_compare;
    } else {
        if (((tag1 == JS_TAG_BIG_INT && tag2 == JS_TAG_STRING) ||
             (tag2 == JS_TAG_BIG_INT && tag1 == JS_TAG_STRING)) &&
            !is_math_mode(ctx)) {
            if (tag1 == JS_TAG_STRING) {
                op1 = JS_StringToBigInt(ctx, op1);
                if (JS_VALUE_GET_TAG(op1) != JS_TAG_BIG_INT)
                    goto invalid_bigint_string;
            }
            if (tag2 == JS_TAG_STRING) {
                op2 = JS_StringToBigInt(ctx, op2);
                if (JS_VALUE_GET_TAG(op2) != JS_TAG_BIG_INT) {
                invalid_bigint_string:
                    JS_FreeValue(ctx, op1);
                    JS_FreeValue(ctx, op2);
                    res = FALSE;
                    goto done;
                }
            }
        } else {
            op1 = JS_ToNumericFree(ctx, op1);
            if (JS_IsException(op1)) {
                JS_FreeValue(ctx, op2);
                goto exception;
            }
            op2 = JS_ToNumericFree(ctx, op2);
            if (JS_IsException(op2)) {
                JS_FreeValue(ctx, op1);
                goto exception;
            }
        }

        tag1 = JS_VALUE_GET_NORM_TAG(op1);
        tag2 = JS_VALUE_GET_NORM_TAG(op2);

        if (tag1 == JS_TAG_BIG_DECIMAL || tag2 == JS_TAG_BIG_DECIMAL) {
            res = ctx->rt->bigdecimal_ops.compare(ctx, op, op1, op2);
            if (res < 0)
                goto exception;
        } else if (tag1 == JS_TAG_BIG_FLOAT || tag2 == JS_TAG_BIG_FLOAT) {
            res = ctx->rt->bigfloat_ops.compare(ctx, op, op1, op2);
            if (res < 0)
                goto exception;
        } else if (tag1 == JS_TAG_BIG_INT || tag2 == JS_TAG_BIG_INT) {
            res = ctx->rt->bigint_ops.compare(ctx, op, op1, op2);
            if (res < 0)
                goto exception;
        } else {
            double d1, d2;

        float64_compare:
            /* can use floating point comparison */
            if (tag1 == JS_TAG_FLOAT64) {
                d1 = JS_VALUE_GET_FLOAT64(op1);
            } else {
                d1 = JS_VALUE_GET_INT(op1);
            }
            if (tag2 == JS_TAG_FLOAT64) {
                d2 = JS_VALUE_GET_FLOAT64(op2);
            } else {
                d2 = JS_VALUE_GET_INT(op2);
            }
            switch(op) {
            case OP_lt:
                res = (d1 < d2); /* if NaN return false */
                break;
            case OP_lte:
                res = (d1 <= d2); /* if NaN return false */
                break;
            case OP_gt:
                res = (d1 > d2); /* if NaN return false */
                break;
            default:
            case OP_gte:
                res = (d1 >= d2); /* if NaN return false */
                break;
            }
        }
    }
 done:
    sp[-2] = JS_NewBool(ctx, res);
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

static BOOL tag_is_number(uint32_t tag)
{
    return (tag == JS_TAG_INT || tag == JS_TAG_BIG_INT ||
            tag == JS_TAG_FLOAT64 || tag == JS_TAG_BIG_FLOAT ||
            tag == JS_TAG_BIG_DECIMAL);
}

 no_inline __exception int js_eq_slow(JSContext *ctx, JSValue *sp,
                                            BOOL is_neq)
{
    JSValue op1, op2, ret;
    int res;
    uint32_t tag1, tag2;

    op1 = sp[-2];
    op2 = sp[-1];
 redo:
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    if (tag_is_number(tag1) && tag_is_number(tag2)) {
        if (tag1 == JS_TAG_INT && tag2 == JS_TAG_INT) {
            res = JS_VALUE_GET_INT(op1) == JS_VALUE_GET_INT(op2);
        } else if ((tag1 == JS_TAG_FLOAT64 &&
                    (tag2 == JS_TAG_INT || tag2 == JS_TAG_FLOAT64)) ||
                   (tag2 == JS_TAG_FLOAT64 &&
                    (tag1 == JS_TAG_INT || tag1 == JS_TAG_FLOAT64))) {
            double d1, d2;
            if (tag1 == JS_TAG_FLOAT64) {
                d1 = JS_VALUE_GET_FLOAT64(op1);
            } else {
                d1 = JS_VALUE_GET_INT(op1);
            }
            if (tag2 == JS_TAG_FLOAT64) {
                d2 = JS_VALUE_GET_FLOAT64(op2);
            } else {
                d2 = JS_VALUE_GET_INT(op2);
            }
            res = (d1 == d2);
        } else if (tag1 == JS_TAG_BIG_DECIMAL || tag2 == JS_TAG_BIG_DECIMAL) {
            res = ctx->rt->bigdecimal_ops.compare(ctx, OP_eq, op1, op2);
            if (res < 0)
                goto exception;
        } else if (tag1 == JS_TAG_BIG_FLOAT || tag2 == JS_TAG_BIG_FLOAT) {
            res = ctx->rt->bigfloat_ops.compare(ctx, OP_eq, op1, op2);
            if (res < 0)
                goto exception;
        } else {
            res = ctx->rt->bigint_ops.compare(ctx, OP_eq, op1, op2);
            if (res < 0)
                goto exception;
        }
    } else if (tag1 == tag2) {
        if (tag1 == JS_TAG_OBJECT) {
            /* try the fallback operator */
            res = js_call_binary_op_fallback(ctx, &ret, op1, op2,
                                             is_neq ? OP_neq : OP_eq,
                                             FALSE, HINT_NONE);
            if (res != 0) {
                JS_FreeValue(ctx, op1);
                JS_FreeValue(ctx, op2);
                if (res < 0) {
                    goto exception;
                } else {
                    sp[-2] = ret;
                    return 0;
                }
            }
        }
        res = js_strict_eq2(ctx, op1, op2, JS_EQ_STRICT);
    } else if ((tag1 == JS_TAG_NULL && tag2 == JS_TAG_UNDEFINED) ||
               (tag2 == JS_TAG_NULL && tag1 == JS_TAG_UNDEFINED)) {
        res = TRUE;
    } else if ((tag1 == JS_TAG_STRING && tag_is_number(tag2)) ||
               (tag2 == JS_TAG_STRING && tag_is_number(tag1))) {

        if ((tag1 == JS_TAG_BIG_INT || tag2 == JS_TAG_BIG_INT) &&
            !is_math_mode(ctx)) {
            if (tag1 == JS_TAG_STRING) {
                op1 = JS_StringToBigInt(ctx, op1);
                if (JS_VALUE_GET_TAG(op1) != JS_TAG_BIG_INT)
                    goto invalid_bigint_string;
            }
            if (tag2 == JS_TAG_STRING) {
                op2 = JS_StringToBigInt(ctx, op2);
                if (JS_VALUE_GET_TAG(op2) != JS_TAG_BIG_INT) {
                invalid_bigint_string:
                    JS_FreeValue(ctx, op1);
                    JS_FreeValue(ctx, op2);
                    res = FALSE;
                    goto done;
                }
            }
        } else {
            op1 = JS_ToNumericFree(ctx, op1);
            if (JS_IsException(op1)) {
                JS_FreeValue(ctx, op2);
                goto exception;
            }
            op2 = JS_ToNumericFree(ctx, op2);
            if (JS_IsException(op2)) {
                JS_FreeValue(ctx, op1);
                goto exception;
            }
        }
        res = js_strict_eq(ctx, op1, op2);
    } else if (tag1 == JS_TAG_BOOL) {
        op1 = JS_NewInt32(ctx, JS_VALUE_GET_INT(op1));
        goto redo;
    } else if (tag2 == JS_TAG_BOOL) {
        op2 = JS_NewInt32(ctx, JS_VALUE_GET_INT(op2));
        goto redo;
    } else if ((tag1 == JS_TAG_OBJECT &&
                (tag_is_number(tag2) || tag2 == JS_TAG_STRING || tag2 == JS_TAG_SYMBOL)) ||
               (tag2 == JS_TAG_OBJECT &&
                (tag_is_number(tag1) || tag1 == JS_TAG_STRING || tag1 == JS_TAG_SYMBOL))) {

        /* try the fallback operator */
        res = js_call_binary_op_fallback(ctx, &ret, op1, op2,
                                         is_neq ? OP_neq : OP_eq,
                                         FALSE, HINT_NONE);
        if (res != 0) {
            JS_FreeValue(ctx, op1);
            JS_FreeValue(ctx, op2);
            if (res < 0) {
                goto exception;
            } else {
                sp[-2] = ret;
                return 0;
            }
        }

        op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NONE);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NONE);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            goto exception;
        }
        goto redo;
    } else {
        /* IsHTMLDDA object is equivalent to undefined for '==' and '!=' */
        if ((JS_IsHTMLDDA(ctx, op1) &&
             (tag2 == JS_TAG_NULL || tag2 == JS_TAG_UNDEFINED)) ||
            (JS_IsHTMLDDA(ctx, op2) &&
             (tag1 == JS_TAG_NULL || tag1 == JS_TAG_UNDEFINED))) {
            res = TRUE;
        } else {
            res = FALSE;
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
    }
 done:
    sp[-2] = JS_NewBool(ctx, res ^ is_neq);
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline int js_shr_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    uint32_t v1, v2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    op1 = JS_ToNumericFree(ctx, op1);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToNumericFree(ctx, op2);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    /* XXX: could forbid >>> in bignum mode */
    if (!is_math_mode(ctx) &&
        (JS_VALUE_GET_TAG(op1) == JS_TAG_BIG_INT ||
         JS_VALUE_GET_TAG(op2) == JS_TAG_BIG_INT)) {
        JS_ThrowTypeError(ctx, "bigint operands are forbidden for >>>");
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    /* cannot give an exception */
    JS_ToUint32Free(ctx, &v1, op1);
    JS_ToUint32Free(ctx, &v2, op2);
    r = v1 >> (v2 & 0x1f);
    sp[-2] = JS_NewUint32(ctx, r);
    return 0;
 exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 JSValue js_mul_pow10_to_float64(JSContext *ctx, const bf_t *a,
                                       int64_t exponent)
{
    bf_t r_s, *r = &r_s;
    double d;
    int ret;

    /* always convert to Float64 */
    bf_init(ctx->bf_ctx, r);
    ret = bf_mul_pow_radix(r, a, 10, exponent,
                           53, bf_set_exp_bits(11) | BF_RNDN |
                           BF_FLAG_SUBNORMAL);
    bf_get_float64(r, &d, BF_RNDN);
    bf_delete(r);
    if (ret & BF_ST_MEM_ERROR)
        return JS_ThrowOutOfMemory(ctx);
    else
        return __JS_NewFloat64(ctx, d);
}

 no_inline int js_mul_pow10(JSContext *ctx, JSValue *sp)
{
    bf_t a_s, *a, *r;
    JSValue op1, op2, res;
    int64_t e;
    int ret;

    res = JS_NewBigFloat(ctx);
    if (JS_IsException(res))
        return -1;
    r = JS_GetBigFloat(res);
    op1 = sp[-2];
    op2 = sp[-1];
    a = JS_ToBigFloat(ctx, &a_s, op1);
    if (!a)
        return -1;
    if (JS_IsBigInt(ctx, op2)) {
        ret = JS_ToBigInt64(ctx, &e, op2);
    } else {
        ret = JS_ToInt64(ctx, &e, op2);
    }
    if (ret) {
        if (a == &a_s)
            bf_delete(a);
        JS_FreeValue(ctx, res);
        return -1;
    }

    bf_mul_pow_radix(r, a, 10, e, ctx->fp_env.prec, ctx->fp_env.flags);
    if (a == &a_s)
        bf_delete(a);
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    sp[-2] = res;
    return 0;
}

#else /* !CONFIG_BIGNUM */

static JSValue JS_ThrowUnsupportedBigint(JSContext *ctx)
{
    return JS_ThrowTypeError(ctx, "bigint is not supported");
}

JSValue JS_NewBigInt64(JSContext *ctx, int64_t v)
{
    return JS_ThrowUnsupportedBigint(ctx);
}

JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v)
{
    return JS_ThrowUnsupportedBigint(ctx);
}

int JS_ToBigInt64(JSContext *ctx, int64_t *pres, JSValueConst val)
{
    JS_ThrowUnsupportedBigint(ctx);
    *pres = 0;
    return -1;
}

 no_inline __exception int js_unary_arith_slow(JSContext *ctx,
                                                     JSValue *sp,
                                                     OPCodeEnum op)
{
    JSValue op1;
    double d;

    op1 = sp[-1];
    if (unlikely(JS_ToFloat64Free(ctx, &d, op1))) {
        sp[-1] = JS_UNDEFINED;
        return -1;
    }
    switch(op) {
        case OP_inc:
            d++;
            break;
        case OP_dec:
            d--;
            break;
        case OP_plus:
            break;
        case OP_neg:
            d = -d;
            break;
        default:
            abort();
    }
    sp[-1] = JS_NewFloat64(ctx, d);
    return 0;
}

/* specific case necessary for correct return value semantics */
 __exception int js_post_inc_slow(JSContext *ctx,
                                        JSValue *sp, OPCodeEnum op)
{
    JSValue op1;
    double d, r;

    op1 = sp[-1];
    if (unlikely(JS_ToFloat64Free(ctx, &d, op1))) {
        sp[-1] = JS_UNDEFINED;
        return -1;
    }
    r = d + 2 * (op - OP_post_dec) - 1;
    sp[0] = JS_NewFloat64(ctx, r);
    sp[-1] = JS_NewFloat64(ctx, d);
    return 0;
}

 no_inline __exception int js_binary_arith_slow(JSContext *ctx, JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2;
    double d1, d2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    if (unlikely(JS_ToFloat64Free(ctx, &d1, op1))) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    if (unlikely(JS_ToFloat64Free(ctx, &d2, op2))) {
        goto exception;
    }
    switch(op) {
        case OP_sub:
            r = d1 - d2;
            break;
        case OP_mul:
            r = d1 * d2;
            break;
        case OP_div:
            r = d1 / d2;
            break;
        case OP_mod:
            r = fmod(d1, d2);
            break;
        case OP_pow:
            r = js_pow(d1, d2);
            break;
        default:
            abort();
    }
    sp[-2] = JS_NewFloat64(ctx, r);
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline __exception int js_add_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    uint32_t tag1, tag2;

    op1 = sp[-2];
    op2 = sp[-1];
    tag1 = JS_VALUE_GET_TAG(op1);
    tag2 = JS_VALUE_GET_TAG(op2);
    if ((tag1 == JS_TAG_INT || JS_TAG_IS_FLOAT64(tag1)) &&
        (tag2 == JS_TAG_INT || JS_TAG_IS_FLOAT64(tag2))) {
        goto add_numbers;
    } else {
        op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NONE);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NONE);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            goto exception;
        }
        tag1 = JS_VALUE_GET_TAG(op1);
        tag2 = JS_VALUE_GET_TAG(op2);
        if (tag1 == JS_TAG_STRING || tag2 == JS_TAG_STRING) {
            sp[-2] = JS_ConcatString(ctx, op1, op2);
            if (JS_IsException(sp[-2]))
                goto exception;
        } else {
            double d1, d2;
            add_numbers:
            if (JS_ToFloat64Free(ctx, &d1, op1)) {
                JS_FreeValue(ctx, op2);
                goto exception;
            }
            if (JS_ToFloat64Free(ctx, &d2, op2))
                goto exception;
            sp[-2] = JS_NewFloat64(ctx, d1 + d2);
        }
    }
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline __exception int js_binary_logic_slow(JSContext *ctx,
                                                      JSValue *sp,
                                                      OPCodeEnum op)
{
    JSValue op1, op2;
    uint32_t v1, v2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v1, op1))) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    if (unlikely(JS_ToInt32Free(ctx, (int32_t *)&v2, op2)))
        goto exception;
    switch(op) {
        case OP_shl:
            r = v1 << (v2 & 0x1f);
            break;
        case OP_sar:
            r = (int)v1 >> (v2 & 0x1f);
            break;
        case OP_and:
            r = v1 & v2;
            break;
        case OP_or:
            r = v1 | v2;
            break;
        case OP_xor:
            r = v1 ^ v2;
            break;
        default:
            abort();
    }
    sp[-2] = JS_NewInt32(ctx, r);
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline int js_not_slow(JSContext *ctx, JSValue *sp)
{
    int32_t v1;

    if (unlikely(JS_ToInt32Free(ctx, &v1, sp[-1]))) {
        sp[-1] = JS_UNDEFINED;
        return -1;
    }
    sp[-1] = JS_NewInt32(ctx, ~v1);
    return 0;
}

 no_inline int js_relational_slow(JSContext *ctx, JSValue *sp,
                                        OPCodeEnum op)
{
    JSValue op1, op2;
    int res;

    op1 = sp[-2];
    op2 = sp[-1];
    op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NUMBER);
    if (JS_IsException(op1)) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NUMBER);
    if (JS_IsException(op2)) {
        JS_FreeValue(ctx, op1);
        goto exception;
    }
    if (JS_VALUE_GET_TAG(op1) == JS_TAG_STRING &&
        JS_VALUE_GET_TAG(op2) == JS_TAG_STRING) {
        JSString *p1, *p2;
        p1 = JS_VALUE_GET_STRING(op1);
        p2 = JS_VALUE_GET_STRING(op2);
        res = js_string_compare(ctx, p1, p2);
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
        switch(op) {
            case OP_lt:
                res = (res < 0);
                break;
            case OP_lte:
                res = (res <= 0);
                break;
            case OP_gt:
                res = (res > 0);
                break;
            default:
            case OP_gte:
                res = (res >= 0);
                break;
        }
    } else {
        double d1, d2;
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
        switch(op) {
            case OP_lt:
                res = (d1 < d2); /* if NaN return false */
                break;
            case OP_lte:
                res = (d1 <= d2); /* if NaN return false */
                break;
            case OP_gt:
                res = (d1 > d2); /* if NaN return false */
                break;
            default:
            case OP_gte:
                res = (d1 >= d2); /* if NaN return false */
                break;
        }
    }
    sp[-2] = JS_NewBool(ctx, res);
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline __exception int js_eq_slow(JSContext *ctx, JSValue *sp,
                                            BOOL is_neq)
{
    JSValue op1, op2;
    int tag1, tag2;
    BOOL res;

    op1 = sp[-2];
    op2 = sp[-1];
    redo:
    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    if (tag1 == tag2 ||
        (tag1 == JS_TAG_INT && tag2 == JS_TAG_FLOAT64) ||
        (tag2 == JS_TAG_INT && tag1 == JS_TAG_FLOAT64)) {
        res = js_strict_eq(ctx, op1, op2);
    } else if ((tag1 == JS_TAG_NULL && tag2 == JS_TAG_UNDEFINED) ||
               (tag2 == JS_TAG_NULL && tag1 == JS_TAG_UNDEFINED)) {
        res = TRUE;
    } else if ((tag1 == JS_TAG_STRING && (tag2 == JS_TAG_INT ||
                                          tag2 == JS_TAG_FLOAT64)) ||
               (tag2 == JS_TAG_STRING && (tag1 == JS_TAG_INT ||
                                          tag1 == JS_TAG_FLOAT64))) {
        double d1;
        double d2;
        if (JS_ToFloat64Free(ctx, &d1, op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        if (JS_ToFloat64Free(ctx, &d2, op2))
            goto exception;
        res = (d1 == d2);
    } else if (tag1 == JS_TAG_BOOL) {
        op1 = JS_NewInt32(ctx, JS_VALUE_GET_INT(op1));
        goto redo;
    } else if (tag2 == JS_TAG_BOOL) {
        op2 = JS_NewInt32(ctx, JS_VALUE_GET_INT(op2));
        goto redo;
    } else if (tag1 == JS_TAG_OBJECT &&
               (tag2 == JS_TAG_INT || tag2 == JS_TAG_FLOAT64 || tag2 == JS_TAG_STRING || tag2 == JS_TAG_SYMBOL)) {
        op1 = JS_ToPrimitiveFree(ctx, op1, HINT_NONE);
        if (JS_IsException(op1)) {
            JS_FreeValue(ctx, op2);
            goto exception;
        }
        goto redo;
    } else if (tag2 == JS_TAG_OBJECT &&
               (tag1 == JS_TAG_INT || tag1 == JS_TAG_FLOAT64 || tag1 == JS_TAG_STRING || tag1 == JS_TAG_SYMBOL)) {
        op2 = JS_ToPrimitiveFree(ctx, op2, HINT_NONE);
        if (JS_IsException(op2)) {
            JS_FreeValue(ctx, op1);
            goto exception;
        }
        goto redo;
    } else {
        /* IsHTMLDDA object is equivalent to undefined for '==' and '!=' */
        if ((JS_IsHTMLDDA(ctx, op1) &&
             (tag2 == JS_TAG_NULL || tag2 == JS_TAG_UNDEFINED)) ||
            (JS_IsHTMLDDA(ctx, op2) &&
             (tag1 == JS_TAG_NULL || tag1 == JS_TAG_UNDEFINED))) {
            res = TRUE;
        } else {
            res = FALSE;
        }
        JS_FreeValue(ctx, op1);
        JS_FreeValue(ctx, op2);
    }
    sp[-2] = JS_NewBool(ctx, res ^ is_neq);
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

 no_inline int js_shr_slow(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    uint32_t v1, v2, r;

    op1 = sp[-2];
    op2 = sp[-1];
    if (unlikely(JS_ToUint32Free(ctx, &v1, op1))) {
        JS_FreeValue(ctx, op2);
        goto exception;
    }
    if (unlikely(JS_ToUint32Free(ctx, &v2, op2)))
        goto exception;
    r = v1 >> (v2 & 0x1f);
    sp[-2] = JS_NewUint32(ctx, r);
    return 0;
    exception:
    sp[-2] = JS_UNDEFINED;
    sp[-1] = JS_UNDEFINED;
    return -1;
}

#endif /* !CONFIG_BIGNUM */


/* XXX: Should take JSValueConst arguments */
 BOOL js_strict_eq2(JSContext *ctx, JSValue op1, JSValue op2,
                          JSStrictEqModeEnum eq_mode)
{
    BOOL res;
    int tag1, tag2;
    double d1, d2;

    tag1 = JS_VALUE_GET_NORM_TAG(op1);
    tag2 = JS_VALUE_GET_NORM_TAG(op2);
    switch(tag1) {
        case JS_TAG_BOOL:
            if (tag1 != tag2) {
                res = FALSE;
            } else {
                res = JS_VALUE_GET_INT(op1) == JS_VALUE_GET_INT(op2);
                goto done_no_free;
            }
            break;
        case JS_TAG_NULL:
        case JS_TAG_UNDEFINED:
            res = (tag1 == tag2);
            break;
        case JS_TAG_STRING:
        {
            JSString *p1, *p2;
            if (tag1 != tag2) {
                res = FALSE;
            } else {
                p1 = JS_VALUE_GET_STRING(op1);
                p2 = JS_VALUE_GET_STRING(op2);
                res = (js_string_compare(ctx, p1, p2) == 0);
            }
        }
            break;
        case JS_TAG_SYMBOL:
        {
            JSAtomStruct *p1, *p2;
            if (tag1 != tag2) {
                res = FALSE;
            } else {
                p1 = JS_VALUE_GET_PTR(op1);
                p2 = JS_VALUE_GET_PTR(op2);
                res = (p1 == p2);
            }
        }
            break;
        case JS_TAG_OBJECT:
            if (tag1 != tag2)
                res = FALSE;
            else
                res = JS_VALUE_GET_OBJ(op1) == JS_VALUE_GET_OBJ(op2);
            break;
        case JS_TAG_INT:
            d1 = JS_VALUE_GET_INT(op1);
            if (tag2 == JS_TAG_INT) {
                d2 = JS_VALUE_GET_INT(op2);
                goto number_test;
            } else if (tag2 == JS_TAG_FLOAT64) {
                d2 = JS_VALUE_GET_FLOAT64(op2);
                goto number_test;
            } else {
                res = FALSE;
            }
            break;
        case JS_TAG_FLOAT64:
            d1 = JS_VALUE_GET_FLOAT64(op1);
            if (tag2 == JS_TAG_FLOAT64) {
                d2 = JS_VALUE_GET_FLOAT64(op2);
            } else if (tag2 == JS_TAG_INT) {
                d2 = JS_VALUE_GET_INT(op2);
            } else {
                res = FALSE;
                break;
            }
        number_test:
            if (unlikely(eq_mode >= JS_EQ_SAME_VALUE)) {
                JSFloat64Union u1, u2;
                /* NaN is not always normalized, so this test is necessary */
                if (isnan(d1) || isnan(d2)) {
                    res = isnan(d1) == isnan(d2);
                } else if (eq_mode == JS_EQ_SAME_VALUE_ZERO) {
                    res = (d1 == d2); /* +0 == -0 */
                } else {
                    u1.d = d1;
                    u2.d = d2;
                    res = (u1.u64 == u2.u64); /* +0 != -0 */
                }
            } else {
                res = (d1 == d2); /* if NaN return false and +0 == -0 */
            }
            goto done_no_free;
#ifdef CONFIG_BIGNUM
            case JS_TAG_BIG_INT:
        {
            bf_t a_s, *a, b_s, *b;
            if (tag1 != tag2) {
                res = FALSE;
                break;
            }
            a = JS_ToBigFloat(ctx, &a_s, op1);
            b = JS_ToBigFloat(ctx, &b_s, op2);
            res = bf_cmp_eq(a, b);
            if (a == &a_s)
                bf_delete(a);
            if (b == &b_s)
                bf_delete(b);
        }
        break;
    case JS_TAG_BIG_FLOAT:
        {
            JSBigFloat *p1, *p2;
            const bf_t *a, *b;
            if (tag1 != tag2) {
                res = FALSE;
                break;
            }
            p1 = JS_VALUE_GET_PTR(op1);
            p2 = JS_VALUE_GET_PTR(op2);
            a = &p1->num;
            b = &p2->num;
            if (unlikely(eq_mode >= JS_EQ_SAME_VALUE)) {
                if (eq_mode == JS_EQ_SAME_VALUE_ZERO &&
                           a->expn == BF_EXP_ZERO && b->expn == BF_EXP_ZERO) {
                    res = TRUE;
                } else {
                    res = (bf_cmp_full(a, b) == 0);
                }
            } else {
                res = bf_cmp_eq(a, b);
            }
        }
        break;
    case JS_TAG_BIG_DECIMAL:
        {
            JSBigDecimal *p1, *p2;
            const bfdec_t *a, *b;
            if (tag1 != tag2) {
                res = FALSE;
                break;
            }
            p1 = JS_VALUE_GET_PTR(op1);
            p2 = JS_VALUE_GET_PTR(op2);
            a = &p1->num;
            b = &p2->num;
            res = bfdec_cmp_eq(a, b);
        }
        break;
#endif
        default:
            res = FALSE;
            break;
    }
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    done_no_free:
    return res;
}

 BOOL js_strict_eq(JSContext *ctx, JSValue op1, JSValue op2)
{
    return js_strict_eq2(ctx, op1, op2, JS_EQ_STRICT);
}

 BOOL js_same_value(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return js_strict_eq2(ctx,
                         JS_DupValue(ctx, op1), JS_DupValue(ctx, op2),
                         JS_EQ_SAME_VALUE);
}

 BOOL js_same_value_zero(JSContext *ctx, JSValueConst op1, JSValueConst op2)
{
    return js_strict_eq2(ctx,
                         JS_DupValue(ctx, op1), JS_DupValue(ctx, op2),
                         JS_EQ_SAME_VALUE_ZERO);
}

 no_inline int js_strict_eq_slow(JSContext *ctx, JSValue *sp,
                                       BOOL is_neq)
{
    BOOL res;
    res = js_strict_eq(ctx, sp[-2], sp[-1]);
    sp[-2] = JS_NewBool(ctx, res ^ is_neq);
    return 0;
}

 __exception int js_operator_in(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    JSAtom atom;
    int ret;

    op1 = sp[-2];
    op2 = sp[-1];

    if (JS_VALUE_GET_TAG(op2) != JS_TAG_OBJECT) {
        JS_ThrowTypeError(ctx, "invalid 'in' operand");
        return -1;
    }
    atom = JS_ValueToAtom(ctx, op1);
    if (unlikely(atom == JS_ATOM_NULL))
        return -1;
    ret = JS_HasProperty(ctx, op2, atom);
    JS_FreeAtom(ctx, atom);
    if (ret < 0)
        return -1;
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    sp[-2] = JS_NewBool(ctx, ret);
    return 0;
}

 __exception int js_has_unscopable(JSContext *ctx, JSValueConst obj,
                                         JSAtom atom)
{
    JSValue arr, val;
    int ret;

    arr = JS_GetProperty(ctx, obj, JS_ATOM_Symbol_unscopables);
    if (JS_IsException(arr))
        return -1;
    ret = 0;
    if (JS_IsObject(arr)) {
        val = JS_GetProperty(ctx, arr, atom);
        ret = JS_ToBoolFree(ctx, val);
    }
    JS_FreeValue(ctx, arr);
    return ret;
}

 __exception int js_operator_instanceof(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    BOOL ret;

    op1 = sp[-2];
    op2 = sp[-1];
    ret = JS_IsInstanceOf(ctx, op1, op2);
    if (ret < 0)
        return ret;
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    sp[-2] = JS_NewBool(ctx, ret);
    return 0;
}

 __exception int js_operator_typeof(JSContext *ctx, JSValueConst op1)
{
    JSAtom atom;
    uint32_t tag;

    tag = JS_VALUE_GET_NORM_TAG(op1);
    switch(tag) {
#ifdef CONFIG_BIGNUM
        case JS_TAG_BIG_INT:
        atom = JS_ATOM_bigint;
        break;
    case JS_TAG_BIG_FLOAT:
        atom = JS_ATOM_bigfloat;
        break;
    case JS_TAG_BIG_DECIMAL:
        atom = JS_ATOM_bigdecimal;
        break;
#endif
        case JS_TAG_INT:
        case JS_TAG_FLOAT64:
            atom = JS_ATOM_number;
            break;
        case JS_TAG_UNDEFINED:
            atom = JS_ATOM_undefined;
            break;
        case JS_TAG_BOOL:
            atom = JS_ATOM_boolean;
            break;
        case JS_TAG_STRING:
            atom = JS_ATOM_string;
            break;
        case JS_TAG_OBJECT:
        {
            JSObject *p;
            p = JS_VALUE_GET_OBJ(op1);
            if (unlikely(p->is_HTMLDDA))
                atom = JS_ATOM_undefined;
            else if (JS_IsFunction(ctx, op1))
                atom = JS_ATOM_function;
            else
                goto obj_type;
        }
            break;
        case JS_TAG_NULL:
        obj_type:
            atom = JS_ATOM_object;
            break;
        case JS_TAG_SYMBOL:
            atom = JS_ATOM_symbol;
            break;
        default:
            atom = JS_ATOM_unknown;
            break;
    }
    return atom;
}

 __exception int js_operator_delete(JSContext *ctx, JSValue *sp)
{
    JSValue op1, op2;
    JSAtom atom;
    int ret;

    op1 = sp[-2];
    op2 = sp[-1];
    atom = JS_ValueToAtom(ctx, op2);
    if (unlikely(atom == JS_ATOM_NULL))
        return -1;
    ret = JS_DeleteProperty(ctx, op1, atom, JS_PROP_THROW_STRICT);
    JS_FreeAtom(ctx, atom);
    if (unlikely(ret < 0))
        return -1;
    JS_FreeValue(ctx, op1);
    JS_FreeValue(ctx, op2);
    sp[-2] = JS_NewBool(ctx, ret);
    return 0;
}
