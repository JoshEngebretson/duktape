/*
 *  Heap object representation.
 *
 *  Heap objects are used for Ecmascript objects, arrays, and functions,
 *  but also for internal control like declarative and object environment
 *  records.  Compiled functions, native functions, and threads are also
 *  objects but with an extended C struct.
 *
 *  Objects provide the required Ecmascript semantics and special behaviors
 *  especially for property access.
 *
 *  Properties are stored in three conceptual parts:
 *
 *    1. A linear 'entry part' contains ordered key-value-attributes triples
 *       and is the main method of string properties.
 *
 *    2. An optional linear 'array part' is used for array objects to store a
 *       (dense) range of [0,N[ array indexed entries with default attributes
 *       (writable, enumerable, configurable).  If the array part would become
 *       sparse or non-default attributes are required, the array part is
 *       abandoned and moved to the 'entry part'.
 *
 *    3. An optional 'hash part' is used to optimize lookups of the entry
 *       part; it is used only for objects with sufficiently many properties 
 *       and can be abandoned without loss of information.
 *
 *  These three conceptual parts are stored in a single memory allocated area.
 *  This minimizes memory allocation overhead but also means that all three
 *  parts are resized together, and makes property access a bit complicated.
 */

#ifndef DUK_HOBJECT_H_INCLUDED
#define DUK_HOBJECT_H_INCLUDED

/* there are currently 22 flag bits available */
#define DUK_HOBJECT_FLAG_EXTENSIBLE            DUK_HEAPHDR_USER_FLAG(0)   /* object is extensible */
#define DUK_HOBJECT_FLAG_CONSTRUCTABLE         DUK_HEAPHDR_USER_FLAG(1)   /* object is constructable */
#define DUK_HOBJECT_FLAG_BOUND                 DUK_HEAPHDR_USER_FLAG(2)   /* object established using Function.prototype.bind() */
#define DUK_HOBJECT_FLAG_COMPILEDFUNCTION      DUK_HEAPHDR_USER_FLAG(4)   /* object is a compiled function (duk_hcompiledfunction) */
#define DUK_HOBJECT_FLAG_NATIVEFUNCTION        DUK_HEAPHDR_USER_FLAG(5)   /* object is a native function (duk_hnativefunction) */
#define DUK_HOBJECT_FLAG_THREAD                DUK_HEAPHDR_USER_FLAG(6)   /* object is a thread (duk_hthread) */
#define DUK_HOBJECT_FLAG_ARRAY_PART            DUK_HEAPHDR_USER_FLAG(7)   /* object has an array part (a_size may still be 0) */
#define DUK_HOBJECT_FLAG_STRICT                DUK_HEAPHDR_USER_FLAG(8)   /* function: function object is strict */
#define DUK_HOBJECT_FLAG_NEWENV                DUK_HEAPHDR_USER_FLAG(9)   /* function: create new environment when called (see duk_hcompiledfunction) */
#define DUK_HOBJECT_FLAG_NAMEBINDING           DUK_HEAPHDR_USER_FLAG(10)  /* function: create binding for func name (function templates only, used for named function expressions) */
#define DUK_HOBJECT_FLAG_CREATEARGS            DUK_HEAPHDR_USER_FLAG(11)  /* function: create an arguments object on function call */
#define DUK_HOBJECT_FLAG_ENVRECCLOSED          DUK_HEAPHDR_USER_FLAG(12)  /* envrec: (declarative) record is closed */
#define DUK_HOBJECT_FLAG_SPECIAL_ARRAY         DUK_HEAPHDR_USER_FLAG(13)  /* 'Array' object, array length and index special behavior */
#define DUK_HOBJECT_FLAG_SPECIAL_STRINGOBJ     DUK_HEAPHDR_USER_FLAG(14)  /* 'String' object, array index special behavior */
#define DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS     DUK_HEAPHDR_USER_FLAG(15)  /* 'Arguments' object and has arguments special behavior (non-strict callee) */
/* bit 16 unused */

#define DUK_HOBJECT_FLAG_CLASS_BASE            DUK_HEAPHDR_USER_FLAG_NUMBER(17)
#define DUK_HOBJECT_FLAG_CLASS_BITS            5

#define DUK_HOBJECT_GET_CLASS_NUMBER(h)        \
	DUK_HEAPHDR_GET_FLAG_RANGE(&(h)->hdr, DUK_HOBJECT_FLAG_CLASS_BASE, DUK_HOBJECT_FLAG_CLASS_BITS)
#define DUK_HOBJECT_SET_CLASS_NUMBER(h,v)      \
	DUK_HEAPHDR_SET_FLAG_RANGE(&(h)->hdr, DUK_HOBJECT_FLAG_CLASS_BASE, DUK_HOBJECT_FLAG_CLASS_BITS, (v))

/* for creating flag initializers */
#define DUK_HOBJECT_CLASS_AS_FLAGS(v)          ((v) << DUK_HOBJECT_FLAG_CLASS_BASE)

/* E5 Section 8.6.2 + custom classes */
#define DUK_HOBJECT_CLASS_UNUSED               0
#define DUK_HOBJECT_CLASS_ARGUMENTS            1
#define DUK_HOBJECT_CLASS_ARRAY                2
#define DUK_HOBJECT_CLASS_BOOLEAN              3
#define DUK_HOBJECT_CLASS_DATE                 4
#define DUK_HOBJECT_CLASS_ERROR                5
#define DUK_HOBJECT_CLASS_FUNCTION             6
#define DUK_HOBJECT_CLASS_JSON                 7
#define DUK_HOBJECT_CLASS_MATH                 8
#define DUK_HOBJECT_CLASS_NUMBER               9
#define DUK_HOBJECT_CLASS_OBJECT               10
#define DUK_HOBJECT_CLASS_REGEXP               11
#define DUK_HOBJECT_CLASS_STRING               12
#define DUK_HOBJECT_CLASS_GLOBAL               13
#define DUK_HOBJECT_CLASS_OBJENV               14  /* custom */
#define DUK_HOBJECT_CLASS_DECENV               15  /* custom */
#define DUK_HOBJECT_CLASS_BUFFER               16  /* custom */
#define DUK_HOBJECT_CLASS_POINTER              17  /* custom */
#define DUK_HOBJECT_CLASS_THREAD               18  /* custom */

#define DUK_HOBJECT_IS_OBJENV(h)               (DUK_HOBJECT_GET_CLASS_NUMBER((h)) == DUK_HOBJECT_CLASS_OBJENV)
#define DUK_HOBJECT_IS_DECENV(h)               (DUK_HOBJECT_GET_CLASS_NUMBER((h)) == DUK_HOBJECT_CLASS_DECENV)
#define DUK_HOBJECT_IS_ENV(h)                  (DUK_HOBJECT_IS_OBJENV((h)) || DUK_HOBJECT_IS_DECENV((h)))
#define DUK_HOBJECT_IS_ARRAY(h)                (DUK_HOBJECT_GET_CLASS_NUMBER((h)) == DUK_HOBJECT_CLASS_ARRAY)
#define DUK_HOBJECT_IS_COMPILEDFUNCTION(h)     DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_COMPILEDFUNCTION)
#define DUK_HOBJECT_IS_NATIVEFUNCTION(h)       DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NATIVEFUNCTION)
#define DUK_HOBJECT_IS_THREAD(h)               DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_THREAD)

#define DUK_HOBJECT_IS_NONBOUND_FUNCTION(h)    DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, \
                                                        DUK_HOBJECT_FLAG_COMPILEDFUNCTION | \
                                                        DUK_HOBJECT_FLAG_NATIVEFUNCTION)

#define DUK_HOBJECT_IS_FUNCTION(h)             DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, \
                                                        DUK_HOBJECT_FLAG_BOUND | \
                                                        DUK_HOBJECT_FLAG_COMPILEDFUNCTION | \
                                                        DUK_HOBJECT_FLAG_NATIVEFUNCTION)

#define DUK_HOBJECT_IS_CALLABLE(h)             DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, \
                                                        DUK_HOBJECT_FLAG_BOUND | \
                                                        DUK_HOBJECT_FLAG_COMPILEDFUNCTION | \
                                                        DUK_HOBJECT_FLAG_NATIVEFUNCTION)

/* object has any special behavior(s) */
#define DUK_HOBJECT_SPECIAL_BEHAVIOR_FLAGS     (DUK_HOBJECT_FLAG_SPECIAL_ARRAY | \
                                                 DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS | \
                                                 DUK_HOBJECT_FLAG_SPECIAL_STRINGOBJ)

#define DUK_HOBJECT_HAS_SPECIAL_BEHAVIOR(h)    DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_SPECIAL_BEHAVIOR_FLAGS)

#define DUK_HOBJECT_HAS_EXTENSIBLE(h)          DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_EXTENSIBLE)
#define DUK_HOBJECT_HAS_CONSTRUCTABLE(h)       DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CONSTRUCTABLE)
#define DUK_HOBJECT_HAS_BOUND(h)               DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_BOUND)
#define DUK_HOBJECT_HAS_COMPILEDFUNCTION(h)    DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_COMPILEDFUNCTION)
#define DUK_HOBJECT_HAS_NATIVEFUNCTION(h)      DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NATIVEFUNCTION)
#define DUK_HOBJECT_HAS_THREAD(h)              DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_THREAD)
#define DUK_HOBJECT_HAS_ARRAY_PART(h)          DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ARRAY_PART)
#define DUK_HOBJECT_HAS_STRICT(h)              DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_STRICT)
#define DUK_HOBJECT_HAS_NEWENV(h)              DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NEWENV)
#define DUK_HOBJECT_HAS_NAMEBINDING(h)         DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NAMEBINDING)
#define DUK_HOBJECT_HAS_CREATEARGS(h)          DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CREATEARGS)
#define DUK_HOBJECT_HAS_ENVRECCLOSED(h)        DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ENVRECCLOSED)
#define DUK_HOBJECT_HAS_SPECIAL_ARRAY(h)       DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARRAY)
#define DUK_HOBJECT_HAS_SPECIAL_STRINGOBJ(h)   DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_STRINGOBJ)
#define DUK_HOBJECT_HAS_SPECIAL_ARGUMENTS(h)   DUK_HEAPHDR_CHECK_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS)

#define DUK_HOBJECT_SET_EXTENSIBLE(h)          DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_EXTENSIBLE)
#define DUK_HOBJECT_SET_CONSTRUCTABLE(h)       DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CONSTRUCTABLE)
#define DUK_HOBJECT_SET_BOUND(h)               DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_BOUND)
#define DUK_HOBJECT_SET_COMPILEDFUNCTION(h)    DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_COMPILEDFUNCTION)
#define DUK_HOBJECT_SET_NATIVEFUNCTION(h)      DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NATIVEFUNCTION)
#define DUK_HOBJECT_SET_THREAD(h)              DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_THREAD)
#define DUK_HOBJECT_SET_ARRAY_PART(h)          DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ARRAY_PART)
#define DUK_HOBJECT_SET_STRICT(h)              DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_STRICT)
#define DUK_HOBJECT_SET_NEWENV(h)              DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NEWENV)
#define DUK_HOBJECT_SET_NAMEBINDING(h)         DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NAMEBINDING)
#define DUK_HOBJECT_SET_CREATEARGS(h)          DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CREATEARGS)
#define DUK_HOBJECT_SET_ENVRECCLOSED(h)        DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ENVRECCLOSED)
#define DUK_HOBJECT_SET_SPECIAL_ARRAY(h)       DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARRAY)
#define DUK_HOBJECT_SET_SPECIAL_STRINGOBJ(h)   DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_STRINGOBJ)
#define DUK_HOBJECT_SET_SPECIAL_ARGUMENTS(h)   DUK_HEAPHDR_SET_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS)

#define DUK_HOBJECT_CLEAR_EXTENSIBLE(h)        DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_EXTENSIBLE)
#define DUK_HOBJECT_CLEAR_CONSTRUCTABLE(h)     DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CONSTRUCTABLE)
#define DUK_HOBJECT_CLEAR_BOUND(h)             DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_BOUND)
#define DUK_HOBJECT_CLEAR_COMPILEDFUNCTION(h)  DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_COMPILEDFUNCTION)
#define DUK_HOBJECT_CLEAR_NATIVEFUNCTION(h)    DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NATIVEFUNCTION)
#define DUK_HOBJECT_CLEAR_THREAD(h)            DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_THREAD)
#define DUK_HOBJECT_CLEAR_ARRAY_PART(h)        DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ARRAY_PART)
#define DUK_HOBJECT_CLEAR_STRICT(h)            DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_STRICT)
#define DUK_HOBJECT_CLEAR_NEWENV(h)            DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NEWENV)
#define DUK_HOBJECT_CLEAR_NAMEBINDING(h)       DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_NAMEBINDING)
#define DUK_HOBJECT_CLEAR_CREATEARGS(h)        DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_CREATEARGS)
#define DUK_HOBJECT_CLEAR_ENVRECCLOSED(h)      DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_ENVRECCLOSED)
#define DUK_HOBJECT_CLEAR_SPECIAL_ARRAY(h)     DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARRAY)
#define DUK_HOBJECT_CLEAR_SPECIAL_STRINGOBJ(h) DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_STRINGOBJ)
#define DUK_HOBJECT_CLEAR_SPECIAL_ARGUMENTS(h) DUK_HEAPHDR_CLEAR_FLAG_BITS(&(h)->hdr, DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS)

/* flags used for property attributes in duk_propdesc and packed flags */
#define DUK_PROPDESC_FLAG_WRITABLE              (1 << 0)    /* E5 Section 8.6.1 */
#define DUK_PROPDESC_FLAG_ENUMERABLE            (1 << 1)    /* E5 Section 8.6.1 */
#define DUK_PROPDESC_FLAG_CONFIGURABLE          (1 << 2)    /* E5 Section 8.6.1 */
#define DUK_PROPDESC_FLAG_ACCESSOR              (1 << 3)    /* accessor */
#define DUK_PROPDESC_FLAGS_MASK                 (DUK_PROPDESC_FLAG_WRITABLE | \
                                                 DUK_PROPDESC_FLAG_ENUMERABLE | \
                                                 DUK_PROPDESC_FLAG_CONFIGURABLE | \
                                                 DUK_PROPDESC_FLAG_ACCESSOR)

/* additional flags which are passed in the same flags argument as property
 * flags but are not stored in object properties.
 */
#define DUK_PROPDESC_FLAG_NO_OVERWRITE          (1 << 4)    /* internal define property: skip write silently if exists */

/* convenience */
#define DUK_PROPDESC_FLAGS_NONE                 0
#define DUK_PROPDESC_FLAGS_W                    (DUK_PROPDESC_FLAG_WRITABLE)
#define DUK_PROPDESC_FLAGS_E                    (DUK_PROPDESC_FLAG_ENUMERABLE)
#define DUK_PROPDESC_FLAGS_C                    (DUK_PROPDESC_FLAG_CONFIGURABLE)
#define DUK_PROPDESC_FLAGS_WE                   (DUK_PROPDESC_FLAG_WRITABLE | DUK_PROPDESC_FLAG_ENUMERABLE)
#define DUK_PROPDESC_FLAGS_WC                   (DUK_PROPDESC_FLAG_WRITABLE | DUK_PROPDESC_FLAG_CONFIGURABLE)
#define DUK_PROPDESC_FLAGS_EC                   (DUK_PROPDESC_FLAG_ENUMERABLE | DUK_PROPDESC_FLAG_CONFIGURABLE)
#define DUK_PROPDESC_FLAGS_WEC                  (DUK_PROPDESC_FLAG_WRITABLE | \
                                                 DUK_PROPDESC_FLAG_ENUMERABLE | \
                                                 DUK_PROPDESC_FLAG_CONFIGURABLE)

/*
 *  Macros to access the 'p' allocation.
 */

#define DUK_HOBJECT_E_GET_KEY_BASE(h)           \
	((duk_hstring **) ( \
		(h)->p \
	))
#define DUK_HOBJECT_E_GET_VALUE_BASE(h)         \
	((duk_propvalue *) ( \
		(h)->p + \
			(h)->e_size * sizeof(duk_hstring *) \
	))
#define DUK_HOBJECT_E_GET_FLAGS_BASE(h)         \
	((duk_uint8_t *) ( \
		(h)->p + (h)->e_size * (sizeof(duk_hstring *) + sizeof(duk_propvalue)) \
	))
#define DUK_HOBJECT_A_GET_BASE(h)               \
	((duk_tval *) ( \
		(h)->p + \
			(h)->e_size * (sizeof(duk_hstring *) + sizeof(duk_propvalue) + sizeof(duk_uint8_t)) \
	))
#define DUK_HOBJECT_H_GET_BASE(h)               \
	((duk_uint32_t *) ( \
		(h)->p + \
			(h)->e_size * (sizeof(duk_hstring *) + sizeof(duk_propvalue) + sizeof(duk_uint8_t)) + \
			(h)->a_size * sizeof(duk_tval) \
	))

#define DUK_HOBJECT_P_COMPUTE_SIZE(n_ent,n_arr,n_hash) \
	( \
		(n_ent) * (sizeof(duk_hstring *) + sizeof(duk_propvalue) + sizeof(duk_uint8_t)) + \
		(n_arr) * sizeof(duk_tval) + \
		(n_hash) * sizeof(duk_uint32_t) \
	)

#define DUK_HOBJECT_E_GET_KEY(h,i)              (DUK_HOBJECT_E_GET_KEY_BASE((h))[(i)])
#define DUK_HOBJECT_E_GET_KEY_PTR(h,i)          (&DUK_HOBJECT_E_GET_KEY_BASE((h))[(i)])
#define DUK_HOBJECT_E_GET_VALUE(h,i)            (DUK_HOBJECT_E_GET_VALUE_BASE((h))[(i)])
#define DUK_HOBJECT_E_GET_VALUE_PTR(h,i)        (&DUK_HOBJECT_E_GET_VALUE_BASE((h))[(i)])
#define DUK_HOBJECT_E_GET_VALUE_TVAL(h,i)       (DUK_HOBJECT_E_GET_VALUE((h),(i)).v)
#define DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(h,i)   (&DUK_HOBJECT_E_GET_VALUE((h),(i)).v)
#define DUK_HOBJECT_E_GET_VALUE_GETTER(h,i)     (DUK_HOBJECT_E_GET_VALUE((h),(i)).a.get)
#define DUK_HOBJECT_E_GET_VALUE_GETTER_PTR(h,i) (&DUK_HOBJECT_E_GET_VALUE((h),(i)).a.get)
#define DUK_HOBJECT_E_GET_VALUE_SETTER(h,i)     (DUK_HOBJECT_E_GET_VALUE((h),(i)).a.set)
#define DUK_HOBJECT_E_GET_VALUE_SETTER_PTR(h,i) (&DUK_HOBJECT_E_GET_VALUE((h),(i)).a.set)
#define DUK_HOBJECT_E_GET_FLAGS(h,i)            (DUK_HOBJECT_E_GET_FLAGS_BASE((h))[(i)])
#define DUK_HOBJECT_E_GET_FLAGS_PTR(h,i)        (&DUK_HOBJECT_E_GET_FLAGS_BASE((h))[(i)])
#define DUK_HOBJECT_A_GET_VALUE(h,i)            (DUK_HOBJECT_A_GET_BASE((h))[(i)])
#define DUK_HOBJECT_A_GET_VALUE_PTR(h,i)        (&DUK_HOBJECT_A_GET_BASE((h))[(i)])
#define DUK_HOBJECT_H_GET_INDEX(h,i)            (DUK_HOBJECT_H_GET_BASE((h))[(i)])
#define DUK_HOBJECT_H_GET_INDEX_PTR(h,i)        (&DUK_HOBJECT_H_GET_BASE((h))[(i)])

#define DUK_HOBJECT_E_SET_KEY(h,i,k)  do { \
		DUK_HOBJECT_E_GET_KEY((h),(i)) = (k); \
	} while (0)
#define DUK_HOBJECT_E_SET_VALUE(h,i,v)  do { \
		DUK_HOBJECT_E_GET_VALUE((h),(i)) = (v); \
	} while (0)
#define DUK_HOBJECT_E_SET_VALUE_TVAL(h,i,v)  do { \
		DUK_HOBJECT_E_GET_VALUE((h),(i)).v = (v); \
	} while (0)
#define DUK_HOBJECT_E_SET_VALUE_GETTER(h,i,v)  do { \
		DUK_HOBJECT_E_GET_VALUE((h),(i)).a.get = (v); \
	} while (0)
#define DUK_HOBJECT_E_SET_VALUE_SETTER(h,i,v)  do { \
		DUK_HOBJECT_E_GET_VALUE((h),(i)).a.set = (v); \
	} while (0)
#define DUK_HOBJECT_E_SET_FLAGS(h,i,f)  do { \
		DUK_HOBJECT_E_GET_FLAGS((h),(i)) = (f); \
	} while (0)
#define DUK_HOBJECT_A_SET_VALUE(h,i,v)  do { \
		DUK_HOBJECT_A_GET_VALUE((h),(i)) = (v); \
	} while (0)
#define DUK_HOBJECT_A_SET_VALUE_TVAL(h,i,v)  DUK_HOBJECT_A_SET_VALUE((h),(i),(v))  /* alias for above */
#define DUK_HOBJECT_H_SET_INDEX(h,i,v)  do { \
		DUK_HOBJECT_H_GET_INDEX((h),(i)) = (v); \
	} while (0)

#define DUK_HOBJECT_E_SET_FLAG_BITS(h,i,mask)  do { \
		DUK_HOBJECT_E_GET_FLAGS_BASE((h))[(i)] |= (mask); \
	} while (0)

#define DUK_HOBJECT_E_CLEAR_FLAG_BITS(h,i,mask)  do { \
		DUK_HOBJECT_E_GET_FLAGS_BASE((h))[(i)] &= ~(mask); \
	} while (0)

#define DUK_HOBJECT_E_SLOT_IS_WRITABLE(h,i)     ((DUK_HOBJECT_E_GET_FLAGS((h),(i)) & DUK_PROPDESC_FLAG_WRITABLE) != 0)
#define DUK_HOBJECT_E_SLOT_IS_ENUMERABLE(h,i)   ((DUK_HOBJECT_E_GET_FLAGS((h),(i)) & DUK_PROPDESC_FLAG_ENUMERABLE) != 0)
#define DUK_HOBJECT_E_SLOT_IS_CONFIGURABLE(h,i) ((DUK_HOBJECT_E_GET_FLAGS((h),(i)) & DUK_PROPDESC_FLAG_CONFIGURABLE) != 0)
#define DUK_HOBJECT_E_SLOT_IS_ACCESSOR(h,i)     ((DUK_HOBJECT_E_GET_FLAGS((h),(i)) & DUK_PROPDESC_FLAG_ACCESSOR) != 0)

#define DUK_HOBJECT_E_SLOT_SET_WRITABLE(h,i)        DUK_HOBJECT_E_SET_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_WRITABLE)
#define DUK_HOBJECT_E_SLOT_SET_ENUMERABLE(h,i)      DUK_HOBJECT_E_SET_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_ENUMERABLE)
#define DUK_HOBJECT_E_SLOT_SET_CONFIGURABLE(h,i)    DUK_HOBJECT_E_SET_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_CONFIGURABLE)
#define DUK_HOBJECT_E_SLOT_SET_ACCESSOR(h,i)        DUK_HOBJECT_E_SET_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_ACCESSOR)

#define DUK_HOBJECT_E_SLOT_CLEAR_WRITABLE(h,i)      DUK_HOBJECT_E_CLEAR_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_WRITABLE)
#define DUK_HOBJECT_E_SLOT_CLEAR_ENUMERABLE(h,i)    DUK_HOBJECT_E_CLEAR_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_ENUMERABLE)
#define DUK_HOBJECT_E_SLOT_CLEAR_CONFIGURABLE(h,i)  DUK_HOBJECT_E_CLEAR_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_CONFIGURABLE)
#define DUK_HOBJECT_E_SLOT_CLEAR_ACCESSOR(h,i)      DUK_HOBJECT_E_CLEAR_FLAG_BITS((h),(i),DUK_PROPDESC_FLAG_ACCESSOR)

#define DUK_PROPDESC_IS_WRITABLE(p)             (((p)->flags & DUK_PROPDESC_FLAG_WRITABLE) != 0)
#define DUK_PROPDESC_IS_ENUMERABLE(p)           (((p)->flags & DUK_PROPDESC_FLAG_ENUMERABLE) != 0)
#define DUK_PROPDESC_IS_CONFIGURABLE(p)         (((p)->flags & DUK_PROPDESC_FLAG_CONFIGURABLE) != 0)
#define DUK_PROPDESC_IS_ACCESSOR(p)             (((p)->flags & DUK_PROPDESC_FLAG_ACCESSOR) != 0)

#define DUK_HOBJECT_HASHIDX_UNUSED              0xffffffffUL
#define DUK_HOBJECT_HASHIDX_DELETED             0xfffffffeUL

/*
 *  Misc
 */

/* Maximum prototype traversal depth.  Sanity limit which handles e.g.
 * prototype loops (even complex ones like 1->2->3->4->2->3->4->2->3->4).
 */
#define DUK_HOBJECT_PROTOTYPE_CHAIN_SANITY      10000L

/* Maximum traversal depth for "bound function" chains. */
#define DUK_HOBJECT_BOUND_CHAIN_SANITY          10000L

/*
 *  Ecmascript [[Class]]
 */

/* range check not necessary because all 4-bit values are mapped */
#define DUK_HOBJECT_CLASS_NUMBER_TO_STRIDX(n)  duk_class_number_to_stridx[(n)]

#define DUK_HOBJECT_GET_CLASS_STRING(heap,h)          \
	DUK_HEAP_GET_STRING( \
		(heap), \
		DUK_HOBJECT_CLASS_NUMBER_TO_STRIDX(DUK_HOBJECT_GET_CLASS_NUMBER((h))) \
	)

/*
 *  Macros for property handling
 */		

/* note: this updates refcounts */
#define DUK_HOBJECT_SET_PROTOTYPE(thr,h,p)              duk_hobject_set_prototype((thr),(h),(p))

/*
 *  Macros for Ecmascript built-in semantics
 */

#define DUK_HOBJECT_OBJECT_SEAL(thr,obj)                duk_hobject_object_seal_freeze_helper((thr),(obj),0)
#define DUK_HOBJECT_OBJECT_FREEZE(htr,obj)              duk_hobject_object_seal_freeze_helper((thr),(obj),1)
#define DUK_HOBJECT_OBJECT_IS_SEALED(obj)               duk_hobject_object_is_sealed_frozen_helper((obj),0)
#define DUK_HOBJECT_OBJECT_IS_FROZEN(obj)               duk_hobject_object_is_sealed_frozen_helper((obj),1)
#define DUK_HOBJECT_OBJECT_PREVENT_EXTENSIONS(vm,obj)   DUK_HOBJECT_CLEAR_EXTENSIBLE((obj))
#define DUK_HOBJECT_OBJECT_IS_EXTENSIBLE(vm,obj)        DUK_HOBJECT_HAS_EXTENSIBLE((obj))

/*
 *  Resizing and hash behavior
 */

/* Sanity limit on max number of properties (allocated, not necessarily used).
 * This is somewhat arbitrary, but if we're close to 2**32 properties some
 * algorithms will fail (e.g. hash size selection, next prime selection).
 * Also, we use negative array/entry table indices to indicate 'not found',
 * so anything above 0x80000000 will cause trouble now.
 */
#define DUK_HOBJECT_MAX_PROPERTIES       0x7fffffffU   /* 2**31-1 ~= 2G properties */

/* higher value conserves memory; also note that linear scan is cache friendly */
#define DUK_HOBJECT_E_USE_HASH_LIMIT     32

/* hash size relative to entries size: for value X, approx. hash_prime(e_size + e_size / X) */
#define DUK_HOBJECT_H_SIZE_DIVISOR       4  /* hash size approx. 1.25 times entries size */

/* if new_size < L * old_size, resize without abandon check; L = 3-bit fixed point, e.g. 9 -> 9/8 = 112.5% */
#define DUK_HOBJECT_A_FAST_RESIZE_LIMIT  9  /* 112.5%, i.e. new size less than 12.5% higher -> fast resize */

/* if density < L, abandon array part, L = 3-bit fixed point, e.g. 2 -> 2/8 = 25% */
/* limit is quite low: one array entry is 8 bytes, one normal entry is 4+1+8+4 = 17 bytes (with hash entry) */
#define DUK_HOBJECT_A_ABANDON_LIMIT      2  /* 25%, i.e. less than 25% used -> abandon */

/* FIXME: where to get align target (now fixed to 4)? */
/* internal align target for props allocation, must be 2*n for some n */
#define DUK_HOBJECT_ALIGN_TARGET         4

/* controls for minimum entry part growth */
#define DUK_HOBJECT_E_MIN_GROW_ADD       16
#define DUK_HOBJECT_E_MIN_GROW_DIVISOR   8  /* 2^3 -> 1/8 = 12.5% min growth */

/* controls for minimum array part growth */
#define DUK_HOBJECT_A_MIN_GROW_ADD       16
#define DUK_HOBJECT_A_MIN_GROW_DIVISOR   8  /* 2^3 -> 1/8 = 12.5% min growth */

/* probe sequence */
#define DUK_HOBJECT_HASH_INITIAL(hash,h_size)  ((hash) % (h_size))
#define DUK_HOBJECT_HASH_PROBE_STEP(hash)      DUK_UTIL_GET_HASH_PROBE_STEP((hash))

/*
 *  PC-to-line constants
 */

#define DUK_PC2LINE_SKIP    64

/* maximum length for a SKIP-1 diffstream: 35 bits per entry, rounded up to bytes */
#define DUK_PC2LINE_MAX_DIFF_LENGTH    (((DUK_PC2LINE_SKIP - 1) * 35 + 7) / 8)

/*
 *  Struct defs
 */

struct duk_propaccessor {
	duk_hobject *get;
	duk_hobject *set;
};

union duk_propvalue {
	duk_tval v;
	duk_propaccessor a;
};

struct duk_propdesc {
	/* read-only values 'lifted' for ease of use */
	int flags;
	duk_hobject *get;
	duk_hobject *set;

	/* for updating (all are set to < 0 for virtual properties) */
	int e_idx;	/* prop index in 'entry part', < 0 if not there */
	int h_idx;	/* prop index in 'hash part', < 0 if not there */
	int a_idx;	/* prop index in 'array part', < 0 if not there */
};

struct duk_hobject {
	duk_heaphdr hdr;

	/*
	 *  'p' contains {key,value,flags} entries, optional array entries, and an
	 *  optional hash lookup table for non-array entries in a single 'sliced'
	 *  allocation:
	 *
	 *    e_size * sizeof(duk_hstring *)   bytes of   entry keys (e_used gc reachable)
	 *    e_size * sizeof(duk_propvalue)   bytes of   entry values (e_used gc reachable)
	 *    e_size * sizeof(duk_uint8_t)     bytes of   entry flags (e_used gc reachable)
	 *    a_size * sizeof(duk_tval)        bytes of   (opt) array values (plain only) (all gc reachable)
	 *    h_size * sizeof(duk_uint32_t)    bytes of   (opt) hash indexes to entries (e_size),
	 *                                                0xffffffffU = unused, 0xfffffffeU = deleted
	 *
	 *  Objects with few keys don't have a hash index; keys are looked up linearly,
	 *  which is cache efficient because the keys are consecutive.  Larger objects
	 *  have a hash index part which contains integer indexes to the entries part.
	 *
	 *  A single allocation reduces memory allocation overhead but requires more
	 *  work when any part needs to be resized.  A sliced allocation for entries
	 *  makes linear key matching faster on most platforms (more locality) and
	 *  skimps on flags size (which would be followed by 3 bytes of padding in
	 *  most architectures if entries were placed in a struct).
	 *
	 *  'p' also contains internal properties distinguished with a non-BMP
	 *  prefix.  Often used properties should be placed early in 'p' whenever
	 *  possible to make accessing them as fast a possible.
	 */

	duk_uint8_t *p;
	duk_uint32_t e_size;
	duk_uint32_t e_used;
	duk_uint32_t a_size;
	duk_uint32_t h_size;

	/* prototype: the only internal property lifted outside 'e' as it is so central */
	duk_hobject *prototype;
};

/*
 *  Exposed data
 */

extern duk_uint8_t duk_class_number_to_stridx[32];

/*
 *  Prototypes
 */

/* alloc and init */
duk_hobject *duk_hobject_alloc(duk_heap *heap, int hobject_flags);
duk_hobject *duk_hobject_alloc_checked(duk_hthread *thr, int hobject_flags);
duk_hcompiledfunction *duk_hcompiledfunction_alloc(duk_heap *heap, int hobject_flags);
duk_hnativefunction *duk_hnativefunction_alloc(duk_heap *heap, int hobject_flags);
duk_hthread *duk_hthread_alloc(duk_heap *heap, int hobject_flags);

/* low-level property functions */
void duk_hobject_find_existing_entry(duk_hobject *obj, duk_hstring *key, int *e_idx, int *h_idx);
duk_tval *duk_hobject_find_existing_entry_tval_ptr(duk_hobject *obj, duk_hstring *key);
duk_tval *duk_hobject_find_existing_entry_tval_ptr_and_attrs(duk_hobject *obj, duk_hstring *key, duk_int_t *out_attrs);
duk_tval *duk_hobject_find_existing_array_entry_tval_ptr(duk_hobject *obj, duk_uint32_t i);

/* core property functions */
int duk_hobject_getprop(duk_hthread *thr, duk_tval *tv_obj, duk_tval *tv_key);
int duk_hobject_putprop(duk_hthread *thr, duk_tval *tv_obj, duk_tval *tv_key, duk_tval *tv_val, int throw_flag);
int duk_hobject_delprop(duk_hthread *thr, duk_tval *tv_obj, duk_tval *tv_key, int throw_flag);
int duk_hobject_hasprop(duk_hthread *thr, duk_tval *tv_obj, duk_tval *tv_key);

/* internal property functions */
int duk_hobject_delprop_raw(duk_hthread *thr, duk_hobject *obj, duk_hstring *key, int throw_flag);
int duk_hobject_hasprop_raw(duk_hthread *thr, duk_hobject *obj, duk_hstring *key);
void duk_hobject_define_property_internal(duk_hthread *thr, duk_hobject *obj, duk_hstring *key, int propflags);
void duk_hobject_define_accessor_internal(duk_hthread *thr, duk_hobject *obj, duk_hstring *key, duk_hobject *getter, duk_hobject *setter, int propflags);
void duk_hobject_set_length(duk_hthread *thr, duk_hobject *obj, duk_uint32_t length);
void duk_hobject_set_length_zero(duk_hthread *thr, duk_hobject *obj);
duk_uint32_t duk_hobject_get_length(duk_hthread *thr, duk_hobject *obj);

/* Object built-in methods */
int duk_hobject_object_define_property(duk_context *ctx);
int duk_hobject_object_define_properties(duk_context *ctx);
int duk_hobject_object_get_own_property_descriptor(duk_context *ctx);
void duk_hobject_object_seal_freeze_helper(duk_hthread *thr, duk_hobject *obj, int freeze);
int duk_hobject_object_is_sealed_frozen_helper(duk_hobject *obj, int is_frozen);
int duk_hobject_object_ownprop_helper(duk_context *ctx, int required_desc_flags);

/* internal properties */
int duk_hobject_get_internal_value(duk_heap *heap, duk_hobject *obj, duk_tval *tv);
duk_hstring *duk_hobject_get_internal_value_string(duk_heap *heap, duk_hobject *obj);
	
/* hobject management functions */
void duk_hobject_compact_props(duk_hthread *thr, duk_hobject *obj);

/* enumeration */
void duk_hobject_enumerator_create(duk_context *ctx, int enum_flags);
int duk_hobject_get_enumerated_keys(duk_context *ctx, int enum_flags);
int duk_hobject_enumerator_next(duk_context *ctx, int get_value);

/* macros */
void duk_hobject_set_prototype(duk_hthread *thr, duk_hobject *h, duk_hobject *p);

/* finalization */
void duk_hobject_run_finalizer(duk_hthread *thr, duk_hobject *obj);

/* pc2line */
void duk_hobject_pc2line_pack(duk_hthread *thr, duk_compiler_instr *instrs, size_t length);
duk_uint32_t duk_hobject_pc2line_query(duk_hbuffer_fixed *buf, int pc);

/* misc */	
int duk_hobject_prototype_chain_contains(duk_hthread *thr, duk_hobject *h, duk_hobject *p);

#endif  /* DUK_HOBJECT_H_INCLUDED */

