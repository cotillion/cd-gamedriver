#if defined(PROFILE)
int equal_svalue(const struct svalue *, const struct svalue *);
#else /* PROFILE */
static __inline__ int
equal_svalue(const struct svalue *sval1, const struct svalue *sval2)
{
    if (sval1->type == T_NUMBER && sval1->u.number == 0 &&
	sval2->type == T_OBJECT && sval2->u.ob->flags & O_DESTRUCTED)
	return 1;
    else if (sval2->type == T_NUMBER && sval2->u.number == 0 &&
	     sval1->type == T_OBJECT && sval1->u.ob->flags & O_DESTRUCTED)
	return 1;
    else if (sval1->type == T_NUMBER && sval1->u.number == 0 &&
	     sval2->type == T_FUNCTION && !legal_closure(sval2->u.func))
	return 1;
    else if (sval2->type == T_NUMBER && sval2->u.number == 0 &&
	     sval1->type == T_FUNCTION && !legal_closure(sval1->u.func))
	return 1;
    else if (sval1->type != sval2->type)
	return 0;
    else switch (sval1->type) {
	case T_NUMBER:
	    return sval1->u.number == sval2->u.number;
	case T_POINTER:
	    return sval1->u.vec == sval2->u.vec;
	case T_MAPPING:
	    return sval1->u.map == sval2->u.map;
	case T_STRING:
	    return sval1->u.string == sval2->u.string ||
		   strcmp(sval1->u.string, sval2->u.string) == 0;
	case T_OBJECT:
	    return ((sval1->u.ob->flags & O_DESTRUCTED) && (sval2->u.ob->flags & O_DESTRUCTED)) ||
		   sval1->u.ob == sval2->u.ob;
	case T_FLOAT:
	    return sval1->u.real == sval2->u.real;
	case T_FUNCTION:
	    return sval1->u.func == sval2->u.func;
    }
    return 0;
}
#endif /* PROFILE */
