

struct apair {
    struct apair *next;		/* next pair in hashed at this value */
    struct svalue arg, val;	/* index and value for this element */
    short hashval;		/* cashed hash value */
};

struct mapping {
    short size;			/* current size (no of pairs entries) */
    unsigned int ref;		/* reference count */
    short card;			/* number of elements in the mapping */
    short mcard;		/* extend when card exceeds this value */
    struct apair **pairs;	/* array of lists of elements */
};    

void free_mapping (struct mapping *);
struct mapping *allocate_map (short);
short card_mapping (struct mapping *);
struct svalue * get_map_lvalue (struct mapping *, struct svalue *, int);
struct vector * map_domain (struct mapping *);
struct vector * map_codomain (struct mapping *);
struct mapping * make_mapping (struct vector *, struct vector *);
struct mapping * add_mapping (struct mapping *, struct mapping *);
void addto_mapping (struct mapping *, struct mapping *);
struct mapping * remove_mapping (struct mapping *, struct svalue *);
void remove_from_mapping(struct mapping *m, struct svalue *val);
struct mapping * map_map(struct mapping *, struct closure *);
struct mapping * filter_map(struct mapping *, struct closure *);
int free_apairs(struct apair *p);
