/*
 * This file specifies types and arguments for efuns.
 * An argument can have two different types with the syntax 'type1 | type2'.
 * An argument is marked as optional if it also takes the type 'void'.
 *
 * Look at the end for the list of functions that are optionally available.
 * If you don't want them, simply comment out them. All other functions must
 * remain defined.
 */

#include "config.h"

mixed	abs(float|int);
float	acos(float);
float	acosh(float);
void	add_action(string|function, void|string, void|int);
object	*all_inventory(object default: F_THIS_OBJECT);
mixed	*allocate(int);
float	asin(float);
float	asinh(float);
float	atan(float);
float	atan2(float, float);
float	atanh(float);
string	break_string(int|string, int, void|int|string);
mixed	call_other(mapping|object|string|int|object *, string, ...);
mixed	call_otherv(mapping|object|string|int|object *, string, mixed *);
mixed   call_self(string, ...);
mixed   call_selfv(string, mixed *);
string  calling_function(int default: F_CONST0);
object  calling_object(int default: F_CONST0);
string  calling_program(int default: F_CONST0);
string	capitalize(string|int);
string	clear_bit(string, int);
object	clone_object(string);
int	command(string);
mixed  *commands(object|int default: F_THIS_OBJECT);
float	cos(float);
float	cosh(float);
string	crypt(string, string|int);
string	ctime(int);
mixed	debug(string, ...);
object	*deep_inventory(int|object default: F_THIS_OBJECT);
void	destruct();
void	disable_commands();
void	ed(void|string, void|string|function);
void	enable_commands(void);
object	environment(object default: F_THIS_OBJECT);
int	exec(object|int, object);
float	exp(float);
string	*explode(string, string);
string	extract(string, void|int, void|int);
float	fact(float);
string	file_name(object default: F_THIS_OBJECT);
int	file_size(string);
int	file_time(string);
mixed	*filter(int|mapping|mixed *, string|function, void|object|string, void|mixed);
mixed	find_living(string, void|int);
object	find_object(string);
object	find_player(string);
int     floatp(mixed);
string	ftoa(float);
int	ftoi(float);
string	function_exists(string, object default: F_THIS_OBJECT);
string	function_name(function);
object	function_object(function);
int	functionp(mixed);
float 	gettimeofday();
mixed   *get_alarm(int);
mixed   *get_all_alarms();
string	*get_dir(string);
string	implode(int|string *, string);
void	input_to(string|function, ...);
int	intp(mixed);
float	itof(int);
int     last_reference_time();
int	living(object|int);
float	log(float);
string	lower_case(int|string);
mapping	m_delete(int|mapping, mixed);
void	m_delkey(mapping, mixed);
mixed	*m_indexes(int|mapping);
void    m_restore_object(mapping);
mapping m_save_object();
int	m_sizeof(int|mapping);
mixed	*m_values(int|mapping);
mixed	*map(int|mapping|mixed *, string|function, void|object|string, void|mixed);
int	mappingp(mixed);
mixed	match_path(mapping, string);
mixed	max(int|string|float, ...);
int	member_array(mixed, int|mixed *);
mixed	min(int|string|float, ...);
int	mkdir(string);
function mkfunction(string, object default: F_THIS_OBJECT);
mapping	mkmapping(int|mixed *, int|mixed *);
void	move_object(object|string);
int	notify_fail(string, void|int);
float	nrnd(void|float, void|float);
object *object_clones(object);
int	object_time(object default: F_THIS_OBJECT);
int	objectp(mixed);
void	obsolete(string);
function papplyv(function, mixed *);
int	pointerp(mixed);
float	pow(float, float);
object	present(int|object|string, object *|object default: F_THIS_OBJECT);
object	previous_object(int default: F_CONST0);
string	process_string(string, int default: F_CONST0); 
mixed	process_value(string, int default: F_CONST0); 
mixed	query_auth(object);
string	query_host_name();
int	query_idle(object);
int	query_interactive(object|int);
string  query_ip_ident(object default: F_THIS_OBJECT);
string	query_ip_name(void|object);
string	query_ip_number(void|object);
string	query_living_name(object);
object	query_snoop(object);
string	query_trigverb();
string	query_verb();
int	random(int, void|int);
string	read_bytes(string, void|int, void|int);
string	read_file(string, void|int, void|int); 
string  readable_string(string);
mixed reduce(function, mixed, void|mixed);
string	*regexp(string *, string);
void    remove_alarm(int);
int	rename(string, string);
mapping	restore_map(string);
int	restore_object(string);
int	rm(string);
int	rmdir(string);
float	rnd(void|int);
void	save_map(mapping, string);
void	save_object(string);
int     set_alarm(float, float, string|function, ...);
int     set_alarmv(float, float, string, mixed *);
void	set_auth(object,mixed);
string	set_bit(string, int);
void	set_living_name(string);
void	set_this_player(int|object default: F_THIS_OBJECT);
object	shadow(object, int);
float	sin(float);
float	sinh(float);
int	sizeof(int|mixed *);
object	snoop(void|object, void|object);
string	sprintf(string, ...);
float	sqrt(float);
mixed   str2val(string);
int	stringp(mixed);
int	strlen(int|string);
void	tail(string);
float	tan(float);
float	tanh(float);
int	test_bit(string, int);
object	this_interactive();
object	this_object();
object	this_player();
int	time();
int	typeof(mixed);
mixed	*unique_array(int|mixed *, string|function, void|mixed);
void	update_actions(object default: F_THIS_OBJECT);
string	upper_case(int|string);
object	*users();
string  val2str(mixed);
int	wildmatch(string, string|int);
int	write_bytes(string, int, string);
int	write_file(string, string);
void    write_socket(string|int);
void    write_socket_gmcp(string, mixed);
string  val2json(mixed);

#ifdef WORD_WRAP
/*
 * These are needed to control the word wrap mechanism in comm1.
 */
void set_screen_width(int);
int query_screen_width();
#endif /* WORD_WRAP */
