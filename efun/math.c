/*
 * Math related efuns
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../lang.h"
#include "../interpret.h"
#include "../random.h"

#ifndef LLONG_MAX
#define LLONG_MAX ((long long)(~(unsigned long long)0 >> 1))
#define LLONG_MIN (~LLONG_MAX)
#endif

/* ARGSUSED */
void
f_itof(int num_arg)
{
    sp->type = T_FLOAT;
    sp->u.real = sp->u.number;
}

/* ARGSUSED */
void
f_ftoi(int num_arg)
{
    if (sp->u.real > (double)LLONG_MAX || sp->u.real < (double)LLONG_MIN)
        error("Integer overflow.\n");
    sp->type = T_NUMBER;
    sp->u.number = sp->u.real;
}

/* ARGSUSED */
void
f_sin(int num_arg)
{
    /* CHECK result */
    sp->u.real = sin(sp->u.real);
}

/* ARGSUSED */
void
f_cos(int num_arg)
{
    sp->u.real = cos(sp->u.real);
}

/* ARGSUSED */
void
f_tan(int num_arg)
{
    sp->u.real = tan(sp->u.real);
}

/* ARGSUSED */
void
f_asin(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = asin(arg);
    if (errno)
        error("Argument %.18g to asin() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_acos(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = acos(arg);
    if (errno)
        error("Argument %.18g to acos() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_atan(int num_arg)
{
    sp->u.real = atan(sp->u.real);
}

/* ARGSUSED */
void
f_atan2(int num_arg)
{
    (sp-1)->u.real = atan2((sp-1)->u.real, sp->u.real);
    sp--;
}

/* ARGSUSED */
void
f_exp(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = exp(arg);
    if (errno)
        error("Argument %.18g to exp() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_log(int num_arg)
{
    sp->u.real = log(sp->u.real);
}

/* ARGSUSED */
void
f_pow(int num_arg)
{
    double arg1 = (sp-1)->u.real, arg2 = sp->u.real;
    errno = 0;
    (sp-1)->u.real = pow(arg1, arg2);
    if (errno)
        error("Arguments %.18g and %.18g to pow() are out of bounds.\n", arg1, arg2);
    sp--;
}

/* ARGSUSED */
void
f_sinh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = sinh(arg);
    if (errno)
        error("Argument %.18g to sinh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_cosh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = cosh(arg);
    if (errno)
        error("Argument %.18g to cosh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_tanh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = tanh(arg);
    if (errno)
        error("Argument %.18g to tanh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_asinh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = asinh(arg);
    if (errno)
        error("Argument %.18g to asinh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_acosh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = acosh(arg);
    if (errno)
        error("Argument %.18g to acosh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_atanh(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = atanh(arg);
    if (errno)
        error("Argument %.18g to atanh() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_abs(int num_arg)
{
    if (sp->type == T_NUMBER)
        sp->u.number = llabs(sp->u.number);
    else if (sp->type == T_FLOAT)
        sp->u.real = fabs(sp->u.real);
    else
        bad_arg(1, F_ABS, sp);
}


/* ARGSUSED */
void
f_rnd(int num_arg)
{
    if (num_arg > 0)
    {
        long long seed = sp->u.number;
        pop_stack();
        set_random_seed(seed);
        push_float(random_double());
        clear_random_seed();
    }
    else
        push_float(random_double());
}

/* ARGSUSED */
void
f_nrnd(int num_arg)
{
    double x1, x2, w, m = 0.0, s = 1.0;

    switch (num_arg)
    {
        case 2:
            s = sp->u.real;
            sp--;

        case 1:
            m = sp->u.real;
            sp--;
    }

    do
    {
        x1 = 2.0 * random_double() - 1.0;
        x2 = 2.0 * random_double() - 1.0;
        w = (x1 * x1) + (x2 * x2);
    }
    while (w >= 1.0 || w == 0.0);

    w = sqrt((-2.0 * logl(w)) / w);

    push_float(m + x1 * w * s);
}

/* ARGSUSED */
void
f_ftoa(int num_arg)
{
    char buffer[1024];

    (void)sprintf(buffer,"%.18g",sp->u.real);
    sp--;
    push_string(buffer, STRING_MSTRING);
}

void
f_fact(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = signgam * exp(lgamma(sp->u.real + 1.0));
    if (errno)
        error("Argument %.18g to exp() is out of bounds.\n", arg);
}

/* ARGSUSED */
void
f_sqrt(int num_arg)
{
    double arg = sp->u.real;
    errno = 0;
    sp->u.real = sqrt(arg);
    if (errno)
        error("Argument %.18g to sqrt() is out of bounds.\n", arg);
}
