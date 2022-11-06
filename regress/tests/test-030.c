/*
 This is a test for a very big program sizes.
 All the defines serve only to multiply number of opcodes in program, it does not really matter which opcodes.
 Should pass after program-size fixes.
*/ 

#define L_1 (1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20)
#define L_2 (L_1|L_1|L_1^L_1^L_1^L_1^L_1^L_1^L_1^L_1^L_1^L_1^L_1&L_1&L_1&L_1|L_1|L_1|L_1|L_1|L_1|L_1|L_1)
#define L_3 (L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2|L_2)
void
create()
{
    int * a = ({L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3,L_3, L_3});
    write(debug("object_info", 1, this_object()));
    write("Huge program size ");
}
