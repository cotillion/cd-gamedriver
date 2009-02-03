#pragma strict_types

void
create()
{
    if (break_string("ab cd ef gh ij kl mn op qr", 8) !=
	"ab cd ef\ngh ij kl\nmn op qr")
	throw("break_string inconsistant\n");
}
