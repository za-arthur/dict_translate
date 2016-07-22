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
	char	   *key;			/* Word */
	char	   *value;			/* Unparsed list of synonyms, including the
								 * word itself */
} TranslateEntry;

typedef struct
{
	size_t			len;			/* length of trn array */
	TranslateEntry *trn;

	/* subdictionary to normalize input lexeme */
	Oid			inDictOid;
	TSDictionaryCacheEntry *inDict;
} DictTranslate;

PG_FUNCTION_INFO_V1(dtrn_init);
PG_FUNCTION_INFO_V1(dtrn_lexize);

static char *
find_word(char *in, char **end)
{
	char	   *start;

	*end = NULL;
	while (*in && t_isspace(in))
		in += pg_mblen(in);

	if (!*in || *in == '#')
		return NULL;
	start = in;

	while (*in && !t_isspace(in))
		in += pg_mblen(in);

	*end = in;

	return start;
}

static int
compareTrn(const void *a, const void *b)
{
	return strcmp(((const TranslateEntry *) a)->key,
				  ((const TranslateEntry *) b)->key);
}

static void
read_dictionary(char *filename, DictTranslate *d)
{
	char	   *real_filename = get_tsearch_config_filename(filename, "trn");
	tsearch_readline_state trst;
	char	   *line = NULL;
	size_t		cur = 0;

	if (!tsearch_readline_begin(&trst, real_filename))
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("could not open translate file \"%s\": %m",
						real_filename)));

	while ((line = tsearch_readline(&trst)) != NULL)
	{
		char	   *lower_line,
				   *key,
				   *value,
				   *end;

		if (*line == '\0')
			continue;

		lower_line = lowerstr(line);
		pfree(line);

		key = find_word(lower_line, &end);
		if (!key)
		{
			/* Empty line */
			goto skipline;
		}

		if (*end == '\0')
		{
			/* A line with only one word. Ignore silently. */
			goto skipline;
		}

		/* Find start position of the key translation */
		value = end;
		while (*value && t_isspace(value))
			value += pg_mblen(value);

		if (!value)
		{
			/* A line with only one word (+whitespace). Ignore silently. */
			goto skipline;
		}

		/* Enlarge trn structure if full */
		if (cur == d->len)
		{
			d->len = (d->len > 0) ? 2 * d->len : 16;
			if (d->trn)
				d->trn = (TranslateEntry *) repalloc(d->trn,
													 sizeof(TranslateEntry) * d->len);
			else
				d->trn = (TranslateEntry *) palloc(sizeof(TranslateEntry) * d->len);
		}

		d->trn[cur].key = pnstrdup(key, end - key);
		d->trn[cur].value = pstrdup(value);

		cur++;

skipline:
		pfree(lower_line);
	}

	tsearch_readline_end(&trst);

	d->len = cur;
	if (cur > 1)
		qsort(d->trn, d->len, sizeof(TranslateEntry), compareTrn);

	pfree(real_filename);
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
			   *input_res,
			   *input_ptr;
	TranslateEntry word,
			   *found;
	uint16		nvariant = 1;
	Size		cur = 0,
				lres = 0;
	char	   *translate,
			   *pos,
			   *end;

	/* note: d->len test protects against Solaris bsearch-of-no-items bug */
	if (len <= 0 || d->len <= 0)
		PG_RETURN_POINTER(NULL);

	if (!d->inDict->isvalid)
		d->inDict = lookup_ts_dictionary_cache(d->inDictOid);

	input_res = (TSLexeme *) DatumGetPointer(FunctionCall4(&(d->inDict->lexize),
									   PointerGetDatum(d->inDict->dictData),
													 PointerGetDatum(in),
													 Int32GetDatum(len),
													 PointerGetDatum(NULL)));

	if (!input_res)
		PG_RETURN_POINTER(NULL);

	if (!input_res->lexeme)
	{
		pfree(input_res);
		PG_RETURN_POINTER(NULL);
	}

	input_ptr = input_res;
	while (input_ptr->lexeme)
	{
		word.key = lowerstr(input_ptr->lexeme);
		word.value = NULL;
		pfree(input_ptr->lexeme);

		found = (TranslateEntry *) bsearch(&word, d->trn, d->len,
										   sizeof(TranslateEntry), compareTrn);
		pfree(word.key);

		if (found)
		{
			pos = found->value;
			while ((translate = find_word(pos, &end)) != NULL)
			{
				if (cur == 0 || cur == lres - 1 /* for res[cur].lexeme = NULL */)
				{
					lres = (lres > 0) ? 2 * lres : 8;
					if (res)
						res = (TSLexeme *) repalloc(res, sizeof(TSLexeme) * lres);
					else
						res = (TSLexeme *) palloc(sizeof(TSLexeme) * lres);
				}

				res[cur].nvariant = nvariant;
				res[cur].lexeme = pnstrdup(translate, end - translate);
				res[cur].flags = 0;

				cur++;
				nvariant++;

				pos = end;
			}
		}
		/*
		 * Compound words do not supported yet.
		 * So iterate without considering them.
		 */
		input_ptr++;
	}

	pfree(input_res);

	if (res)
		res[cur].lexeme = NULL;

	PG_RETURN_POINTER(res);
}
