#include <stdio.h>
#include <string.h>
#if defined(sun) || defined(__osf__)
#include <alloca.h>
#endif

#include "../lint.h"
#include "../interface.h"
#include "../object.h"
#include "../mapping.h"

/* Define variables */
extern struct object *previous_ob;

/* Define functions */

char lower(char c)
{
    char   ret;

    ret = c;
    if (ret < 96)
        ret += 32;
    return ret;
}

static void
global_article(struct svalue *fp)
{
    if (fp->type != T_STRING)
    {
        push_number(0);
            return;
    }

    size_t len = strlen(fp->u.string);
    if (len < 1 || len > 100) {
        push_string("", STRING_CSTRING);
        return;
    }

    char *tmp = alloca(len + 2);
    (void)strcpy(tmp, (char *)(fp[0].u.string));
    (void)strcat(tmp, " ");
    tmp[0] = (char)lower(tmp[0]);

    if (!strncmp(tmp, "the ", 4))
    {
        push_string("", STRING_CSTRING);
        return;
    }
    if (tmp[0] == 'a' || tmp[0] == 'e' || tmp[0] == 'i' ||
        tmp[0] == 'o' || tmp[0] == 'u')
    {
        push_string("an", STRING_CSTRING);
        return;
    }
    push_string("a", STRING_CSTRING);
    return;
}

static func func_article =
{
    "article",
    global_article,
};

static char *irregular_plurals[][2] = {
    { "addendum", "addenda" },
    { "aircraft", "aircraft" },
    { "alga", "algae" },
    { "alumna", "alumnae" },
    { "alumnus", "alumni" },
    { "amoeba", "amoebae" },
    { "analysis", "analyses" },
    { "antenna", "antennae" },
    { "antithesis", "antitheses" },
    { "apex", "apices" },
    { "appendix", "appendices" },
    { "automaton", "automata" },
    { "axis", "axes" },
    { "bacillus", "bacilli" },
    { "bacterium", "bacteria" },
    { "barracks", "barracks" },
    { "basis", "bases" },
    { "beau", "beaux" },
    { "bison", "bison" },
    { "buffalo", "buffalo" },
    { "bureau", "bureaus" },
    { "cactus", "cacti" },
    { "calf", "calves" },
    { "carp", "carp" },
    { "census", "censuses" },
    { "cestus", "cesti" },
    { "chassis", "chassis" },
    { "cherub", "cherubim" },
    { "child", "children" },
    { "château", "châteaus" },
    { "cod", "cod" },
    { "codex", "codices" },
    { "concerto", "concerti" },
    { "corpus", "corpora" },
    { "crisis", "crises" },
    { "criterion", "criteria" },
    { "curriculum", "curricula" },
    { "datum", "data" },
    { "deer", "deer" },
    { "diagnosis", "diagnoses" },
    { "die", "dice" },
    { "dwarf", "dwarves" },     /* We use the tolkien plural */
    { "drow", "drow" },
    { "echo", "echoes" },
    { "elf", "elves" },
    { "elk", "elk" },
    { "ellipsis", "ellipses" },
    { "embargo", "embargoes" },
    { "emphasis", "emphases" },
    { "erratum", "errata" },
    { "faux pas", "faux pas" },
    { "fez", "fezes" },
    { "firmware", "firmware" },
    { "fish", "fish" },
    { "focus", "foci" },
    { "foot", "feet" },
    { "formula", "formulae" },
    { "fungus", "fungi" },
    { "gallows", "gallows" },
    { "genus", "genera" },
    { "goose", "geese" },
    { "graffito", "graffiti" },
    { "grouse", "grouse" },
    { "half", "halves" },
    { "hero", "heroes" },
    { "hoof", "hooves" },
    { "hovercraft", "hovercraft" },
    { "hypothesis", "hypotheses" },
    { "index", "indices" },
    { "kakapo", "kakapo" },
    { "knife", "knives" },
    { "larva", "larvae" },
    { "leaf", "leaves" },
    { "libretto", "libretti" },
    { "life", "lives" },
    { "loaf", "loaves" },
    { "locus", "loci" },
    { "louse", "lice" },
    { "man", "men" },
    { "matrix", "matrices" },
    { "means", "means" },
    { "medium", "media" },
    { "media", "media" },
    { "memorandum", "memoranda" },
    { "millennium", "millennia" },
    { "minutia", "minutiae" },
    { "moose", "moose" },
    { "mouse", "mice" },
    { "nebula", "nebulae" },
    { "nemesis", "nemeses" },
    { "neurosis", "neuroses" },
    { "news", "news" },
    { "nucleus", "nuclei" },
    { "oasis", "oases" },
    { "offspring", "offspring" },
    { "opus", "opera" },
    { "ovum", "ova" },
    { "ox", "oxen" },
    { "paralysis", "paralyses" },
    { "parenthesis", "parentheses" },
    { "person", "people" },
    { "phenomenon", "phenomena" },
    { "phylum", "phyla" },
    { "pike", "pikes" },            /* We use the plural form of the weapon */
    { "polyhedron", "polyhedra" },
    { "potato", "potatoes" },
    { "prognosis", "prognoses" },
    { "quiz", "quizzes" },
    { "radius", "radii" },
    { "referendum", "referenda" },
    { "salmon", "salmon" },
    { "scarf", "scarves" },
    { "self", "selves" },
    { "series", "series" },
    { "sheep", "sheep" },
    { "shelf", "shelves" },
    { "shrimp", "shrimp" },
    { "spacecraft", "spacecraft" },
    { "species", "species" },
    { "spectrum", "spectra" },
    { "squid", "squid" },
    { "stimulus", "stimuli" },
    { "stratum", "strata" },
    { "swine", "swine" },
    { "syllabus", "syllabi" },
    { "symposium", "symposia" },
    { "synopsis", "synopses" },
    { "synthesis", "syntheses" },
    { "tableau", "tableaus" },
    { "that", "those" },
    { "thesis", "theses" },
    { "thief", "thieves" },
    { "this", "these" },
    { "tomato", "tomatoes" },
    { "tooth", "teeth" },
    { "trout", "trout" },
    { "tuna", "tuna" },
    { "vertebra", "vertebrae" },
    { "vertex", "vertices" },
    { "veto", "vetoes" },
    { "vita", "vitae" },
    { "vortex", "vortices" },
    { "watercraft", "watercraft" },
    { "wharf", "wharves" },
    { "wife", "wives" },
    { "wolf", "wolves" },
    { "woman", "women" },
};

struct mapping *init_irregular_plural_word_map()
{
    struct mapping *map = allocate_map(128);
    struct svalue key, value;

    for (size_t i = 0; i < sizeof(irregular_plurals) / sizeof(char *) / 2; i++) {
        key.type = T_STRING;
        key.string_type = STRING_CSTRING;
        key.u.string = irregular_plurals[i][0];

        value.type = T_STRING;
        value.string_type = STRING_CSTRING;
        value.u.string = irregular_plurals[i][1];

        assign_svalue(get_map_lvalue(map, &key, 1), &value);
    }

    return map;
}

static struct mapping *plural_map = NULL;

static void
global_plural_word(struct svalue *fp)
{
    if (NULL == plural_map)
        plural_map = init_irregular_plural_word_map();

    if (fp->type != T_STRING)
    {
            push_number(0);
            return;
    }

    char *str = fp->u.string;
    struct svalue *match = get_map_lvalue(plural_map, fp, 0);
    if (match->type == T_STRING)
    {
        push_svalue(match);
        return;
    }

    size_t sl = strlen(str);
    if (sl < 3 || sl > 100)
    {
        push_svalue(fp);
            return;
    }

    char *tmp = alloca(sl + 10);
    (void)strncpy(tmp, str, sl);
    char ultimate = str[sl - 1];
    char penultimate = str[sl - 2];
    tmp[sl] = '\0';

    switch(ultimate)
    {
    case 's':
    case 'x':
    case 'h':
        (void)strcat(tmp, "es");
        break;
    case 'y':
        if (penultimate == 'a' || penultimate == 'e' || penultimate == 'o')
            (void)strcat(tmp, "s");
        else
        {
            tmp[sl - 1] = '\0';
            (void)strcat(tmp, "ies");
        }
        break;
    case 'e':
        if (penultimate == 'f')
        {
            tmp[sl - 2] = '\0';
            (void)strcat(tmp, "ves");
        }
        else
            (void)strcat(tmp, "s");
        break;
    case 'f':
        if (penultimate == 'f')
        {
            tmp[sl - 2] = '\0';
        }
        tmp[sl - 1] = '\0';
        (void)strcat(tmp, "ves");
        break;
    default:
        (void)strcat(tmp, "s");
        break;
    }
    push_string(tmp, STRING_MSTRING);
    return;
}

static func func_plural_word =
{
    "plural_word",
    global_plural_word,
};


static func *(funcs[]) =
{
    &func_article,
    &func_plural_word,
    0,
};
static var *(vars[]) =
{
   0,
};

struct interface gl_language =
{
    "sys/global/language.c",
    vars,
    funcs,
};





