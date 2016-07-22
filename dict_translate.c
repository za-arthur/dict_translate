/*-------------------------------------------------------------------------
 *
 * dict_translate.c
 *	  Translation dictionary
 *
 * IDENTIFICATION
 *	  contrib/dict_translate/dict_translate.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/namespace.h"
#include "commands/defrem.h"
#include "nodes/pg_list.h"
#include "tsearch/ts_cache.h"
#include "tsearch/ts_locale.h"
#include "tsearch/ts_public.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

typedef struct
{
	char	   *in;
	char	   *out;
	int			outlen;
} TranslateEntry;

typedef struct
{
	int			len;			/* length of trn array */
	TranslateEntry *trn;

	/* subdictionary to normalize input lexeme */
	Oid			inDictOid;
	TSDictionaryCacheEntry *inDict;
} DictTranslate;

PG_FUNCTION_INFO_V1(dtrn_init);
PG_FUNCTION_INFO_V1(dtrn_lexize);

/*
 * Finds the next whitespace-delimited word within the 'in' string.
 * Returns a pointer to the first character of the word, and a pointer
 * to the next byte after the last character in the word (in *end).
 */
static char *
findwrd(char *in, char **end)
{
	char	   *start;

	/* Skip leading spaces */
	while (*in && t_isspace(in))
		in += pg_mblen(in);

	/* Return NULL on empty lines */
	if (*in == '\0')
	{
		*end = NULL;
		return NULL;
	}

	start = in;

	/* Find end of word */
	while (*in && !t_isspace(in))
		in += pg_mblen(in);

	*end = in;

	return start;
}

static int
compareTrn(const void *a, const void *b)
{
	return strcmp(((const TranslateEntry *) a)->in,
				  ((const TranslateEntry *) b)->in);
}

static void
read_dictionary(char *filename, DictTranslate *d)
{
	tsearch_readline_state trst;
	char	   *line = NULL;
	char	   *starti,
			   *starto,
			   *end = NULL;
	int			cur = 0;

	filename = get_tsearch_config_filename(filename, "trn");

	if (!tsearch_readline_begin(&trst, filename))
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("could not open translate file \"%s\": %m",
						filename)));

	while ((line = tsearch_readline(&trst)) != NULL)
	{
		starti = findwrd(line, &end);
		if (!starti)
		{
			/* Empty line */
			goto skipline;
		}
		if (*end == '\0')
		{
			/* A line with only one word. Ignore silently. */
			goto skipline;
		}
		*end = '\0';

		starto = findwrd(end + 1, &end);
		if (!starto)
		{
			/* A line with only one word (+whitespace). Ignore silently. */
			goto skipline;
		}
		*end = '\0';

		/*
		 * starti now points to the first word, and starto to the second word
		 * on the line, with a \0 terminator at the end of both words.
		 */

		if (cur >= d->len)
		{
			if (d->len == 0)
			{
				d->len = 64;
				d->trn = (TranslateEntry *) palloc(sizeof(TranslateEntry) * d->len);
			}
			else
			{
				d->len *= 2;
				d->trn = (TranslateEntry *) repalloc(d->trn,
													 sizeof(TranslateEntry) * d->len);
			}
		}

		d->trn[cur].in = lowerstr(starti);
		d->trn[cur].out = lowerstr(starto);

		d->trn[cur].outlen = strlen(starto);

		cur++;

skipline:
		pfree(line);
	}

	tsearch_readline_end(&trst);

	d->len = cur;
	qsort(d->trn, d->len, sizeof(TranslateEntry), compareTrn);
}

Datum
dtrn_init(PG_FUNCTION_ARGS)
{
	List	   *dictoptions = (List *) PG_GETARG_POINTER(0);
	ListCell   *l;
	DictTranslate *d;
	bool		fileloaded = false;
	char	   *inputdict = NULL;

	d = (DictTranslate *) palloc0(sizeof(DictTranslate));

	foreach(l, dictoptions)
	{
		DefElem    *defel = (DefElem *) lfirst(l);

		if (pg_strcasecmp("DictFile", defel->defname) == 0)
		{
			if (fileloaded)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("multiple DictFile parameters")));
			read_dictionary(defGetString(defel), d);
			fileloaded = true;
		}
		else if (pg_strcasecmp("InputDict", defel->defname) == 0)
		{
			if (inputdict)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("multiple InputDict parameters")));
			inputdict = pstrdup(defGetString(defel));
		}
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("unrecognized synonym parameter: \"%s\"",
							defel->defname)));
	}

	if (!fileloaded)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("missing DictFile parameter")));
	if (!inputdict)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("missing InputDict parameter")));

	d->inDictOid = get_ts_dict_oid(stringToQualifiedNameList(inputdict), false);
	d->inDict = lookup_ts_dictionary_cache(d->inDictOid);

	PG_RETURN_POINTER(d);
}

Datum
dtrn_lexize(PG_FUNCTION_ARGS)
{
	DictTranslate *d = (DictTranslate *) PG_GETARG_POINTER(0);
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	TSLexeme   *res = NULL,
			   *ptr_cur,
			   *ptr_res;
	TranslateEntry key,
			   *found;
	int32		nvariant = 1;

	/* note: d->len test protects against Solaris bsearch-of-no-items bug */
	if (len <= 0 || d->len <= 0)
		PG_RETURN_POINTER(NULL);

	if (!d->inDict->isvalid)
		d->inDict = lookup_ts_dictionary_cache(d->inDictOid);

	res = (TSLexeme *) DatumGetPointer(FunctionCall4(&(d->inDict->lexize),
									   PointerGetDatum(d->inDict->dictData),
													 PointerGetDatum(in),
													 Int32GetDatum(len),
													 PointerGetDatum(NULL)));

	if (!res || !res->lexeme)
		PG_RETURN_POINTER(NULL);

	ptr_res = ptr_cur = res;
	while (ptr_cur->lexeme)
	{
		key.in = lowerstr_with_len(ptr_cur->lexeme, strlen(ptr_cur->lexeme));
		key.out = NULL;

		found = (TranslateEntry *) bsearch(&key, d->trn, d->len,
										   sizeof(TranslateEntry), compareTrn);
		pfree(ptr_cur->lexeme);
		pfree(key.in);

		if (found)
		{
			ptr_res->nvariant = nvariant;
			ptr_res->lexeme = pnstrdup(found->out, found->outlen);
			ptr_res->flags = 0;

			ptr_res++;
			nvariant++;
		}
		/*
		 * Compound words do not supported yet.
		 * So iterate without considering them.
		 */
		ptr_cur++;
	}
	ptr_res->lexeme = NULL;

	PG_RETURN_POINTER(res);
}
