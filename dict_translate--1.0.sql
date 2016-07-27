/* contrib/dict_translate/dict_translate--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION dict_translate" to load this file. \quit

CREATE FUNCTION dtrn_init(internal)
		RETURNS internal
		AS 'MODULE_PATHNAME'
		LANGUAGE C STRICT;

CREATE FUNCTION dtrn_lexize(internal, internal, internal, internal)
		RETURNS internal
		AS 'MODULE_PATHNAME'
		LANGUAGE C STRICT;

CREATE TEXT SEARCH TEMPLATE translate (
		LEXIZE = dtrn_lexize,
		INIT   = dtrn_init
);

COMMENT ON TEXT SEARCH TEMPLATE translate IS 'Dictionary template for translation';
