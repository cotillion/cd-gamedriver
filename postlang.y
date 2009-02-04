%{
#line 3 "postlang.y"

/*
 * These are token values that needn't have an associated code for the
 * compiled file
 */

%}
%right F_ELSE
%token F_WHILE F_DO F_FOR F_STORE F_LAND F_LOR F_STATUS F_IF F_COMMA F_INT 
%token F_CONTINUE F_INHERIT F_COLON_COLON F_VARARGS F_VARARG F_SUBSCRIPT
%token F_BREAK
%token F_OBJECT F_VOID F_MIXED F_PRIVATE F_NO_MASK F_MAPPING F_FLOAT
%token F_PUBLIC F_FUNCTION F_OPERATOR 
%token F_CASE F_DEFAULT

%union
{
	long long number;
	double real;
	unsigned int address;	/* Address of an instruction */
	char *string;
	short type;
	struct { long long key; char block; } case_label;
	struct function *funp;
}
%type <number> assign F_NUMBER constant F_LOCAL_NAME expr_list funop
%type <number> oexpr_list oexpr_list1
%type <number> const1 const2 const3 const4 const5 const6 const7 const8 const9
%type <number> lvalue_list fun_args argument type basic_type optional_star expr_list2
%type <number> type_modifier type_modifier_list opt_basic_type block_or_semi
%type <number> argument_list m_expr_list m_expr_list2
%type <number> new_local_name2
%type <string> identifier identifier2 fun_identifier opt_inherit_name
%type <string> F_IDENTIFIER F_STRING string_con1 string_constant
%type <real> F_FLOATC
%type <case_label> case_label

/* The following symbols return type information */

%type <type> lvalue lvaluec string cast expr01 c_expr
%type <type> comma_expr rvalue funident funexpr opt_expr01
%type <type> expr2 expr211 expr1 expr212 expr213 expr24 expr22 expr23 expr25
%type <type> expr27 expr28 expr3 expr31 expr4 number expr0 real
/* %expect 2 */ /* You can uncomment previous statement if you use bison */
%{
/* has to be here so that F_EXT is defined */
static void store_reloc_data (char, unsigned short, char *name, int, char);
static void ins_f_byte (unsigned int);
static void start_block(void);
static void end_block(void);
static int first_default_arg, current_arg;
static int is_ctor, is_dtor;
static int block_depth;
static int return_label;
#define CTOR_FUNC ((char *)1)
#define DTOR_FUNC ((char *)2)
%}
%%

all: { block_depth = 0; } program ;
	    
program: program def possible_semi_colon
       | /* empty */ ;

possible_semi_colon: /* empty */
                   | ';' { yyerror("Extra ';'. Ignored."); };

inheritance: type_modifier_list F_INHERIT string_constant opt_inherit_name ';'
		{
		    struct object *ob;
		    
                    if (mem_block[A_VARIABLES].current_size ||
		        mem_block[A_FUNCTIONS].current_size)
                        yyerror("Inherit after other definitions\n");

		    ob = find_object2($3);
		    if (ob == 0) {
			if (inherit_file)
			    free(inherit_file);
			inherit_file = string_copy($3);
			/* Return back to load_object() */
			YYACCEPT;
		    }
                    if (ob->prog->flags & PRAGMA_NO_INHERIT)
                    {
                        yyerror("Inheriting bad object.\n");
                    }
		    if (!check_inherits(ob->prog))
                    {
			if (inherit_file)
			    free(inherit_file);
			inherit_file = string_copy($3);
			/* Return back to load_object() */
			YYACCEPT;
                    }
		    copy_inherits(ob->prog, $1, $4);
		};

opt_inherit_name: /* empty */
                {
                    char *n = strrchr($<string>0, '/');
                    
                    if (n)
                    {
                        char *s = tmpstring_copy(n + 1);
                        char *p = strchr(s, '.');

                        if (p)
                        {
                            *p = '\0';
                            $$ = tmpstring_copy(s);
                        }
                        $$ = s;
                    }
                    else
                        $$ = tmpstring_copy($<string>0);
                }
                | F_IDENTIFIER
                {
                    $$ = $1;
                } ;

number: F_NUMBER
	{
	    if ( $1 == 0 ) {
		ins_f_byte(F_CONST0); $$ = TYPE_ANY;
	    } else if ( $1 == 1 ) {
		ins_f_byte(F_CONST1); $$ = TYPE_NUMBER;
	    } else {
		ins_f_byte(F_NUMBER); ins_mem(&$1, sizeof($1)); $$ = TYPE_NUMBER;
	    }
	} ;

optional_star: /* empty */ { $$ = 0; } | '*' { $$ = TYPE_MOD_POINTER; } ;

block_or_semi: block { $$ = 0; }
         | ';' { $$ = ';'; } 
         | '=' string_con1 ';' { $<string>$ = $<string>2; }
	 | '=' F_NUMBER ';'
         {
	     if ($2 != 0)
	     {
		 yyerror("Syntax error");
	     }
	     $$ = 1;
	 } ;

def: type optional_star fun_identifier
	{
	    /* Save start of function. */
	    clear_init_arg_stack();
	    deref_index = 0;
	    push_address();
	    try_level = break_try_level = continue_try_level = 0;
	    true_varargs = 0;
	    first_default_arg = current_arg = 0;
	    is_ctor = is_dtor = 0;

	    return_label = make_label();
	    if ($3 == CTOR_FUNC)
	    {
		$3 = tmpstring_copy(".CTOR");
		is_ctor = 1;
	    }
	    else if ($3 == DTOR_FUNC)
	    {
		$3 = tmpstring_copy(".DTOR");
		is_dtor = 1;
	    }

	    if (is_ctor | is_dtor)
	    {
		if ($1 | $2)
		{
		    yyerror("Constructors and destructors don't have a type");
		    $1 = $2 = 0;
		}
		if (pragma_strict_types)
		{
		    $1 = exact_types = TYPE_VOID;
		}
		else
		    exact_types = 0;
	    }
	    else if ($1 & TYPE_MASK)
	    {
		exact_types = $1 | $2;
	    } 
	    else 
	    {
		if (pragma_strict_types)
		    yyerror("\"#pragma strict_types\" requires type of function");
		exact_types = 0;
	    }
	}
	fun_args
	{
	    /*
	     * Define a prototype. If it is a real function, then the
	     * prototype will be replaced below.
	     */
	    if ($5 && is_ctor | is_dtor)
	    {
		yyerror("Constructors and destructors don't take arguments");
	    }

	    ins_f_byte(F_JUMP);
	    push_address();
	    ins_short(0);
	    {
	        int i;
	        unsigned short j;
		i = pop_address();
		push_address();
		dump_init_arg_table($5);
		j = mem_block[A_PROGRAM].current_size;
		upd_short(i, (short)mem_block[A_PROGRAM].current_size);
		((char *)mem_block[A_PROGRAM].block)[i] = ((char *)&j)[0];
		((char *)mem_block[A_PROGRAM].block)[i + 1] = ((char *)&j)[1];
		define_new_function($3, (char)$5, 0, 0,
				    NAME_PROTOTYPE | $1 | $2 | true_varargs,
				    (char)first_default_arg);
	    }
	}
        block_or_semi
	{
		  
	    /* Either a prototype or a block */
	    if ($7 == ';') /* its only a prototype */
	    {
		(void)pop_address(); /* Not used here */
		mem_block[A_PROGRAM].current_size = 
		    pop_address(); 
	    } 
	    else if ($7 == 0)
	    {
		define_new_function($3, (char)$5,
				    (unsigned char)(max_number_of_locals - $5),
				    pop_address(), $1 | $2 | true_varargs,
				    (char)first_default_arg);
		ins_f_byte(F_CONST0);
		set_label(return_label, (short)mem_block[A_PROGRAM].current_size);
		ins_f_byte(F_RETURN);
		(void)pop_address(); /* Not used here */
	    }
	    else if ($7 == 1)
	    {
		if (is_ctor | is_dtor)
		    yyerror("Constructors and destructors can't be abstract");
		define_new_function($3, (char)$5, 0, 0,
				    NAME_ABSTRACT | $1 | $2 | true_varargs,
				    (char)first_default_arg);
		yyerror("Abstract functions aren't implemented yet");
	    }
	    else
            {
		extern void (* get_C_fun_address(char *, char *))(struct svalue *);
		void (*funca)(struct svalue *);

		define_new_function($3, (char)$5,
				    (unsigned char)(max_number_of_locals - $5),
				    pop_address(), $1 | $2 | true_varargs,
				    (char)first_default_arg);
		ins_f_byte(F_CALL_C);
		funca = get_C_fun_address(current_file, $<string>7);
		if (!funca) 
                {
		    char buf[256];

		    (void)snprintf(buf, sizeof(buf), "Undefined interface %s() = \"%s\".\n",
			    $3, $<string>7);
		    yyerror(buf);
		}
		ins_mem(&funca, sizeof(funca));
		ins_f_byte(F_RETURN);
		(void)pop_address(); /* Not used here */
	    }
	    free_all_local_names();
	    is_ctor = is_dtor = 0;
	}
   | type name_list ';' { if ($1 == 0) yyerror("Missing type"); }
   | inheritance ;

new_arg_name: type optional_star F_IDENTIFIER
	{
	    if (exact_types && $1 == 0) {
		yyerror("Missing type for argument");
		(void)add_local_name($3, TYPE_ANY);	/* Supress more errors */
	    } else {
		(void)add_local_name($3, $1 | $2);
	    }
	    if (first_default_arg != current_arg)
		yyerror("non default argument defined after default one");
	    else
		first_default_arg++;
	    push_init_arg_address(mem_block[A_PROGRAM].current_size);
	} 
          | type optional_star F_IDENTIFIER
          {
              int var_num;
	      if (exact_types && $1 == 0) {
		  yyerror("Missing type for argument");
		  var_num = add_local_name($3, TYPE_ANY);	/* Supress more errors */
	      } else {
		  var_num = add_local_name($3, $1 | $2);
	      }
	      push_init_arg_address(mem_block[A_PROGRAM].current_size);
	      ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
	      ins_byte((char)var_num);
	  }
          '=' expr0
          {
	      if (!compatible_types((($1 ? $1 : TYPE_ANY) | $2) & TYPE_MASK, $6)){
		  char buff[100];

		  (void)snprintf(buff, sizeof(buff), "Type mismatch %s when initializing %s",
			  get_two_types($1 | $2, $6), $3);
		  yyerror(buff);
	      }
	      ins_f_byte(F_ASSIGN);
	      ins_f_byte(F_POP_VALUE);
	  } 

fun_args: '(' /* empty */ ')'	{ $$ = 0; }
	| '(' argument ')'	{ $$ = $2; }
	| '(' F_VOID ')'	{ $$ = 0; };

argument:   argument_list
	  | argument_list ',' F_VARARG
	  {
              int var_num = add_local_name(tmpstring_copy("argv"), TYPE_ANY | TYPE_MOD_POINTER);
	      $$ = $1 + 1;
              true_varargs = TYPE_MOD_TRUE_VARARGS;
	      push_init_arg_address(mem_block[A_PROGRAM].current_size);
	      ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
	      ins_byte((char)var_num);
	      ins_f_byte(F_AGGREGATE);
	      ins_short(0);
	      ins_f_byte(F_ASSIGN);
	      ins_f_byte(F_POP_VALUE);
	      current_arg++;
	  }
	  | F_VARARG
	  {
              int var_num = add_local_name(tmpstring_copy("argv"), TYPE_ANY | TYPE_MOD_POINTER);
	      $$ = 1;
              true_varargs = TYPE_MOD_TRUE_VARARGS;
	      push_init_arg_address(mem_block[A_PROGRAM].current_size);
	      ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
	      ins_byte((char)var_num);
	      ins_f_byte(F_AGGREGATE);
	      ins_short(0);
	      ins_f_byte(F_ASSIGN);
	      ins_f_byte(F_POP_VALUE);
	      current_arg++;
	  };

argument_list: new_arg_name { $$ = 1; current_arg++; }
	     | argument_list ',' new_arg_name { $$ = $1 + 1; current_arg++; } ;

type_modifier: F_NO_MASK { $$ = TYPE_MOD_NO_MASK; }
	     | F_STATIC { $$ = TYPE_MOD_STATIC; }
	     | F_PRIVATE { $$ = TYPE_MOD_PRIVATE; }
	     | F_PUBLIC { $$ = TYPE_MOD_PUBLIC; }
	     | F_VARARGS { $$ = TYPE_MOD_VARARGS; } ;

type_modifier_list: /* empty */ { $$ = 0; }
		  | type_modifier type_modifier_list { $$ = $1 | $2; } ;

type: type_modifier_list opt_basic_type { $$ = $1 | $2; current_type = $$; } ;

cast: '(' basic_type optional_star ')'
	{
	    $$ = $2 | $3;
	} ;

opt_basic_type: basic_type | /* empty */ { $$ = TYPE_UNKNOWN; } ;

basic_type: F_STATUS { $$ = TYPE_NUMBER; current_type = $$; }
	| F_INT { $$ = TYPE_NUMBER; current_type = $$; }
	| F_STRING_DECL { $$ = TYPE_STRING; current_type = $$; }
	| F_OBJECT { $$ = TYPE_OBJECT; current_type = $$; }
	| F_VOID {$$ = TYPE_VOID; current_type = $$; }
	| F_MIXED { $$ = TYPE_ANY; current_type = $$; } 
	| F_MAPPING { $$ = TYPE_MAPPING; current_type = $$; }
        | F_FLOAT { $$ = TYPE_FLOAT; current_type = $$; }
        | F_FUNCTION { $$ = TYPE_FUNCTION; current_type = $$; };

name_list: new_name
	 | new_name ',' name_list;

new_name: optional_star F_IDENTIFIER
	{
	    define_variable($2, current_type | $1);
	}
| optional_star F_IDENTIFIER
	{
	    define_variable($2, current_type | $1);
	    transfer_init_control();
	    ins_f_byte(F_PUSH_IDENTIFIER_LVALUE);
	    ins_byte((char)variable_inherit_found);
	    ins_byte((char)variable_index_found);
	}
	'=' expr0
	{
	    if (pragma_strict_types &&
		!compatible_types((current_type | $1) & TYPE_MASK, $5)){
		char buff[100];

		(void)snprintf(buff, sizeof(buff), "Type mismatch %s when initializing %s",
			get_two_types(current_type | $1, $5), $2);
		yyerror(buff);
	    }
	    ins_f_byte(F_ASSIGN);
	    ins_f_byte(F_POP_VALUE);
	    add_new_init_jump();
	} ;

block: '{'
        {
		start_block();
        }
        statements '}'
        {
		end_block();
        };

local_declaration: basic_type local_name_list ';' { ;} ;

new_local_name2: optional_star F_IDENTIFIER
        {
            $$ = add_local_name($2, current_type | $1);
        }

new_local_name: new_local_name2
        { /* To shut bison up */ }
        | new_local_name2
        {
            ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
            ins_byte((char)$1);
        }
        '=' expr0
        {
            if (exact_types &&
                !compatible_types(type_of_locals[$1] & TYPE_MASK, $4))
            {
                char buff[100];

                (void)snprintf(buff, sizeof(buff),
			       "Type mismatch %s when initializing %s",
			       get_two_types(type_of_locals[$1], $4),
			       local_names[$1]);
                yyerror(buff);
            }
            ins_f_byte(F_ASSIGN);
            ins_f_byte(F_POP_VALUE);
        };


local_name_list: new_local_name
	| new_local_name ',' local_name_list ;

statements: /* empty */
	  | statement statements
	  | error ';' ;

statement: comma_expr ';'
	{
	    ins_f_byte(F_POP_VALUE);
	    /* if (exact_types && !TYPE($1,TYPE_VOID))
		yyerror("Value thrown away"); */
	}
	 | cond | try | while | do | for | switch | case | default | return ';'
	 | foreach | foreach_m | block | local_declaration
  	 | /* empty */ ';'
	 | F_BREAK ';'	/* This code is a jump to a jump */
		{
		    if (current_break_address == 0)
			yyerror("break statement outside loop");
		    for (int i = try_level; i > break_try_level; i--)
			ins_f_byte(F_END_TRY);
		    ins_f_byte(F_JUMP);
		    add_jump();
		    ins_label(current_break_address);
		}
	 | F_CONTINUE ';'	/* This code is a jump to a jump */
		{
		    if (current_continue_address == 0)
			yyerror("continue statement outside loop");
		    for (int i = try_level; i > continue_try_level; i--)
			ins_f_byte(F_END_TRY);
		    ins_f_byte(F_JUMP);
		    add_jump();
		    ins_label(current_continue_address);
		}
         ;

try: F_TRY
        {
	    ins_f_byte(F_TRY);
	    push_address();
	    ins_short(0);
	    ins_byte(0);
	    try_level++;
	} statement F_CATCH {start_block();} '(' basic_type new_local_name2 ')'
        {
	    short try_start = pop_address();	    
	    try_level--;
	    ins_f_byte(F_END_TRY);
	    ins_f_byte(F_JUMP);
	    push_address();
	    ins_short(0);
	    upd_short(try_start,
		      (short)mem_block[A_PROGRAM].current_size);
	    upd_byte(try_start + 2, $8);
	} statement
	{
	    end_block();
	    upd_short(pop_address(),
		      (short)mem_block[A_PROGRAM].current_size);
	};


while:  {   push_explicit(current_continue_address);
	    push_explicit(continue_try_level);
	    push_explicit(current_break_address);
	    push_explicit(break_try_level);
	    current_continue_address = make_label();
	    current_break_address = make_label();
	    continue_try_level = break_try_level = try_level;
	    set_label(current_continue_address, (short)mem_block[A_PROGRAM].current_size);
	} F_WHILE '(' comma_expr ')'
	{
	    ins_f_byte(F_JUMP_WHEN_ZERO);
	    add_jump();
	    ins_label(current_break_address);
	}
       statement
	{
	  ins_f_byte(F_JUMP);
	  add_jump();
	  ins_label(current_continue_address);
	  set_label(current_break_address, (short)mem_block[A_PROGRAM].current_size);
	  break_try_level = pop_address();
	  current_break_address = pop_address();
	  continue_try_level = pop_address();
	  current_continue_address = pop_address();
        };

do: {
        push_explicit(current_continue_address);
	push_explicit(continue_try_level);
	push_explicit(current_break_address);
	push_explicit(break_try_level);
	current_continue_address = make_label();
	current_break_address = make_label();
	continue_try_level = break_try_level = try_level;
        push_address();
    }
    F_DO statement
    {
	set_label(current_continue_address, (short)mem_block[A_PROGRAM].current_size);
    }
    F_WHILE '(' comma_expr ')' ';'
    {
	ins_f_byte(F_JUMP_WHEN_NON_ZERO);
	add_jump();
	ins_short((short)pop_address());
	set_label(current_break_address, (short)mem_block[A_PROGRAM].current_size);
	break_try_level = pop_address();
	current_break_address = pop_address();
	continue_try_level = pop_address();
	current_continue_address = pop_address();
    };

start_block:
    { start_block();} ;

foreach_var: basic_type new_local_name2	{
            ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
            ins_byte((char)$2);
	} ;

foreach_m: F_FOREACH start_block '(' foreach_var ',' foreach_var ':' comma_expr ')' {
          ins_f_byte(F_DUP);
          ins_f_byte(F_M_INDEXES);
	  ins_f_byte(F_CONST0);
          push_explicit(current_continue_address);
	  push_explicit(continue_try_level);
	  push_explicit(current_break_address);
	  push_explicit(break_try_level);
	  current_continue_address = make_label();
	  current_break_address = make_label();
	  continue_try_level = break_try_level = try_level;
	  set_label(current_continue_address, get_address());
	  ins_f_byte(F_FOREACH_M);
          add_jump();
	  ins_label(current_break_address);
	}
	statement 
	{
            ins_f_byte(F_JUMP);
            add_jump();
            ins_label(current_continue_address);
            set_label(current_break_address, get_address());
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            end_block();
	    break_try_level = pop_address();
            current_break_address = pop_address();
	    continue_try_level = pop_address();
            current_continue_address = pop_address();
	};

foreach: F_FOREACH start_block '(' foreach_var ':' comma_expr ')' {
	  ins_f_byte(F_CONST0);
          push_explicit(current_continue_address);
	  push_explicit(continue_try_level);
	  push_explicit(current_break_address);
	  push_explicit(break_try_level);
	  current_continue_address = make_label();
	  current_break_address = make_label();
	  continue_try_level = break_try_level = try_level;
	  set_label(current_continue_address, get_address());
	  ins_f_byte(F_FOREACH);
          add_jump();
	  ins_label(current_break_address);
	}
	statement 
	{
            ins_f_byte(F_JUMP);
            add_jump();
            ins_label(current_continue_address);
            set_label(current_break_address, get_address());
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            ins_f_byte(F_POP_VALUE);
            end_block();
	    break_try_level = pop_address();
            current_break_address = pop_address();
	    continue_try_level = pop_address();
            current_continue_address = pop_address();
	};

for_init_expr: for_expr
	| basic_type local_name_list {;};

for: F_FOR '('	start_block  { 
    push_explicit(current_continue_address);
    push_explicit(continue_try_level);
    push_explicit(current_break_address);
    push_explicit(break_try_level);
    continue_try_level = break_try_level = try_level;
    } for_init_expr ';' {
		      push_address();
		  }
     for_guard ';' {
		    push_address();
		    ins_short(0);
		    current_continue_address = make_label();
		    current_break_address = make_label();
		    ins_f_byte(F_JUMP);
		    add_jump();
		    ins_label(current_break_address);
		    set_label(current_continue_address, (short)mem_block[A_PROGRAM].current_size);
		  }
     for_expr ')' {
		    upd_short(pop_address(), (short)(mem_block[A_PROGRAM].current_size + 3));
		    ins_f_byte(F_JUMP);
		    add_jump();
		    ins_short((short)pop_address());
		  }
     statement
   {
       ins_f_byte(F_JUMP);
       add_jump();
       ins_label(current_continue_address);
       set_label(current_break_address, (short)mem_block[A_PROGRAM].current_size);
       end_block();
       break_try_level = pop_address();
       current_break_address = pop_address();
       continue_try_level = pop_address();
       current_continue_address = pop_address();
   };

for_guard: /* EMPTY */
	{
	    ins_f_byte(F_JUMP);
	    add_jump();
	}
    | comma_expr
	{
	    ins_f_byte(F_JUMP_WHEN_NON_ZERO);
	    add_jump();
	}
    ;

for_expr: /* EMPTY */
        | comma_expr { ins_f_byte(F_POP_VALUE); } ;

switch: F_SWITCH '(' comma_expr ')'
      {
	  /* initialize switch */
	  push_explicit(current_case_number_heap);
	  push_explicit(current_case_string_heap);
	  push_explicit(zero_case_label);
	  push_explicit(current_break_address);
	  push_explicit(break_try_level);
	  break_try_level = try_level;
	  push_explicit(current_case_address);

	  ins_f_byte(F_SWITCH);
	  current_case_address = mem_block[A_PROGRAM].current_size;
	  ins_byte((char)0xff); /* kind of table */
	  current_case_number_heap = mem_block[A_CASE_NUMBERS].current_size;
	  current_case_string_heap = mem_block[A_CASE_STRINGS].current_size;
	  zero_case_label = NO_STRING_CASE_LABELS;
	  ins_short(0); /* address of table */
	  ins_short(0); /* break address to push, table is entered before */
	  ins_short(0); /* default address */

	  current_break_address = make_label();
      }
      statement
      {
	  int block_index;
	  int current_case_heap;
	  short default_addr;

	  /* Wrap up switch */
	  /* It is not unusual that the last case/default has no break 
	   */
	  /* Default address */
	  if (!(default_addr = read_short(current_case_address + 5)))
	  {
	      upd_short(current_case_address + 5,   /* no default given ->  */
			(short)mem_block[A_PROGRAM].current_size);  /* create one   */
	      default_addr = mem_block[A_PROGRAM].current_size;
	  }

	  ins_f_byte(F_JUMP);
	  add_jump();
	  ins_label(current_break_address);

	  /* Table address */
	  upd_short(current_case_address + 1,
		    (short)mem_block[A_PROGRAM].current_size);

	  /* What type of table? */
	  if (zero_case_label & (NO_STRING_CASE_LABELS|SOME_NUMERIC_CASE_LABELS))
	  {
	      block_index = A_CASE_NUMBERS;
	      current_case_heap = current_case_number_heap;
	      mem_block[A_PROGRAM].block[current_case_address] = 0;
	  } 
	  else 
	  {
	      block_index = A_CASE_STRINGS;
	      current_case_heap = current_case_string_heap;
	      mem_block[A_PROGRAM].block[current_case_address] = 1;
	      if (zero_case_label & 0xffff) 
	      {
		  ins_long_long(0); /* not used */
		  ins_short((short)zero_case_label); /* the address */
	      }
	      else
	      {
		  ins_long_long(0); /* not used */
		  ins_short(default_addr); /* the address */
	      }
	  }
	  
	  /* Dump the table */
	  {
	      struct case_heap_entry *ent, *end;

	      ent = (struct case_heap_entry *)
		  (mem_block[block_index].block + current_case_heap);
	      end = (struct case_heap_entry *)
		  (mem_block[block_index].block +
		   mem_block[block_index].current_size);
	      for (; ent < end; ent++)
	      {
		  ins_long_long(ent->key);
		  ins_short(ent->addr);
	      }
	  }

	  /* Break address */
	  upd_short(current_case_address + 3, 
		    (short)mem_block[A_PROGRAM].current_size);
	  set_label(current_break_address, 
		    (short)mem_block[A_PROGRAM].current_size);

	  mem_block[A_CASE_NUMBERS].current_size = current_case_number_heap;
	  mem_block[A_CASE_STRINGS].current_size = current_case_string_heap;
	  current_case_address = pop_address();
	  break_try_level = pop_address();
	  current_break_address = pop_address();
	  zero_case_label = pop_address();
	  current_case_string_heap = pop_address();
	  current_case_number_heap = pop_address();
      };


case: F_CASE case_label ':'
    {
	/* Register label */
	struct case_heap_entry temp;

	if ( !current_case_address ) 
	{
	    yyerror("Case outside switch");
	    break;
	}
	temp.key = $2.key;
	temp.addr = mem_block[A_PROGRAM].current_size;
	temp.line = current_line;
	add_to_case_heap($2.block,&temp, NULL);

    }
    | F_CASE case_label F_RANGE case_label ':'
    {
	/* Register range label */
	struct case_heap_entry temp1, temp2;

	if ( !current_case_address) 
	{
	    yyerror("Case outside switch");
	    break;
	}

	if ($2.block != $4.block)
	    yyerror("Incompatible types in case label range bounds");
	else
	{
	    temp1.key = $2.key;
	    temp1.addr = -1;
	    temp1.line = current_line;
	    temp2.key = $4.key;
	    temp2.addr = mem_block[A_PROGRAM].current_size;
	    temp2.line = current_line;
	    add_to_case_heap($2.block, &temp1, &temp2);
	}
    };


case_label: constant
        {
	    if ( !(zero_case_label & NO_STRING_CASE_LABELS) )
		yyerror("Mixed case label list not allowed");
	    if ( ($$.key = $1) != 0 )
	        zero_case_label |= SOME_NUMERIC_CASE_LABELS;
	    else
		zero_case_label |= mem_block[A_PROGRAM].current_size;
	    $$.block = A_CASE_NUMBERS;
	}
          | string_constant
        {
	    if ( zero_case_label & SOME_NUMERIC_CASE_LABELS )
		yyerror("Mixed case label list not allowed");
	    zero_case_label &= ~NO_STRING_CASE_LABELS;
            $$.key = (unsigned short)store_prog_string($1);
	    $$.block = A_CASE_STRINGS;
        };


constant: const1
	| constant '|' const1 { $$ = $1 | $3; } ;

const1: const2
      | const1 '^' const2 { $$ = $1 ^ $3; } ;

const2: const3
      | const2 '&' const3 { $$ = $1 & $3; } ;

const3: const4
      | const3 F_EQ const4 { $$ = $1 == $3; }
      | const3 F_NE const4 { $$ = $1 != $3; } ;

const4: const5
      | const4 '>'  const5 { $$ = $1 >  $3; }
      | const4 F_GE const5 { $$ = $1 >= $3; }
      | const4 '<'  const5 { $$ = $1 <  $3; }
      | const4 F_LE const5 { $$ = $1 <= $3; } ;

const5: const6
      | const5 F_LSH const6 { $$ = $1 << $3; }
      | const5 F_RSH const6 { $$ = (unsigned)$1 >> $3; } ;

const6: const7
      | const6 '+' const7 { $$ = $1 + $3; }
      | const6 '-' const7 { $$ = $1 - $3; } ;

const7: const8
      | const7 '*' const8 { $$ = $1 * $3; }
      | const7 F_MOD const8 { $$ = $1 % $3; }
      | const7 '/' const8 { $$ = $1 / $3; } ;

const8: const9
      | '(' constant ')' { $$ = $2; } ;

const9: F_NUMBER
      | '-'   F_NUMBER { $$ = -$2; }
      | F_NOT F_NUMBER { $$ = !$2; }
      | '~'   F_NUMBER { $$ = ~$2; } ;

default: F_DEFAULT ':'
    {
	if ( !current_case_address) 
        {
	    yyerror("Default outside switch");
	    break;
	}
	if ( read_short(current_case_address + 5 ) )
	    yyerror("Duplicate default");
	upd_short(current_case_address + 5, 
            (short)mem_block[A_PROGRAM].current_size);
    } ;


comma_expr: c_expr
     | F_THROW comma_expr { ins_f_byte(F_THROW); $$ = TYPE_NONE; } ;

c_expr: expr0 { $$ = $1; }
          | c_expr { ins_f_byte(F_POP_VALUE); }
	',' expr0
	{ $$ = $4; } ;

expr0:  expr01
     | lvalue assign expr0
	{
	    deref_index--;
	    if (!($1 & TYPE_LVALUE))
		yyerror("Illegal LHS");
	    $1 &= ~TYPE_LVALUE;
	    if (exact_types && !compatible_types($1, $3) &&
		!($1 == TYPE_STRING && $3 == TYPE_NUMBER && $2 == F_ADD_EQ) &&
		!(($1 == TYPE_STRING || ($1 & TYPE_MOD_POINTER)) &&
		  $3 == TYPE_NUMBER && $2 == F_MULT_EQ))
		type_error("Bad assignment. Rhs", $3);
	    ins_f_byte((unsigned)$2);
	    $$ = $3;
	}
     | error assign expr01 { yyerror("Illegal LHS"); $$ = TYPE_ANY; };

opt_expr01: { $$ = TYPE_NONE;}
          | expr01 ;

expr01: expr1 { $$ = $1; }
     | expr1 '?'
	{
	    push_address();
	    ins_f_byte(F_JUMP_WHEN_ZERO);
	    add_jump();
	    push_address();
	    ins_short(0);
	}
	opt_expr01
	{
	    int i;
	    i = pop_address();
	    if ($4 != TYPE_NONE)
	    {
	      (void)pop_address();
	      ins_f_byte(F_JUMP);
	      add_jump();
	      push_address();
	      ins_short(0);
	      upd_short(i, (short)mem_block[A_PROGRAM].current_size);
	    }
	    else
	    {
	      mem_block[A_PROGRAM].current_size = pop_address();
	      ins_f_byte(F_SKIP_NZ);
	      push_address();
	      ins_short(0);
	      $4 = $1;
	    }
	}
      ':' expr01
	{
	    upd_short(pop_address(), (short)mem_block[A_PROGRAM].current_size);
	    if (exact_types && !compatible_types($4, $7)) {
		type_error("Different types in ?: expr", $4);
		type_error("                      and ", $7);
	    }
	    if ($4 == TYPE_ANY) $$ = $7;
	    else if ($7 == TYPE_ANY) $$ = $4;
	    else if (TYPE($4, TYPE_MOD_POINTER|TYPE_ANY)) $$ = $7;
	    else if (TYPE($7, TYPE_MOD_POINTER|TYPE_ANY)) $$ = $4;
	    else $$ = $4;
	};

assign: '=' { $$ = F_ASSIGN; }
      | F_AND_EQ { $$ = F_AND_EQ; }
      | F_OR_EQ { $$ = F_OR_EQ; }
      | F_XOR_EQ { $$ = F_XOR_EQ; }
      | F_LSH_EQ { $$ = F_LSH_EQ; }
      | F_RSH_EQ { $$ = F_RSH_EQ; }
      | F_ADD_EQ { $$ = F_ADD_EQ; }
      | F_SUB_EQ { $$ = F_SUB_EQ; }
      | F_MULT_EQ { $$ = F_MULT_EQ; }
      | F_MOD_EQ { $$ = F_MOD_EQ; }
      | F_DIV_EQ { $$ = F_DIV_EQ; };

return: F_RETURN
	{
	    if (exact_types && !TYPE(exact_types, TYPE_VOID))
		type_error("Must return a value for a function declared",
			   exact_types);
	    ins_f_byte(F_CONST0);
	    for (int i = try_level;i > 0; i--)
		ins_f_byte(F_END_TRY);
	    ins_f_byte(F_JUMP);
	    add_jump();
	    ins_label(return_label);
	}
      | F_RETURN comma_expr
	{
	    if (exact_types && !TYPE($2, exact_types & TYPE_MASK))
		type_error("Return type not matching", exact_types);
	    for (int i = try_level; i > 0; i--)
		ins_f_byte(F_END_TRY);
	    ins_f_byte(F_JUMP);
	    add_jump();
	    ins_label(return_label);
	};

expr_list: /* empty */		{ $$ = 0; }
	 | expr_list2		{ $$ = $1; }
	 | expr_list2 ','	{ $$ = $1; } ; /* Allow a terminating comma */

expr_list2: expr0		{ $$ = 1; add_arg_type($1); }
         | expr_list2 ',' expr0	{ $$ = $1 + 1; add_arg_type($3); } ;

m_expr_list: /* empty */	{ $$ = 0; }
	 | m_expr_list2		{ $$ = $1; }
         | m_expr_list2 ','	{ $$ = $1; } ; /* Allow a terminating comma */

m_expr_list2: expr0 ':' expr1	{ $$ = 2; add_arg_type($1); add_arg_type($3); }
         | m_expr_list2 ',' expr0 ':' expr1 { $$ = $1 + 2; add_arg_type($3); add_arg_type($5); };

expr1: expr2 { $$ = $1; }
     | expr2 F_LOR
	{
	    ins_f_byte(F_DUP);
	    ins_f_byte(F_JUMP_WHEN_NON_ZERO);
	    add_jump();
	    push_address();
	    ins_short(0);
	    ins_f_byte(F_POP_VALUE);
	}
       expr1
	{
	    upd_short(pop_address(), (short)mem_block[A_PROGRAM].current_size);
	    if ($1 == $4)
		$$ = $1;
	    else
		$$ = TYPE_ANY;	/* Return type can't be known */
	};

expr2: expr211 { $$ = $1; }
     | expr211 F_LAND
	{
	    ins_f_byte(F_DUP);
	    ins_f_byte(F_JUMP_WHEN_ZERO);
	    add_jump();
	    push_address();
	    ins_short(0);
	    ins_f_byte(F_POP_VALUE);
	}
       expr2
	{
	    upd_short(pop_address(), (short)mem_block[A_PROGRAM].current_size);
	    if ($1 == $4)
		$$ = $1;
	    else
		$$ = TYPE_ANY;	/* Return type can't be known */
	} ;

expr211: expr212
       | expr211 '|' expr212
          {
	    int t1 = $1 & TYPE_MASK, t3 = $3 & TYPE_MASK;
	    if (($1 & TYPE_MOD_POINTER && $3 & TYPE_MOD_POINTER) && 
		 (t1 == t3))
	      $$ = $1;
	    else  if (((t1 == TYPE_ANY) && $3 & TYPE_MOD_POINTER) || 
		($1 & TYPE_MOD_POINTER && (t3 == TYPE_ANY)))
	      $$ = TYPE_MOD_POINTER | TYPE_ANY;
	    else if ((t1 == TYPE_ANY) && (t3 == TYPE_ANY))
	      $$ = TYPE_ANY;
	    else
	    {
	      if (exact_types && !TYPE($1,TYPE_NUMBER))
		  type_error("Bad argument 1 to |", $1);
	      if (exact_types && !TYPE($3,TYPE_NUMBER))
		  type_error("Bad argument 2 to |", $3);
	      $$ = TYPE_NUMBER;
	    }
	    ins_f_byte(F_OR);
	  };

expr212: expr213
       | expr212 '^' expr213
	  {
	      if (exact_types && !TYPE($1,TYPE_NUMBER))
		  type_error("Bad argument 1 to ^", $1);
	      if (exact_types && !TYPE($3,TYPE_NUMBER))
		  type_error("Bad argument 2 to ^", $3);
	      $$ = TYPE_NUMBER;
	      ins_f_byte(F_XOR);
	  };

expr213: expr22
       | expr213 '&' expr22
	  {
	    int t1 = $1 & TYPE_MASK, t3 = $3 & TYPE_MASK;
	    if (($1 & TYPE_MOD_POINTER && $3 & TYPE_MOD_POINTER) && 
		 (t1 == t3))
	      $$ = $1;
	    else  if (((t1 == TYPE_ANY) && $3 & TYPE_MOD_POINTER) || 
		($1 & TYPE_MOD_POINTER && (t3 == TYPE_ANY)))
	      $$ = TYPE_MOD_POINTER | TYPE_ANY;
	    else if ((t1 == TYPE_ANY) && (t3 == TYPE_ANY))
	      $$ = TYPE_ANY;
	    else
	    {
	      if (exact_types && !TYPE($1,TYPE_NUMBER))
		type_error("Bad argument 1 to &", $1);
	      if (exact_types && !TYPE($3,TYPE_NUMBER))
		type_error("Bad argument 2 to &", $3);
	      $$ = TYPE_NUMBER;
	    }
	    ins_f_byte(F_AND);
	  };

expr22: expr23
      | expr24 F_EQ expr24
	{
	    int t1 = $1 & TYPE_MASK, t2 = $3 & TYPE_MASK;
	    if (exact_types && t1 != t2 && t1 != TYPE_ANY && t2 != TYPE_ANY) {
		type_error("== always false because of different types", $1);
		type_error("                               compared to", $3);
	    }
	    ins_f_byte(F_EQ);
	    $$ = TYPE_NUMBER;
	}
      | expr24 F_NE expr24
	{
	    int t1 = $1 & TYPE_MASK, t2 = $3 & TYPE_MASK;
	    if (exact_types && t1 != t2 && t1 != TYPE_ANY && t2 != TYPE_ANY) {
		type_error("!= always true because of different types", $1);
		type_error("                               compared to", $3);
	    }
	    ins_f_byte(F_NE);
	    $$ = TYPE_NUMBER;
	};

expr23: expr24
      | expr24 '>' expr24
	{ $$ = TYPE_NUMBER; ins_f_byte(F_GT); }
      | expr24 F_GE expr24
	{ $$ = TYPE_NUMBER; ins_f_byte(F_GE); }
      | expr24 '<' expr24
	{ $$ = TYPE_NUMBER; ins_f_byte(F_LT); }
      | expr24 F_LE expr24
	{ $$ = TYPE_NUMBER; ins_f_byte(F_LE); };

expr24: expr25
      | expr24 F_LSH expr25
	{
	    ins_f_byte(F_LSH);
	    $$ = TYPE_NUMBER;
	    if (exact_types && !TYPE($1, TYPE_NUMBER))
		type_error("Bad argument number 1 to '<<'", $1);
	    if (exact_types && !TYPE($3, TYPE_NUMBER))
		type_error("Bad argument number 2 to '<<'", $3);
	}
      | expr24 F_RSH expr25
	{
	    ins_f_byte(F_RSH);
	    $$ = TYPE_NUMBER;
	    if (exact_types && !TYPE($1, TYPE_NUMBER))
		type_error("Bad argument number 1 to '>>'", $1);
	    if (exact_types && !TYPE($3, TYPE_NUMBER))
		type_error("Bad argument number 2 to '>>'", $3);
	};

expr25: expr27
      | expr25 '+' expr27	/* Type checks of this case is complicated */
	{ ins_f_byte(F_ADD); $$ = TYPE_ANY; }
      | expr25 '-' expr27
	{
	    int bad_arg = 0;

	    if (exact_types) {
		if (!(TYPE($1, TYPE_NUMBER) || TYPE($1, TYPE_FLOAT)) &&
		    !($1 & TYPE_MOD_POINTER) ) {
                    type_error("Bad argument number 1 to '-'", $1);
		    bad_arg++;
		}
		if (!TYPE($3, $1) && !($3 & TYPE_MOD_POINTER) ) {
                    type_error("Bad argument number 2 to '-'", $3);
		    bad_arg++;
		}
	    }
	    $$ = TYPE_ANY;
	    if (($1 & TYPE_MOD_POINTER) || ($3 & TYPE_MOD_POINTER)) 
		$$ = TYPE_MOD_POINTER | TYPE_ANY;
	    if (!($1 & TYPE_MOD_POINTER) || !($3 & TYPE_MOD_POINTER)) {
	      if (exact_types && $$ != TYPE_ANY && !bad_arg)
		yyerror("Arguments to '-' don't match");
	      $$ = ($1 & TYPE_MOD_POINTER)?$3:$1;
	    }
	    ins_f_byte(F_SUBTRACT);
	}
	;

expr27: expr28
      | expr27 '*' expr3
	{
            if (exact_types &&
	        !(TYPE($1, TYPE_NUMBER) || TYPE($1, TYPE_FLOAT) ||
		TYPE($1, TYPE_STRING) || TYPE($1, TYPE_ANY | TYPE_MOD_POINTER)))
		type_error("Bad argument number 1 to '*'", $1);
	    if (exact_types && !TYPE($1, $3) && 
	        !(TYPE($1, TYPE_STRING) && TYPE($3, TYPE_NUMBER)) &&
	        !(TYPE($3, TYPE_STRING) && TYPE($1, TYPE_NUMBER)) &&
		!(TYPE($1, TYPE_ANY | TYPE_MOD_POINTER) && TYPE($3, TYPE_NUMBER)) &&
		!(TYPE($3, TYPE_ANY | TYPE_MOD_POINTER) && TYPE($1, TYPE_NUMBER)))
		type_error("Bad argument number 2 to '*'", $3);

	    ins_f_byte(F_MULTIPLY);

            if (TYPE($3, TYPE_STRING) ||
	        TYPE($3, TYPE_ANY | TYPE_MOD_POINTER))
		$$ = $3;
            else
                $$ = $1;

	}
      | expr27 F_MOD expr3
	{
	    if (exact_types && !TYPE($1, TYPE_NUMBER))
		type_error("Bad argument number 1 to '%'", $1);
	    if (exact_types && !TYPE($3, TYPE_NUMBER))
		type_error("Bad argument number 2 to '%'", $3);
	    ins_f_byte(F_MOD);
	    $$ = TYPE_NUMBER;
	}
      | expr27 '/' expr3
	{
	    if (exact_types && !(TYPE($1, TYPE_NUMBER) || TYPE($1, TYPE_FLOAT)))
		type_error("Bad argument number 1 to '/'", $1);
	    if (exact_types && !TYPE($3, $1))
		type_error("Bad argument number 2 to '/'", $3);
	    ins_f_byte(F_DIVIDE);
	    $$ = $1;
	}
      | expr27 '@' expr3
	{
	    if (exact_types && !TYPE($1, TYPE_FUNCTION))
		type_error("Bad argument number 1 to '@'", $1);
	    if (exact_types && !TYPE($3, TYPE_FUNCTION))
		type_error("Bad argument number 2 to '@'", $3);
	    ins_f_byte(F_BUILD_CLOSURE);
	    ins_byte(FUN_COMPOSE);
	    $$ = TYPE_FUNCTION;
	}
	;

expr28: expr3
	| cast expr3
	      {
		  $$ = $1;
		  if (exact_types && $2 != TYPE_ANY && $2 != TYPE_UNKNOWN &&
		      $1 != TYPE_VOID)
		      type_error("Casts are only legal for type mixed, or when unknown", $2);
	      } ;

expr3: expr31
     | F_INC lvalue
        {
	    deref_index--;
	    ins_f_byte(F_INC);
	    if (exact_types && !TYPE($2, TYPE_NUMBER))
		type_error("Bad argument to ++", $2);
	    $$ = TYPE_NUMBER;
	}
     | F_DEC lvalue
        {
	    deref_index--;
	    ins_f_byte(F_DEC);
	    if (exact_types && !TYPE($2, TYPE_NUMBER))
		type_error("Bad argument to --", $2);
	    $$ = TYPE_NUMBER;
	}
     | F_NOT expr3
	{
	    ins_f_byte(F_NOT);	/* Any type is valid here. */
	    $$ = TYPE_NUMBER;
	}
     | '~' expr3
	{
	    ins_f_byte(F_COMPL);
	    if (exact_types && !TYPE($2, TYPE_NUMBER))
		type_error("Bad argument to ~", $2);
	    $$ = TYPE_NUMBER;
	}
     | '-' expr3
	{
	    ins_f_byte(F_NEGATE);
	    if (exact_types && !(TYPE($2, TYPE_NUMBER) || TYPE($2, TYPE_FLOAT)))
		type_error("Bad argument to unary '-'", $2);
	    $$ = $2;
	}
     | '&' funexpr '(' oexpr_list ')'
	{
	    if ($4) {
		pop_arg_stack($4);
		ins_f_byte(F_PAPPLY);
		ins_byte((char)$4);
	    }
	    if (exact_types && !TYPE($2, TYPE_FUNCTION))
		type_error("Bad argument to unary '&'", $2);
	    $$ = TYPE_FUNCTION;
	}
     | '&' F_ARROW F_IDENTIFIER
	{
	    /* emulate &call_other(,id) */

	    ins_f_byte(F_BUILD_CLOSURE);
	    ins_byte(FUN_EFUN);
	    ins_short(F_CALL_OTHER); /* call_other */
	    
	    ins_f_byte(F_BUILD_CLOSURE);
	    ins_byte(FUN_EMPTY); /* empty arg */

	    ins_f_byte(F_STRING);
	    ins_short(store_prog_string($3)); /* the string for the id */

	} '(' oexpr_list ')'
	{
	    pop_arg_stack($6);
	    ins_f_byte(F_PAPPLY);
	    ins_byte((char)(2 + $6));	/* two args above & the oexpr_list  */

	    $$ = TYPE_FUNCTION;
	}
     ;

funexpr:	
	funident		{ $$ = $1; }
	| string_or_id F_ARROW F_IDENTIFIER
	{
	    ins_f_byte(F_STRING);
	    ins_short(store_prog_string($3));
	    ins_f_byte(F_BUILD_CLOSURE);
	    ins_byte(FUN_LFUNO);
	    $$ = TYPE_FUNCTION;
	}
	;

/* I hate it!  LA */
string_or_id:
	string
	{
	    $<type>$ = $1;
	}
	| identifier
	{
            int var_num = lookup_local_name($1);
            if (var_num != -1) {
	        ins_f_byte(F_LOCAL_NAME);
	        ins_byte((char)var_num);
            } else {
	        int i = verify_declared($1);

	        if (i == -1) {
		    char buff[100];

		    (void)snprintf(buff, sizeof(buff), "Variable %s not declared!", $1);
		    yyerror(buff);
	        } else {
		    ins_f_byte(F_IDENTIFIER);
		    ins_byte((char)variable_inherit_found);
		    ins_byte((char)variable_index_found);
	        }
           }
	}
	| '(' comma_expr ')'
	;

oexpr_list: /* empty */			{ $$ = 0; }
	|   expr0			{ add_arg_type($1); $$ = 1; }
	|   oexpr_list1			{ $$ = $1; };

oexpr_list1: oexpr0 ',' oexpr0		{ $$ = 2; }
	| oexpr0 ',' oexpr_list1	{ $$ = $3 + 1; } ;

oexpr0:	expr0				{ add_arg_type($1); }
	| /*empty */			{
					  ins_f_byte(F_BUILD_CLOSURE);
					  ins_byte(FUN_EMPTY);
					  add_arg_type(TYPE_ANY);
					}
	;

funident: F_IDENTIFIER 
	{
            int var_num = lookup_local_name($1);
            if (var_num != -1) {
	        ins_f_byte(F_LOCAL_NAME);
	        ins_byte((char)var_num);
                $$ = type_of_locals[var_num];
            } else {
	        int i = verify_declared($1);

	        if (i == -1) {
		    if (handle_function_id($1)) {
		        $$ = TYPE_FUNCTION;
		    } else {
		        char buff[100];

		        (void)snprintf(buff, sizeof(buff), "Variable %s not declared!", $1);
		        yyerror(buff);
		        $$ = TYPE_ANY;
		    }
	        } else {
		    ins_f_byte(F_IDENTIFIER);
		    ins_byte((char)variable_inherit_found);
		    ins_byte((char)variable_index_found);
		    $$ = i & TYPE_MASK;
	        }
            }
        }
	| funoperator 
	{
 	    $$ = TYPE_FUNCTION;
	};

funoperator:
	F_OPERATOR '(' funop ')'
	{
	    ins_f_byte(F_BUILD_CLOSURE); /* and build the function closure */
	    ins_byte(FUN_EFUN);
	    ins_short((short)$3);
	};

funop:	'+' { $$ = F_ADD; } |
	'-' { $$ = F_SUBTRACT; } |
	'*' { $$ = F_MULTIPLY; } |
	'/' { $$ = F_DIVIDE; } |
	F_MOD { $$ = F_MOD; } |
	'>' { $$ = F_GT; } |
	'<' { $$ = F_LT; } |
	F_GE { $$ = F_GE; } |
	F_LE { $$ = F_LE; } |
	F_EQ { $$ = F_EQ; } |
	F_NE { $$ = F_NE; } |
	'&' { $$ = F_AND; } |
	'^' { $$ = F_XOR; } |
	'|' { $$ = F_OR; } |
	F_LSH { $$ = F_LSH; } |
	F_RSH { $$ = F_RSH; } |
	'[' ']' { $$ = F_INDEX; } |
	F_NOT { $$ = F_NOT; } |
	'~' { $$ = F_COMPL; };

/*aexpr: rvalue | '(' comma_expr ')' { $$ = $2; };*/

expr31: expr4
      | lvalue F_INC
         {
	     deref_index--;
	     ins_f_byte(F_POST_INC);
	     if (exact_types && !TYPE($1, TYPE_NUMBER))
		 type_error("Bad argument to ++", $1);
	     $$ = TYPE_NUMBER;
	 }
      | lvalue F_DEC
         {
	     deref_index--;
	     ins_f_byte(F_POST_DEC);
	     if (exact_types && !TYPE($1, TYPE_NUMBER))
		 type_error("Bad argument to --", $1);
	     $$ = TYPE_NUMBER;
	 };

rvalue: lvalue
	{
	    int i, lastl;
	    /* This is yucky!  But the way the parser is structured
	     * together with YACC forces it.
	     */
	    if (deref_index <= 0)
		fatal("deref_index\n");
	    lastl = deref_stack[--deref_index];
	    if (lastl != -1) {
		switch(mem_block[A_PROGRAM].block[lastl]) {
		case F_PUSH_IDENTIFIER_LVALUE-EFUN_FIRST:
		    i = F_IDENTIFIER-EFUN_FIRST; break;
		case F_PUSH_LOCAL_VARIABLE_LVALUE-EFUN_FIRST:
		    i = F_LOCAL_NAME-EFUN_FIRST; break;
		case F_PUSH_INDEXED_LVALUE-EFUN_FIRST:
		    i = F_INDEX-EFUN_FIRST; break;
		default:i = -1; break;
		}
		if (i == -1) fatal("Should be a push at this point!\n");
		mem_block[A_PROGRAM].block[lastl] = i;
	    }
	    $$ = $1 & ~TYPE_LVALUE;
	};

expr4:  rvalue
	| funoperator
	  {
 	    $$ = TYPE_FUNCTION;
	  }
	| expr4 '[' comma_expr F_RANGE comma_expr ']'
	  {
	      ins_f_byte(F_RANGE);
	      if (exact_types && !($1 & TYPE_MAPPING))
		{
		  if (($1 & TYPE_MOD_POINTER) == 0 && !TYPE($1, TYPE_STRING))
		      type_error("Bad type to indexed value", $1);
		  if (!TYPE($3, TYPE_NUMBER))
		      type_error("Bad type of index", $3);
		  if (!TYPE($5, TYPE_NUMBER))
		      type_error("Bad type of index", $5);
		}
	      if ($1 == TYPE_ANY)
		  $$ = TYPE_ANY;
	      else if (TYPE($1, TYPE_STRING))
		  $$ = TYPE_STRING;
	      else if ($1 & TYPE_MOD_POINTER)
		  $$ = $1;
	      else if (exact_types)
		  type_error("Bad type of argument used for range", $1);
	  }
	| expr4 '[' F_RANGE { ins_f_byte(F_CONST0); } comma_expr ']'
	  {
	      ins_f_byte(F_RANGE);
	      if (exact_types && !($1 & TYPE_MAPPING))
		{
		  if (($1 & TYPE_MOD_POINTER) == 0 && !TYPE($1, TYPE_STRING))
		      type_error("Bad type to indexed value", $1);
		  if (!TYPE($5, TYPE_NUMBER))
		      type_error("Bad type of index", $5);
		}
	      if ($1 == TYPE_ANY)
		  $$ = TYPE_ANY;
	      else if (TYPE($1, TYPE_STRING))
		  $$ = TYPE_STRING;
	      else if ($1 & TYPE_MOD_POINTER)
		  $$ = $1;
	      else if (exact_types)
		  type_error("Bad type of argument used for range", $1);
	  }
	| expr4 '[' comma_expr F_RANGE  ']'
	  {
              static long long maxint = ~(unsigned long long)0 >> 1;
	      ins_f_byte(F_NUMBER);
	      ins_mem(&maxint, sizeof(maxint)); /* MAXINT */

	      ins_f_byte(F_RANGE);
	      if (exact_types && !($1 & TYPE_MAPPING))
		{
		  if (($1 & TYPE_MOD_POINTER) == 0 && !TYPE($1, TYPE_STRING))
		      type_error("Bad type to indexed value", $1);
		  if (!TYPE($3, TYPE_NUMBER))
		      type_error("Bad type of index", $3);
		}
	      if ($1 == TYPE_ANY)
		  $$ = TYPE_ANY;
	      else if (TYPE($1, TYPE_STRING))
		  $$ = TYPE_STRING;
	      else if ($1 & TYPE_MOD_POINTER)
		  $$ = $1;
	      else if (exact_types)
		  type_error("Bad type of argument used for range", $1);
	  }
     | string | number
     | real
     | '(' comma_expr ')' { $$ = $2; }
     | catch { $$ = TYPE_ANY; }
     | sscanf { $$ = TYPE_NUMBER; }
     | parse_command { $$ = TYPE_NUMBER; }
     | '(' '{' expr_list '}' ')'
       {
	   pop_arg_stack($3);		/* We don't care about these types */
	   ins_f_byte(F_AGGREGATE);
	   ins_short((short)$3);
	   $$ = TYPE_MOD_POINTER | TYPE_ANY;
       }
     | '(' '[' m_expr_list ']' ')'
       {
	   pop_arg_stack($3);
	   ins_f_byte(F_M_AGGREGATE);
	   ins_short((short)$3);
	   $$ = TYPE_MAPPING;
       }
	   /* Function calls */
/* ident, not name. Calls to :: functions are not allowed anyway /Dark 
*/
        | call_other_start
        {
	    ins_f_byte(F_BUILD_CLOSURE);
	    ins_byte(FUN_LFUNO);
	    $$ = TYPE_FUNCTION;	    
	}
        | expr4 '(' expr_list ')'
	{
            if ($1 != TYPE_FUNCTION && $1 != TYPE_ANY)
		type_error("Call of non-function", $1);
	    ins_f_byte(F_CALL_VAR);
	    ins_byte((char)$3);
	    pop_arg_stack($3);
#if MUSTCASTFUN
	    $$ = TYPE_UNKNOWN;
#else
	    $$ = TYPE_ANY;
#endif
	}
        | call_other_start '(' expr_list ')' {
	    ins_f_byte(F_CALL_OTHER);
	    ins_byte((char)($3 + 2));
	    pop_arg_stack($3);	/* No good need of these arguments */
	    $$ = TYPE_ANY;          /* TYPE_UNKNOWN forces casts */
	}
        | identifier '(' expr_list ')' /* special case */
        { 
          int var_num = lookup_local_name($1);
          if (var_num != -1) {
	    int t;
	    ins_f_byte(F_LOCAL_NAME);
	    ins_byte((char)var_num);
	    t = type_of_locals[var_num];
            if (t != TYPE_FUNCTION && t != TYPE_ANY)
		type_error("Call of non-function", t);
	    ins_f_byte(F_CALL_VAR);
	    ins_byte((char)$3);
	    pop_arg_stack($3);	/* Argument types not needed more */
#if MUSTCASTFUN
	    $$ = TYPE_UNKNOWN;
#else
	    $$ = TYPE_ANY;
#endif
          } else {
          /* Note that this code could be made a lot cleaner if some 
           * support functions were added. (efun_call() is a good one) 
	   */
	  int inherited_override = strchr($1, ':') != NULL;
	  int efun_override = inherited_override && strncmp($1,"efun::", 6) == 0;
	  int is_sfun = 0;
	  int f;
  
          $$ = TYPE_ANY;		/* in case anything goes wrong we need a default type. /LA */
	  if (inherited_override && !efun_override) 
	  {
	      struct function *funp;

	      /* Find the right function to call 
		         
                   This assumes max 255 inherited programs
		   and 32767 functions in one program.
	       */
	      if (defined_function($1)) 
	      {

		  funp = &function_prog_found->functions[function_index_found];
		  $$ = funp->type_flags & TYPE_MASK;
		  ins_f_byte(F_CALL_NON_VIRT);
		  ins_byte ((char)function_inherit_found);
                  ins_short ((short)function_index_found);
		  ins_byte((char)$3); /* Number of arguments */
	      }
	      else 
	      {
		  char buff[100];

		  (void)snprintf(buff, sizeof(buff), "Function %s undefined", $1);
		  yyerror(buff);
              }
           }

           /* All simul_efuns are considered defined. 
	    */
	   else if (!efun_override && 
                  (defined_function ($1) || ((is_sfun = 1) && is_simul_efun ($1))) ) 
	   {
	       struct function *funp;
	       unsigned short *arg_indices;
	       unsigned short *arg_types; 
 
	       if (function_prog_found) 
	       {
		   funp = &(function_prog_found->
			    functions[function_index_found]);
		   arg_indices = function_prog_found->type_start;
		   arg_types = function_prog_found->argument_types;
	       }
	       else
	       {
		   funp = FUNCTION(function_index_found);
		   
		   /* Beware... these are pointers to volatile structures 
		    */
		   arg_indices = (unsigned short *) mem_block[A_ARGUMENT_INDEX].block;
		   arg_types = (unsigned short *) mem_block[A_ARGUMENT_TYPES].block;
		   /* Assumption: exact_types implies correct 
		      values for arg_types 
		    */
	       }

	       /* Private functions in inherited programs may not be called. 
	        */
	       if (function_prog_found && 
		   function_type_mod_found & TYPE_MOD_PRIVATE) 
	       {
		   char buff[100];

		   (void)snprintf(buff, sizeof(buff), "Function %s marked 'private'", 
			    funp->name);
		   yyerror (buff);
	       }
	       $$ = funp->type_flags & TYPE_MASK;
              
               if (is_sfun)
	       {
                   ins_f_byte(F_CALL_SIMUL);
		   store_reloc_data(R_CALL,
				    (unsigned short)mem_block[A_PROGRAM].current_size,
				    $1,0,0);
	       }
               else if (function_type_mod_found & (TYPE_MOD_NO_MASK|TYPE_MOD_PRIVATE))
	       {
                   ins_f_byte(F_CALL_NON_VIRT);
	       }
               else
	       {
		   ins_f_byte (F_CALL_VIRT);
		   if (function_prog_found)
		       store_reloc_data(R_CALL,
					(unsigned short)mem_block[A_PROGRAM].current_size,
					$1,0,0);
	       }
	       ins_byte ((char)function_inherit_found);
               ins_short ((short)function_index_found);
               /* Insert function name into string list and code its index 
		*/
	       ins_byte ((char)$3);

	       /*
		* Check number of arguments.
		*/
	       if ((funp->type_flags & NAME_STRICT_TYPES) && exact_types &&
		   funp->num_arg != $3 && 

		   /* An old varargs function */
		   !((funp->type_flags & TYPE_MOD_VARARGS) &&
		     $3 < funp->num_arg) &&

		   /* A true varargs function? */
		   !((funp->type_flags & TYPE_MOD_TRUE_VARARGS) &&
		   $3 + 1 >= funp->num_arg) &&

		   /* A function with default values for arguments? */
		   !($3 < funp->num_arg && funp->first_default <= $3))
	       {
		   char buff[100];

		   (void)snprintf(buff, sizeof(buff), "Wrong number of arguments to %s", $1);
		   yyerror(buff);
	       }

	       /*
		* Check the argument types.
		*/
	       if (funp->type_flags & NAME_STRICT_TYPES &&
		   exact_types && arg_indices && 
		   arg_indices[function_index_found] != INDEX_START_NONE) 
	       {
		   int i, first;
		   int num_check = funp->num_arg;
		   
		   if (funp->type_flags & TYPE_MOD_TRUE_VARARGS)
		       num_check--;

		   first = arg_indices[function_index_found];
		   for (i=0; i < num_check && i < $3; i++) 
		   {
		       int tmp = get_argument_type(i, $3);
		       if (!TYPE(tmp, arg_types[first + i])) 
		       {
			   char buff[100];

			   (void)snprintf(buff, sizeof(buff),
					  "Bad type for argument %d %s", i+1,
					  get_two_types(arg_types[first+i], tmp));
			   yyerror(buff);
		       }
		   }
	       }
	   }

	   else if (efun_override || (f = lookup_predef($1)) != -1)
	   {
	       int min, max, def, *argp;
	       extern int efun_arg_types[];
 
	       if (efun_override) 
		   f = lookup_predef($1+6);

	       if (f == -1) 	/* Only possible for efun_override */
	       {
		   char buff[100];

		   (void)snprintf(buff, sizeof(buff), "Unknown efun: %s", $1+6);
		   yyerror(buff);
	       }
	       else
	       {
		   min = instrs[f-EFUN_FIRST].min_arg;
		   max = instrs[f-EFUN_FIRST].max_arg;
		   def = instrs[f-EFUN_FIRST].Default;
		   $$ = instrs[f-EFUN_FIRST].ret_type;
		   argp = &efun_arg_types[instrs[f-EFUN_FIRST].arg_index];
		   if (def && $3 == min-1) 
		   {
		       ins_f_byte((unsigned)def);
		       max--;
		       min--;
		   } 
		   else if ($3 < min) 
		   {
		       char bff[100];

		       (void)snprintf(bff, sizeof(bff), "Too few arguments to %s", 
				      instrs[f-EFUN_FIRST].name);
		       yyerror(bff);
		   } 
		   else if ($3 > max && max != -1) 
		   {
		       char bff[100];

		       (void)snprintf(bff, sizeof(bff), "Too many arguments to %s",
				      instrs[f - EFUN_FIRST].name);
		       yyerror(bff);
		   } 
		   else if (max != -1 && exact_types) 
		   {
		       /*
			* Now check all types of the arguments to efuns.
			*/
		       int i, argn;
		       char buff[100];
		       for (argn=0; argn < $3; argn++) 
		       {
			   int tmp = get_argument_type(argn, $3);
			   for(i=0; !TYPE(argp[i], tmp) && argp[i] != 0; i++)
			       ;
			   if (argp[i] == 0) 
			   {
			       (void)snprintf(buff, sizeof(buff),
				       "Bad argument %d type to efun %s()",
				       argn+1, instrs[f-EFUN_FIRST].name);
			       yyerror(buff);
			   }
			   while(argp[i] != 0)
			       i++;
			   argp += i + 1;
		       }
		   }
		   ins_f_byte((unsigned)f);

		   /* Only store number of arguments for instructions
		    * that allowed a variable number.
		    */
		   if (max != min)
		       ins_byte((char)$3); /* Number of actual arguments */
	       }
	   } 
	   else 
	   {
	       int i;
	       i = verify_declared($1);
	       if (i != -1 && (i & TYPE_MASK) == TYPE_FUNCTION) {
		   ins_f_byte(F_IDENTIFIER);
		   ins_byte((char)variable_inherit_found);
		   ins_byte((char)variable_index_found);
		   ins_f_byte(F_CALL_VAR);
		   ins_byte((char)$3);
#if MUSTCASTFUN
		   $$ = TYPE_UNKNOWN;
#else
		   $$ = TYPE_ANY;
#endif
	       } else {

		   if (exact_types) {
		       char buff[200];

		       (void)snprintf(buff, sizeof(buff), "Undefined function: %s", $1);
		       yyerror (buff);
		   }

		   /* Function not found 
		    */
		   ins_f_byte(F_CALL_VIRT);
		   store_reloc_data(R_CALL,
				    (unsigned short)mem_block[A_PROGRAM].current_size,
				    $1,0,0);
		   ins_byte((char)0xFF);
		   ins_short (-1);
		   ins_byte((char)$3); /* Number of arguments */
		   $$ = TYPE_ANY; /* just a guess */
	       }
	   }
	   pop_arg_stack($3);	/* Argument types not needed more */
           }
	};

call_other_start: expr4 F_ARROW F_IDENTIFIER
	{
	    ins_f_byte(F_STRING);
	    ins_short(store_prog_string($3));
	} ;
catch: F_CATCH { ins_f_byte(F_CATCH); push_address(); ins_short(0);}
       '(' comma_expr ')'
	       {
		   ins_f_byte(F_POP_VALUE);
		   ins_f_byte(F_END_CATCH);
		   upd_short(pop_address(),
			     (short)mem_block[A_PROGRAM].current_size);
	       };

sscanf: F_SSCANF '(' expr0 ',' expr0 lvalue_list ')'
	{
	    ins_f_byte(F_SSCANF); ins_byte((char)($6 + 2));
	};

parse_command: F_PARSE_COMMAND '(' expr0 ',' expr0 ',' expr0 lvalue_list ')'
	{
	    ins_f_byte(F_PARSE_COMMAND); ins_byte((char)($8 + 3));
	};

lvalue_list: /* empty */ { $$ = 0; }
	   | ',' lvaluec lvalue_list { $$ = 1 + $3; } ;
lvaluec: lvalue { if (!($1 & TYPE_LVALUE)) yyerror("Illegal lvalue"); $$ = $1 & TYPE_LVALUE; deref_index--; };

identifier: identifier2
           | F_COLON_COLON identifier2
           {
               $$ = tmpalloc(strlen($2) + 3);
               (void)sprintf($$, "::%s", $2);
           }
identifier2: F_IDENTIFIER
           | identifier2 F_COLON_COLON F_IDENTIFIER
           {
               $$ = tmpalloc(strlen($1) + strlen($3) + 3);
               (void)sprintf($$, "%s::%s", $1, $3);
           } ;
fun_identifier: F_IDENTIFIER
           | F_LSH { $$ = DTOR_FUNC;}
           | F_RSH { $$ = CTOR_FUNC;};

lvalue: identifier
	{
            int var_num = lookup_local_name($1);
            if (var_num != -1) {
	        deref_stack[deref_index++] = mem_block[A_PROGRAM].current_size;
	        if (deref_index >= DEREFSIZE)
		    fatal("deref_index overflow\n");
	        ins_f_byte(F_PUSH_LOCAL_VARIABLE_LVALUE);
	        ins_byte((char)var_num);
                $$ = type_of_locals[var_num] | TYPE_LVALUE;
            } else {
	        int i = verify_declared($1);

	        if (i == -1) {
		    deref_stack[deref_index++] = -1;
		    if (deref_index >= DEREFSIZE)
		        fatal("deref_index overflow\n");
		    if (handle_function_id($1)) {
		        $$ = TYPE_FUNCTION;
		    } else {
		        char buff[100];

		        (void)snprintf(buff, sizeof(buff), "Variable %s not declared!", $1);
		        yyerror(buff);
		        $$ = TYPE_ANY;
		    }
	        } else {
		    deref_stack[deref_index++] = mem_block[A_PROGRAM].current_size;
		    if (deref_index >= DEREFSIZE)
		        fatal("deref_index overflow\n");
		    ins_f_byte(F_PUSH_IDENTIFIER_LVALUE);
		    ins_byte((char)variable_inherit_found);
		    ins_byte((char)variable_index_found);
		    $$ = (i & TYPE_MASK) | TYPE_LVALUE;
	        }
            }
	}
	| expr4 '[' comma_expr ']'
	  {
	      deref_stack[deref_index++] = mem_block[A_PROGRAM].current_size;
	      if (deref_index >= DEREFSIZE)
		  fatal("deref_index overflow\n");
	      ins_f_byte(F_PUSH_INDEXED_LVALUE);
	      if (exact_types  && !($1 & TYPE_MAPPING)) {
		  if (($1 & TYPE_MOD_POINTER) == 0 && !TYPE($1, TYPE_STRING))
		      type_error("Bad type to indexed value", $1);
		  if (!TYPE($3, TYPE_NUMBER))
		      type_error("Bad type of index", $3);
	      }
	      if ($1 == TYPE_ANY)
		  $$ = TYPE_ANY;
	      else if (TYPE($1, TYPE_STRING))
		  $$ = TYPE_NUMBER;
	      else if ($1 == TYPE_MAPPING)
		  $$ = TYPE_ANY;
	      else
		  $$ = $1 & TYPE_MASK & ~TYPE_MOD_POINTER;
	      $$ |= TYPE_LVALUE;
	  };

real: F_FLOATC
	{
	    ins_f_byte(F_FLOATC);
	    ins_mem(&$<real>1, sizeof($<real>1));
	    $$ = TYPE_FLOAT;
	};

string: F_STRING
	{
	    ins_f_byte(F_STRING);
	    ins_short(store_prog_string($1));
	    $$ = TYPE_STRING;
	};

string_constant: string_con1
        {
            $$ = $1;
        };

string_con1: F_STRING
	   | string_con1 '+' F_STRING
	{
	    $$ = tmpalloc( strlen($1) + strlen($3) + 1 );
	    (void)strcpy($$, $1);
	    (void)strcat($$, $3);
	}
	   | '(' string_con1 ')'
	{
	    $$ = $2;
	};

cond: condStart
      statement
	{
	    int i;
	    i = pop_address();
	    ins_f_byte(F_JUMP);
	    add_jump();
	    push_address();
	    ins_short(0);
	    upd_short(i, (short)mem_block[A_PROGRAM].current_size);
	}
      optional_else_part
	{ upd_short(pop_address(), (short)mem_block[A_PROGRAM].current_size); } ;

condStart: F_IF '(' comma_expr ')'
	{
	    ins_f_byte(F_JUMP_WHEN_ZERO);
	    add_jump();
	    push_address();
	    ins_short(0);
	} ;

optional_else_part: /* empty */
       | F_ELSE statement ;
%%
#line 2115 "postlang.y"

int link_errors;

static void ins_f_byte(unsigned int b)
{
    if (b - EFUN_FIRST < 256)
	ins_byte((char)(b - EFUN_FIRST));
    else
    {
	ins_byte((char)(F_EXT - EFUN_FIRST));
	ins_short((short)(b - EFUN_FIRST));
    }
}

void 
yyerror(char *str)
{
    extern int num_parse_error;

    if (num_parse_error > 5)
	return;
    (void)fprintf(stderr, "%s: %s line %d\n", current_file, str,
		  current_line);
    (void)fflush(stderr);
    smart_log(current_file, current_line, str);
    num_parse_error++;
}

static int 
check_declared(char *str)
{
    struct variable *vp;
    
    int offset;
    int inh;
    char *super_name = 0;
    char *sub_name;
    char *real_name;
    char *search;
    
    variable_index_found = variable_inherit_found = 255;
    real_name = strrchr(str, ':') + 1;
    sub_name = strchr(str, ':') + 2;
    
    if(!(real_name = (find_sstring((real_name == (char *)1) ? str : real_name))))
        return -1;
    if (sub_name == (char *)2)
	for (offset = 0; offset < mem_block[A_VARIABLES].current_size;
	     offset += sizeof (struct variable)) 
	    {
		vp = (struct variable *)&mem_block[A_VARIABLES].block[offset];
		/* Only index, prog, and type will be defined. */
		if (real_name == vp->name) 
		    {
			variable_type_mod_found = vp->type;
			variable_index_found = offset / sizeof (struct variable);
			variable_inherit_found = 255;
			return variable_index_found;
		    }
	    }
    else
	if (sub_name - str > 2)
	{
	    super_name = xalloc((size_t)(sub_name - str - 1));
	    (void)memcpy(super_name, str, (size_t)(sub_name - str - 2));
	    super_name[sub_name - str - 2] = 0;
	    if (strcmp(super_name, "this") == 0)
		return check_declared(sub_name);
	}
	else
	    str = sub_name;
    
    /* Look for the variable in the inherited programs
	*/

    for (inh = mem_block[A_INHERITS].current_size / sizeof (struct inherit) - 1;
          inh >= 0; inh -= ((struct inherit *)mem_block[A_INHERITS].block)[inh].prog->
	 num_inherited) 
    {
	if (super_name &&
	    strcmp(super_name, ((struct inherit *)mem_block[A_INHERITS].block)[inh].name) == 0)
	    search = sub_name;
	else
	    search = str;
        if (find_status (((struct inherit *)mem_block[A_INHERITS].block)[inh].prog,
			      search, TYPE_MOD_PRIVATE) != -1)
	{
	    /* Adjust for inherit-type */
	    int type = ((struct inherit *)mem_block[A_INHERITS].block)[inh].type;
	    
	    if (variable_type_mod_found & TYPE_MOD_PRIVATE)
		type &= ~TYPE_MOD_PUBLIC;
	    if (variable_type_mod_found & TYPE_MOD_PUBLIC)
		type &= ~TYPE_MOD_PRIVATE;
            variable_type_mod_found |= type & TYPE_MOD_MASK;

	    variable_inherit_found += inh -
		(((struct inherit *)mem_block[A_INHERITS].block)[inh].prog->
		 num_inherited - 1);
	    return 1;
	}
    }
    return -1;
}

static int 
handle_function_id(char *str)
{
    short i;

    if (defined_function(str)) {
	ins_f_byte(F_STRING);	/* XXX */
	ins_short(store_prog_string(str)); /* XXX */
	ins_f_byte(F_BUILD_CLOSURE); /* and build the function closure */
	if (function_type_mod_found & (TYPE_MOD_NO_MASK|TYPE_MOD_PRIVATE)) {
	    if (function_prog_found && function_prog_found->
		functions[function_index_found].type_flags & TYPE_MOD_PRIVATE)
	    {
		char buff[100];
		(void)snprintf(buff, sizeof(buff), "Function %s marked 'private'", 
			       str);
		yyerror (buff);
	    }
	    ins_byte(FUN_LFUN_NOMASK);
	    ins_byte((char)function_inherit_found);
	    ins_short((short)function_index_found);
	} else {
	    ins_byte(FUN_LFUN);
	    ins_byte((char)function_inherit_found);
	    ins_short((short)function_index_found);
	}
	return 1;
    } else if (is_simul_efun(str)) {
	ins_f_byte(F_STRING);
	ins_short(store_prog_string(str));
	ins_f_byte(F_BUILD_CLOSURE); /* and build the function closure */
	ins_byte(FUN_SFUN);
	return 1;
    } else if ((i = lookup_predef(str)) != -1) {
	ins_f_byte(F_BUILD_CLOSURE); /* and build the function closure */
	ins_byte(FUN_EFUN);
	ins_short(i);
	return 1;
    } else {
	return 0;
    }
}

static int 
verify_declared(char *str)
{
    int r;

    r = check_declared(str);
    if (r == -1)
    {
	return -1;
    }
    if (variable_inherit_found == 255)
	return ((struct variable *)mem_block[A_VARIABLES].block)[variable_index_found].
	    type;
    else
	return ((struct inherit *)mem_block[A_INHERITS].block)[variable_inherit_found].
	    prog->variable_names[variable_index_found].type;
}

void 
free_all_local_names()
{
    current_number_of_locals = 0;
    max_number_of_locals = 0;
}

static void
start_block(void)
{
    block_depth++;
}

static void end_block(void)
{
    block_depth--;
    for(;current_number_of_locals > 0 &&
        local_blockdepth[current_number_of_locals - 1] > block_depth;
        current_number_of_locals--)
        ;
}

int
lookup_local_name(char *str)
{
    int i;

    for (i = current_number_of_locals - 1; i >= 0; i--)
    {
        if (strcmp(local_names[i], str) == 0)
            return i;
    }

    return -1;
}

int 
add_local_name(char *str, int type)
{
    int var_num = lookup_local_name(str);
    if (var_num != -1) 
        if (local_blockdepth[var_num] == block_depth) {
            char buff[100];

            (void)snprintf(buff, sizeof(buff), "redefinition of variable '%s'",
                           str);
            yyerror(buff);
        }

    if (current_number_of_locals == MAX_LOCAL)
	yyerror("Too many local variables");
    else {
	type_of_locals[current_number_of_locals] = type;
	local_blockdepth[current_number_of_locals] = block_depth;
	local_names[current_number_of_locals++] = str;
    }
    if (current_number_of_locals > max_number_of_locals)
        max_number_of_locals = current_number_of_locals;
    return current_number_of_locals - 1;
}


int 
find_inherit(struct program *prog1, struct program *prog2)
{
    int i;
    
    for (i = 0; i < (int)prog1->num_inherited; i++)
	if (prog1->inherit[i].prog == prog2)
	    return i;
    return -1;
}

static void 
copy_inherits(struct program *from, int type, char *name)
{
    int i;
    struct inherit inh;
    int j[256], k = 0;
    char buf[256];
    
    if (!has_inherited)
    {
	free_sstring(((struct inherit *)mem_block[A_INHERITS].block)->name);
	mem_block[A_INHERITS].current_size = 0;
	has_inherited = 1;
    }
    for(i = 0; i < (int)from->num_variables; i++)
	if ((type | from->variable_names[i].type) & TYPE_MOD_NO_MASK &&
	    check_declared(from->variable_names[i].name) != -1 &&
	    (variable_inherit_found == 255 || ((struct inherit *)
	     (mem_block[A_INHERITS].block))[variable_inherit_found].prog != from))
	{
	    (void)snprintf(buf, sizeof(buf),
			   "Redefinition of no_mask variable %s in program %s.\n",
			   from->variable_names[i].name,
			   from->name);
            yyerror(buf);
	}

    for (i = 0; i < (int) from->num_functions; i++)
	if ((type | from->functions[i].type_flags) & TYPE_MOD_NO_MASK &&
	    defined_function(from->functions[i].name) &&
	    function_prog_found != from)
	{
	    (void)snprintf(buf, sizeof(buf),
			   "Redefinition of no_mask function %s in program %s.\n",
			   from->functions[i].name,
			   from->name);
	    yyerror(buf);
	}

    for (i = from->num_inherited - 2; i >= 0;
	 i -= from->inherit[i].prog->num_inherited)
	j[k++] = i;

    while (k) {
	/* Correct the type */
	int new_type = type;
	int old_type;
	
	i = j[--k];
	old_type = from->inherit[i].type;

	if (old_type & TYPE_MOD_PRIVATE)
	    new_type &= ~TYPE_MOD_PUBLIC;
	if (old_type & TYPE_MOD_PUBLIC)
	    new_type &= ~TYPE_MOD_PRIVATE;
	
	copy_inherits(from->inherit[i].prog,
		      old_type | new_type,
		      from->inherit[i].name);
    }
    
    inh = from->inherit[from->num_inherited - 1]; /* Make a copy */

    /* Adjust the info */
    inh.name = make_sstring(name);
    inh.type = type;
    add_to_mem_block (A_INHERITS, (char *)&inh, sizeof inh);
}

static int
check_inherits(struct program *from)
{
    int i, update = 0;
    struct object *ob;
    size_t len = strlen(from->name) - 1;
    char *ob_name = alloca(len);
    
    (void)strncpy(ob_name, from->name, len - 1);
    ob_name[len - 1] = 0;
    ob = find_object2(ob_name);

    if (!ob || ob->prog != from)
	return 0;

    for (i = from->num_inherited - 2; i >= 0;
	 i -= from->inherit[i].prog->num_inherited)
	if (!check_inherits(from->inherit[i].prog))
	    update = 1;

    if (update)
    {
	if (ob) {
	    ob->prog->flags &= ~PRAGMA_RESIDENT;
	    destruct_object(ob);
	}
	return 0;
    }
    return 1;
}

void
hash_func (int size, struct function *from, struct function_hash *hash)
{
    int i, probe, count, last_coll;
    int *coll;

    if (size <= 0)
        return;

    /* Allocate the list where we store collisions */
    coll = (int *)xalloc(sizeof(int) * size);

    /* Prepare the lists */
    for (i = 0; i < size; i++)
    {
        hash[i].next_hashed_function = -1; /* forward */
        hash[i].name = 0; /* free entry */
        coll[i] = -1;
    }

    /* Hash all non-collisions and mark collisions */
    count = 0;
    for (i = 0; i < size; i++)
    {
        probe = PTR_HASH(from[i].name, size);

        if (!hash[probe].name)
        {
            /* Free */
            hash[probe].name = from[i].name;
            hash[probe].func_index = i;
            from[i].hash_idx = probe;
        }
        else
        {
            /* Collision */
            coll[count] = i;
            count++;
        }
    }

    /* Plug collisions into the holes */
    i = 0;
    last_coll = 0;
    while (last_coll < count && i < size)
    {
        if (!hash[i].name)
        {
            /*
             * A hole, walk probe chain, update last next_hashed_function
             * to point here.
             */
            probe = PTR_HASH(from[coll[last_coll]].name, size);

            while (hash[probe].name && hash[probe].next_hashed_function != -1)
                probe = hash[probe].next_hashed_function;
            hash[probe].next_hashed_function = i;
            hash[i].name = from[coll[last_coll]].name;
            hash[i].func_index = coll[last_coll];
            from[coll[last_coll]].hash_idx = i;
            last_coll++;
        }

        i++;
    }

    free(coll);
}

/*
 * This function is called from lex.c for every new line read.
 */

void
init_lineno_info()
{
}

void
end_lineno_info()
{
}

void
store_line_number_info(int file, int line)
{
    struct lineno tmp;
    unsigned int code = mem_block[A_PROGRAM].current_size;
    struct lineno *last_lineno =
      (struct lineno *)(mem_block[A_LINENUMBERS].block + 
			mem_block[A_LINENUMBERS].current_size) - 1;

    if (mem_block[A_LINENUMBERS].current_size &&
	last_lineno->code == code) {
      last_lineno->file = file;
      last_lineno->lineno = line;
      return;
    }
    tmp.code = code;
    tmp.file = file;
    tmp.lineno = line;
    add_to_mem_block(A_LINENUMBERS, (char *)&tmp, sizeof(tmp));
}

static void 
store_reloc_data(char type, unsigned short address, char *name,
			     int value, char modifier)
{
    struct reloc rel;
    rel.type = type;
    rel.modifier = modifier;
    rel.address = address;
    rel.name = string_copy(name);
    rel.value = value;

    add_to_mem_block(A_RELOC, (char *)&rel, sizeof(struct reloc));
}
static char *
get_type_name(int type)
{
    static char buff[100];
    static char *type_name[] = { "unknown", "int", "string",
				     "void", "object", "mixed", "mapping",
				     "float", "function" };
    int pointer = 0;

    buff[0] = 0;
    if (type & TYPE_MOD_STATIC)
	(void)strcat(buff, "static ");
    if (type & TYPE_MOD_NO_MASK)
	(void)strcat(buff, "nomask ");
    if (type & TYPE_MOD_PRIVATE)
	(void)strcat(buff, "private ");
    if (type & TYPE_MOD_PUBLIC)
	(void)strcat(buff, "public ");
    if (type & TYPE_MOD_VARARGS)
	(void)strcat(buff, "varargs ");
    type &= TYPE_MASK;
    if (type & TYPE_MOD_POINTER) {
	pointer = 1;
	type &= ~TYPE_MOD_POINTER;
    }
    if (type >= sizeof type_name / sizeof type_name[0])
	fatal("Bad type\n");
    (void)strcat(buff, type_name[type]);
    (void)strcat(buff," ");
    if (pointer)
	(void)strcat(buff, "* ");
    return buff;
}

void 
type_error(char *str, int type)
{
    static char buff[100];
    char *p;
    p = get_type_name(type);
    if (strlen(str) + strlen(p) + 5 >= sizeof buff)
    {
	yyerror(str);
    }
    else
    {
	(void)strcpy(buff, str);
	(void)strcat(buff, ": \"");
	(void)strcat(buff, p);
	(void)strcat(buff, "\"");
	yyerror(buff);
    }
}

int 
remove_undefined_prototypes (int num_functions, struct function *functions) 
{
    int i;
    for (i = 0; i < num_functions;i++)
    {
	if (functions[i].type_flags & NAME_PROTOTYPE) 
	{
	    char buff[500];

	    (void)snprintf(buff, sizeof(buff),
			   "Function %s declared but never defined",
			   functions[i].name);
	    yyerror(buff);
	}
    }
    return num_functions;
}

/*
 * Compile an LPC file.
 */
void 
compile_file()
{
    int yyparse (void);

    prolog();
    (void)yyparse();
    epilog();
}

static char *
get_two_types(int type1, int type2)
{
    static char buff[100];

    (void)strcpy(buff, "( ");
    (void)strcat(buff, get_type_name(type1));
    (void)strcat(buff, "vs ");
    (void)strcat(buff, get_type_name(type2));
    (void)strcat(buff, ")");
    return buff;
}

/*
  This is used as an index of includefiles for giving correct runtime error
  messages references.
*/
void 
remember_include(char *buf)
{
    struct stat sbuf;
    char number[128];

    if (stat(buf, &sbuf)==-1) 
        fatal("LIB not able to stat open file\n");
    (void)sprintf(number,"%ld:", (long)sbuf.st_mtime);
    add_to_mem_block(A_INCLUDES, number, (int)strlen(number));
    add_to_mem_block(A_INCLUDES, buf, (int)strlen(buf) + 1);
}


/*
 * The program has been compiled. Prepare a 'struct program' to be returned.
 */

int current_id_number = 1;
#define H_OFFSET(arg) (((char *)&(((struct program *)0)->arg)) -\
		       ((char *)((struct program *)0)))

static struct section_desc sec_hdr[] =
{
    {A_HEADER, -1, -1, sizeof(struct program)},
    {A_INHERITS, H_OFFSET(inherit), H_OFFSET(num_inherited),
     sizeof(struct inherit)},
    {A_FUNC_HASH, H_OFFSET(func_hash), -1, sizeof(struct function_hash)},
    {-1, 0, 0, 0},
};
static struct section_desc sec_exe[] =
{
    {A_PROGRAM, H_OFFSET(program), H_OFFSET(program_size), 1},
    {A_RODATA, H_OFFSET(rodata), H_OFFSET(rodata_size), 1},
    {A_FUNCTIONS, H_OFFSET(functions), H_OFFSET(num_functions),
     sizeof(struct function)},
    {A_VARIABLES, H_OFFSET(variable_names), H_OFFSET(num_variables),
     sizeof(struct variable)},
    {A_CFUN, H_OFFSET(cfuns), -1, sizeof(struct cfun_desc)},
    {-1, 0, 0, 0},
};
static struct section_desc sec_dbg[] =
{
    {A_LINENUMBERS, H_OFFSET(line_numbers),
     H_OFFSET(sizeof_line_numbers), sizeof(struct lineno)},
    {A_INCLUDES, H_OFFSET(include_files),
     H_OFFSET(sizeof_include_files), 1},
    {-1, 0, 0, 0},
};

struct segment_desc segm_desc[] =
{
    /* S_HDR */
    { -1, -1, H_OFFSET(total_size),sec_hdr, },

    /* S_EXEQ */
    { H_OFFSET(program), H_OFFSET(swap_num), H_OFFSET(exec_size),sec_exe, },

    /* S_DBG */
    { H_OFFSET(line_numbers), H_OFFSET(swap_lineno_index),
    H_OFFSET(debug_size),sec_dbg, },
    { -1, -1, -1, (struct section_desc *)0 },
};

char *
load_segments(struct segment_desc *seg, struct mem_block *mem_block1)
{
    char *hdr = 0;
    int size;
    char *block;
    struct section_desc *sect;

    for ( ; seg->sections != NULL; seg++)
    {
	size = 0;
	for (sect = seg->sections; sect->section != -1; sect++)
	    size += align(mem_block1[sect->section].current_size);
	
	if (size)
	    block = xalloc((size_t)size);
	else
	    block = xalloc(1);
#ifdef PURIFY
	(void)memset(block, '\0', size ? size : 1);
#endif

	if (!hdr)
	    hdr = block;

	if (seg->ptr_offset != -1)
	    *(char **)(hdr + seg->ptr_offset) = block;

	for (sect = seg->sections; sect->section != -1; sect++)
	{
	    (void)memcpy(block, mem_block1[sect->section].block,
			 (size_t)mem_block1[sect->section].current_size);

	    if (sect->ptr_offset != -1)
		*(char **)(hdr + sect->ptr_offset) = block;
	    if (sect->num_offset != -1)
		*(unsigned short *)(hdr + sect->num_offset) = 
		    mem_block1[sect->section].current_size /
			sect->ent_size;

	    block += align(mem_block1[sect->section].current_size);
	}
	if (seg->size_offset != -1)
	    *(int *)(hdr + seg->size_offset) = size;
    }
    return hdr;
}

static void
process_reloc(struct reloc *reloc, int num_relocs, int num_inherited)
{
    unsigned int i;
    char *name;
    unsigned short fix;
    unsigned int psize;

    psize = mem_block[A_PROGRAM].current_size;
    link_errors = 0;
    
    for (i = 0; i < num_relocs; i++, reloc++)
    {
	if (reloc->address >= psize)
	    fatal("Corrupt relocation address.\n");
	
	name = reloc->name;
	switch (reloc->type)
	{
	    int call_efun;
	case R_CALL:
	    call_efun = 0;
	    if (!defined_function(name) ||
		((function_type_mod_found & TYPE_MOD_PRIVATE) &&
		function_inherit_found != num_inherited))
	    {
		if (is_simul_efun(name))
		{
		    call_efun = F_CALL_SIMUL;
		    function_index_found = store_prog_string(name);
		    function_inherit_found = 0;
		}
		else
		{
		    link_errors++;
		    (void)fprintf(stderr,"%s Function %s doesn't exist.\n",
                        inner_get_srccode_position(reloc->address,
                            (struct lineno *)mem_block[A_LINENUMBERS].block,
                            mem_block[A_LINENUMBERS].current_size /
                            sizeof(struct lineno),
                            mem_block[A_INCLUDES].block,
                            current_file), name);
                    
		    (void)fflush(stderr);
		    break;
		}
	    }
	    else if (strchr(name,':') || function_type_mod_found & (TYPE_MOD_NO_MASK|TYPE_MOD_PRIVATE))
		call_efun = F_CALL_NON_VIRT;
	    else
		call_efun = F_CALL_VIRT;
	    
	    fix = function_index_found;
	    mem_block[A_PROGRAM].block[reloc->address - 1] =
		call_efun - EFUN_FIRST;
	    mem_block[A_PROGRAM].block[reloc->address] =
		function_inherit_found;
	    mem_block[A_PROGRAM].block[reloc->address + 1] =
		((char *)&fix)[0];
	    mem_block[A_PROGRAM].block[reloc->address + 2] =
		((char *)&fix)[1];
	    break;
	    
	default:
	    fatal("Unsupported reloc data.\n");
	}
    }
}

void
link_C_functions(char *name)
{
    int i, j;
    struct cfun_desc cfun;
    int num_inherited;
    num_inherited = 
	mem_block[A_INHERITS].current_size /
	    sizeof(struct inherit);

    for(i = 0; interface[i]; i++)
	if (strcmp(interface[i]->program, name) == 0)
	    for(j = 0; interface[i]->vars[j]; j++)
	    {
		if (check_declared(interface[i]->vars[j]->name)
		    == -1)
		{
		    char buf[100];

		    (void)snprintf(buf, sizeof(buf),
				   "Variable %s (referenced from cfun) not defined.\n",
				   interface[i]->vars[j]->name);
		    current_line = -1;
		    yyerror(buf);
		}

		if (variable_inherit_found == 255)
		    variable_inherit_found = num_inherited;
		cfun.idx = variable_index_found;
		cfun.inh = variable_inherit_found - num_inherited;
		add_to_mem_block(A_CFUN, (char *)&cfun, sizeof(cfun));
	    }
}

/*
 * Optimize jumps.  If a jump's target is an unconditional jump, then update
 * the jump's target to be that of the unconditional jump.
 */
static void
optimize_jumps(void)
{
    int jumps, *ip, i, offset1, offset2, opcode;

    /*
     * Compute the number of jumps in the jump table.
     */
    jumps = mem_block_size(A_JUMPS, sizeof (int));

    /*
     * Get a pointer to the base of the jump table.
     */
    ip = (int *)mem_block[A_JUMPS].block;

    /*
     * Loop through each jump table entry.
     */
    for (i = 0; i < jumps; i++)
    {
	/*
	 * Read the target PC from the program section.
	 */
	offset2 = (unsigned short)read_short(*ip);

	/*
	 * Short-circuit all unconditional jumps.
	 */
	for (;;)
	{
	    /*
	     * Get the target PC of the jump.
	     */
	    offset1 = offset2;

	    /*
	     * Get the opcode.
	     */
	    opcode = (unsigned char)read_byte(offset2++);
	    if (opcode == F_EXT - EFUN_FIRST)
	    {
		opcode = (unsigned short)read_short(offset2);
		offset2 += 2;
	    }

	    /*
	     * If the target is not an unconditional jump, then proceed to
	     * the next jump table entry.
	     */
	    if (opcode != F_JUMP - EFUN_FIRST)
		break;

	    /*
	     * Get the target PC of the unconditional jump.
	     */
	    offset2 = (unsigned short)read_short(offset2);

	    /*
	     * If the target PC of the jump is the same as the target PC of
	     * the unconditional jump, then the jump is self-referential and
	     * not interesting.
	     */
	    if (offset2 == offset1)
		break;

	    /*
	     * Short-circuit the unconditional jump.
	     */
	    upd_short(*ip, (short)offset2);
	}

	ip++;
    }
}

static int
find_function(char *name) {
    int num_functions = mem_block[A_FUNCTIONS].current_size / sizeof (struct function);
    struct function *funcs = (struct function *)mem_block[A_FUNCTIONS].block;

    for (int idx = 0; idx < num_functions; idx++)
        if (strcmp(name, funcs[idx].name) == 0)
            return idx;
    return -1;
}

void
epilog()
{
    int i, ix;
    extern int pragma_resident;
    int functions_left; /* Functions left after removing dangling prototypes */
    extern int total_program_size;
    struct inherit inherit_self;
    int has_dtor, has_ctor;

#ifdef DEBUG
    if (num_parse_error == 0 && type_of_arguments.current_size != 0)
	fatal("Failed to deallocate argument type stack\n");
#endif
#ifdef DEALLOCATE_MEMORY_AT_SHUTDOWN
    if (type_of_arguments.block != NULL)
    {
	type_of_arguments.max_size = 0;
	type_of_arguments.current_size = 0;
	free(type_of_arguments.block);
	type_of_arguments.block = NULL;
    }
#endif
    /*
     * Define the .CTOR function, but only if there was any code
     * to initialize.
     */
    has_ctor = find_function(".CTOR");
    has_dtor = find_function(".DTOR");

    if (first_last_initializer_end != last_initializer_end)
    {
	if (has_ctor != -1)
	{
	    struct function *funp1;
	    
	    funp1 = &((struct function *)
		    mem_block[A_FUNCTIONS].block)[has_ctor];
	    upd_short(last_initializer_end, funp1->offset);
	    funp1->offset = 0;
	}
	else
	{
	    has_ctor = mem_block[A_FUNCTIONS].current_size /
		sizeof(struct function);
	    define_new_function(".CTOR", 0, 0, 0,
				TYPE_MOD_PRIVATE | TYPE_VOID, 0);
	    /*
	     * Change the last jump after the last initializer into a
	     * return(1) statement.
	     */
	    mem_block[A_PROGRAM].block[last_initializer_end - 1] =
		F_CONST1 - EFUN_FIRST;
	    mem_block[A_PROGRAM].block[last_initializer_end] =
		F_RETURN - EFUN_FIRST;
	}
    }

    functions_left = remove_undefined_prototypes (
              (int)(mem_block[A_FUNCTIONS].current_size / sizeof (struct function)),
    (struct function *)mem_block[A_FUNCTIONS].block);

    /* I don't like doing this, but I see no other way. |D| */
    mem_block[A_FUNCTIONS].current_size = 
        functions_left * sizeof (struct function);


    if (!(num_parse_error || inherit_file))
    {
	optimize_jumps();

	link_C_functions(current_file);

	link_errors = 0;
	process_reloc((struct reloc *)mem_block[A_RELOC].block,
		      (int)(mem_block[A_RELOC].current_size / sizeof(struct reloc)),
		      (int)(mem_block[A_INHERITS].current_size / sizeof(struct inherit)));
	num_parse_error += link_errors;
    }
    {
	int c;

	for (c = 0;
	     c < mem_block[A_RELOC].current_size / sizeof(struct reloc);
	     c++)
	    free(((struct reloc *)mem_block[A_RELOC].block)[c].name);
    }

    inherit_self.type = 0;
    inherit_self.name = make_sstring("this");
    inherit_self.prog = 0;
    inherit_self.variable_index_offset = 0;
    add_to_mem_block(A_INHERITS, (char *) &inherit_self, sizeof(struct inherit));

    add_to_mem_block(A_HEADER, (char *)&NULL_program, sizeof(NULL_program));
    {
	int num_func = 
	    mem_block[A_FUNCTIONS].current_size / sizeof(struct function);

	    mem_block[A_FUNC_HASH].current_size = num_func *
		sizeof(struct function_hash);
	if (num_func)
	{
	    mem_block[A_FUNC_HASH].block =
		realloc(mem_block[A_FUNC_HASH].block, num_func *
			sizeof(struct function_hash));
	    	mem_block[A_FUNC_HASH].max_size = 
		    mem_block[A_FUNC_HASH].current_size;
	}
	hash_func(num_func, (struct function *)mem_block[A_FUNCTIONS].block,
		  (struct function_hash *)mem_block[A_FUNC_HASH].block);
    }
    end_lineno_info();

    prog = (struct program *)load_segments(segm_desc, mem_block);

    prog->ref = 1;
    prog->swap_num = 0;
    prog->ctor_index = has_ctor;
    prog->dtor_index = has_dtor;
    prog->mod_time = current_time;
    prog->name = string_copy(current_file);
    prog->id_number = current_id_number++;
    prog->clones = NULL;
    prog->num_clones = 0;
#if defined(PROFILE_LPC)
    prog->cpu = 0;
    prog->last_avg_update = 0;
#endif
    prog->swap_lineno_index = 0;
    prog->flags = (pragma_no_inherit ? PRAGMA_NO_INHERIT : 0) |
	(pragma_no_clone ? PRAGMA_NO_CLONE : 0) |
	(pragma_no_shadow ? PRAGMA_NO_SHADOW : 0) |
        (pragma_resident ? PRAGMA_RESIDENT : 0);
    
    prog->inherit[prog->num_inherited - 1].prog = prog;

    for (i = ix = 0; i < (int)prog->num_inherited; i++)
    {
	int inh = find_inherit(prog, prog->inherit[i].prog);
	if (inh == i)
	{
	    prog->inherit[i].variable_index_offset = ix;
	    ix += prog->inherit[i].prog->num_variables;
	    prog->inherit[i].type &= ~TYPE_MOD_SECOND;
	}
	else
	{
	    prog->inherit[i].variable_index_offset = 
		prog->inherit[inh].variable_index_offset;
	    prog->inherit[i].type |= TYPE_MOD_SECOND;
	}
    }

    /*  don't forget the following:
     *   prog->sizeof_argument_types = 
     *			mem_block[A_ARGUMENT_TYPES??].current_size);
     *   if you have fixed it one day .... ok ? :-=)
     *   remember to remove the above change to total_size !!!!!!!!
     */
    prog->sizeof_argument_types=0;
    /* NOTE: Don't forget to hash the argument types along with the functions
     */  
    prog->argument_types = 0;	/* For now. Will be fixed someday */

    prog->type_start = 0;


    for (i = 0; i < (int)prog->num_inherited - 1; i++)
    {
	reference_prog (prog->inherit[i].prog, "inheritance");
    }

    prog->load_time = current_time;
    
    register_program(prog);

    /*  marion
	Do referencing here - avoid multiple referencing when an object
	inherits more than one object and one of the inherited is already
	loaded and not the last inherited
    */
    total_program_size += prog->exec_size;
    total_prog_block_size += prog->total_size;
    total_num_prog_blocks += 1;

    for (i=0; i < A_NUM; i++)
        free((char *)mem_block[i].block);

    if (num_parse_error || inherit_file)
    {
        free_prog(prog);
        prog = NULL;
    }
}

/*
 * Initialize the environment that the compiler needs.
 */
static void 
prolog() 
{
    int i;
#ifdef AUTO_INCLUDE
    char auto_include[] = AUTO_INCLUDE;
#endif
    if (type_of_arguments.block == 0) {
	type_of_arguments.max_size = 100;
	type_of_arguments.block = xalloc((size_t)type_of_arguments.max_size);
    }
    type_of_arguments.current_size = 0;
    prog = 0;		/* 0 means fail to load. */
    comp_stackp = 0;	/* Local temp stack used by compiler */
    current_continue_address = 0;
    current_break_address = 0;
    current_case_address = 0;
    num_parse_error = 0;
    try_level = break_try_level = continue_try_level = 0;
    free_all_local_names();	/* In case of earlier error */
    /* Initialize memory blocks where the result of the compilation
     * will be stored.
     */
    for (i=0; i < NUMAREAS; i++) {
	mem_block[i].block = xalloc(START_BLOCK_SIZE);
	mem_block[i].current_size = 0;
	mem_block[i].max_size = START_BLOCK_SIZE;
    }
    init_lineno_info();
    add_new_init_jump();
    first_last_initializer_end = last_initializer_end;
    has_inherited = 1;
    if (auto_ob && strcmp(current_file, auto_ob->prog->name))
    {
	copy_inherits(auto_ob->prog, 0, "auto");
	has_inherited = 0;
    }
#ifdef AUTO_INCLUDE
    current_line = 0;
    if (!handle_include(auto_include, 1))
       current_line = 1;
#endif

}

/*
 * Add a trailing jump after the last initialization code.
 */
void 
add_new_init_jump() 
{
    /*
     * Add a new jump.
     */
    ins_f_byte(F_JUMP);
    last_initializer_end = mem_block[A_PROGRAM].current_size;
    ins_short(0);
}

static int
search_for_ext_function(char *name, struct program *prog1)
{
    char *ix;
    int i, res;
    
    ix = strchr(name, ':');
    if (ix == NULL)
    {
	/* its a simple name so search for it the normal way */
	return search_for_function(name, prog1);
    }

    if (ix - name == 4 && strncmp(name, "this", 4) == 0)
	return search_for_ext_function(ix + 2, prog1);

    for (i = prog1->num_inherited - 2; i >= 0; i--) 
    {
	if (ix == name || (strlen(prog1->inherit[i].name) == ix - name &&
			   strncmp(prog1->inherit[i].name, name, (size_t)(ix - name)) == 0))
	{
	    res = search_for_ext_function(ix + 2, prog1->inherit[i].prog);
	    if (res)
	    {
		int type_mod;
		
		/* adjust values and return */
		function_inherit_found += i -
		    (prog1->inherit[i].prog->num_inherited - 1);
		type_mod = prog1->inherit[function_inherit_found].type;
		
		/* Correct function_type_mod_found */
		if (function_type_mod_found & TYPE_MOD_PRIVATE)
		    type_mod &= ~TYPE_MOD_PUBLIC;
		if (function_type_mod_found & TYPE_MOD_PUBLIC)
		    type_mod &= ~TYPE_MOD_PRIVATE;
		function_type_mod_found |= type_mod;

		return res;
	    }
	    else
	    {
		/* skip program and continue */
		i -= prog1->inherit[i].prog->num_inherited - 1;
	    }
	    
	}
	
    }
    return 0;
}
