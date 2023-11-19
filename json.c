/*
 * Copyright (c) 2012 Erik Gävert <erik@gavert.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdio.h>
#include <json-c/json.h>
#include <string.h>

#include "simulate.h"
#include "lint.h"
#include "interpret.h"
#include "mapping.h"
#include "simulate.h"
#include "mstring.h"
#include "object.h"
#include "inline_svalue.h"

#define MAX_DEPTH 40

json_object *value_to_json(struct svalue *sp);
void json_to_value(json_object *ob, struct svalue *ret);

json_object *
string_to_json(struct svalue *sp)
{
    return json_object_new_string(sp->u.string);
}

json_object *
int_to_json(struct svalue *sp)
{
    return json_object_new_int(sp->u.number);
    // TODO: return json_object_new_int64(sp->u.number);
}

json_object *
float_to_json(struct svalue *sp)
{
    return json_object_new_double(sp->u.real);
}


json_object *
array_to_json(struct svalue *sp)
{
    int i = 0;
    json_object *ary = json_object_new_array();

    for (i = 0; i < sp->u.vec->size; i++) {
        json_object_array_add(ary, value_to_json(&sp->u.vec->item[i]));
    }

    return ary;
}

json_object *
mapping_to_json(struct svalue *sp)
{
    int i;
    struct apair *p;
    struct mapping *m = sp->u.map;
    json_object *obj = json_object_new_object();

    for (i = 0; i < m->size; i++) {
        for (p = m->pairs[i]; p; p = p->next) {
            /* Only strings can be keys */
            if (p->arg.type != T_STRING)
                continue;
            json_object_object_add(obj, p->arg.u.string, value_to_json(&p->val));
        }
    }
    return obj;
}

json_object *
value_to_json(struct svalue *sp)
{
    static int depth = 0;
    json_object *ret = NULL;

    if (++depth > MAX_DEPTH) {
        depth = 0;
        error("Too deep recursion\n");
    }

    switch (sp->type) {
    case T_NUMBER:
        ret = int_to_json(sp);
        break;
    case T_STRING:
        ret = string_to_json(sp);
        break;
    case T_POINTER:
        ret = array_to_json(sp);
        break;
    case T_MAPPING:
        ret = mapping_to_json(sp);
        break;
    case T_FLOAT:
        ret = float_to_json(sp);
        break;
    default:
        depth = 0;
        error("Unsupported data type in val2json (%d)\n", sp->type);
        break;
    }

    depth--;
    return ret;
}

char *
val2json(struct svalue *sp)
{
    json_object *json;
    const char *ptr;
    char *str = NULL;

    json = value_to_json(sp);
    ptr = json_object_to_json_string(json);
    str = strdup(ptr);
    json_object_put(json);

    return str;
}

void
json_to_string(json_object *ob, struct svalue *ret)
{
    free_svalue(ret);
    ret->type = T_STRING;
    ret->string_type = STRING_MSTRING;
    ret->u.string = make_mstring(json_object_get_string(ob));
}

void
json_to_int(json_object *ob, struct svalue *ret)
{
    free_svalue(ret);
    ret->type = T_NUMBER;
    ret->u.number = json_object_get_int(ob);
}

void
json_to_float(json_object *ob, struct svalue *ret)
{
    ret->type = T_FLOAT;
    ret->u.real = json_object_get_double(ob);
}

void
json_to_boolean(json_object *ob, struct svalue *ret)
{
    free_svalue(ret);
    ret->type = T_NUMBER;
    ret->u.number = json_object_get_boolean(ob) ? 1 : 0;
}

void
json_to_null(json_object *ob, struct svalue *ret)
{
    ret->type = T_NUMBER;
    ret->u.number = 0;
}

void
json_to_array(json_object *ob, struct svalue *ret)
{
    int arraylen = json_object_array_length(ob);

    free_svalue(ret);
    ret->type = T_POINTER;
    ret->u.vec = allocate_array(arraylen);

    for (int x = 0; x < arraylen; x++) {
        json_object *element = json_object_array_get_idx(ob, x);
        json_to_value(element, &ret->u.vec->item[x]);
    }
}

void
json_to_mapping(json_object *ob, struct svalue *ret)
{
    free_svalue(ret);
    ret->type = T_MAPPING;
    ret->u.map = allocate_map(10);

    json_object_object_foreach(ob, name, val)
    {
        struct svalue key = const0;
        key.type = T_STRING;
        key.string_type = STRING_SSTRING;
        key.u.string = make_sstring(name);
        json_to_value(val, get_map_lvalue(ret->u.map, &key, 1));
        free_svalue(&key);
    }
}

void
json_to_value(json_object *ob, struct svalue *ret)
{
    enum json_type type = json_object_get_type(ob);
    switch (type) {
    case json_type_boolean:
        json_to_boolean(ob, ret);
        break;
    case json_type_double:
        json_to_float(ob, ret);
        break;
    case json_type_int:
        json_to_int(ob, ret);
        break;
    case json_type_string:
        json_to_string(ob, ret);
        break;
    case json_type_object:
        json_to_mapping(ob, ret);
        break;
    case json_type_array:
        json_to_array(ob, ret);
        break;
    case json_type_null:
        json_to_null(ob, ret);
        break;
    default:
        assign_svalue(ret, &const0);
    }
}

struct svalue *
json2val(const char *cp)
{
    static struct svalue ret = {T_NUMBER};

    json_object *jobj = json_tokener_parse(cp);
    if (jobj == NULL) {
        printf("Unable to parse JSON: %s\n", cp);
        return NULL;
    }

    free_svalue(&ret);
    json_to_value(jobj, &ret);
    json_object_put(jobj);
    return &ret;
}
