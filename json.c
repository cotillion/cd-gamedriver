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
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <setjmp.h>
#include <stdio.h>
#include <json/json.h>

#include "simulate.h"
#include "lint.h"
#include "interpret.h"
#include "mapping.h"
#include "simulate.h"

#define MAX_DEPTH 40

json_object *value_to_json(struct svalue *sp); 

json_object *
string_to_json(struct svalue *sp)
{
    return json_object_new_string(sp->u.string);
}

json_object *
int_to_json(struct svalue *sp)
{
    return json_object_new_int64(sp->u.number);
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

    for (i = 0; i < sp->u.vec->size; i++)
    {
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

    for (i = 0; i < m->size; i++)
    {
        for(p = m->pairs[i]; p; p = p->next)
        {
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

    if (++depth > MAX_DEPTH)
    {
        depth = 0;
        error("Too deep recursion");
    }

    switch(sp->type) {
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
            error("Unsupported data type in val2json");
            break;
    }

    depth--;
    return ret;
}

const char *
val2json(struct svalue *sp)
{
    json_object *json;
    const char *str;

    json = value_to_json(sp);
    str = json_object_to_json_string(json);
    return str;
}
