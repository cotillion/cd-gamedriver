#if defined(PROFILE)
void free_svalue(struct svalue *);
#else /* PROFILE */
static __inline__ void 
free_svalue(struct svalue *v)
{
    if (v->type & T_LVALUE)
	return;
    switch (v->type) {
	case T_NUMBER:
	case T_FLOAT:
	    break;
	case T_STRING:
	    switch (v->string_type) {
		case STRING_MSTRING:
		    free_mstring(v->u.string);
		    break;
		case STRING_SSTRING:
		    free_sstring(v->u.string);
		    break;
		case STRING_CSTRING:
		    break;
	    }
	    break;
	case T_OBJECT:
	    free_object(v->u.ob, "free_svalue");
	    break;
	case T_POINTER:
	    free_vector(v->u.vec);
	    break;
	case T_MAPPING:
	    free_mapping(v->u.map);
	    break;
	case T_FUNCTION:
	    free_closure(v->u.func);
	    break;
	default:
	    fatal("Invalid value of variable!\n");
	    break;
    }
    *v = const0; /* marion - clear this value all away */
}
#endif /* PROFILE */
