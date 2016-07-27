# dict_translate - text search dictionary template for translation

This dictionary normalizes input word into lexems and replaces them with groups
of their translations.

## Installation

Here the example of installation of the module:

    $ git clone https://github.com/select-artur/dict_translate
    $ cd dict_translate
    $ make USE_PGXS=1
    $ sudo make USE_PGXS=1 install
    $ make USE_PGXS=1 installcheck
    $ psql DB -c "CREATE EXTENSION dict_translate;"

## Usage

You can use this template in the following way.

Create the file $SHAREDIR/tsearch_data/test_trn.trn:

    $ forest wald forst holz
    $ home haus heim

Create text search dictionary and configuration:

```sql
=# CREATE TEXT SEARCH DICTIONARY test_trn (
 Template = translate,
 DictFile = test_trn,
 InputDict = pg_catalog.english_stem);

=# CREATE TEXT SEARCH CONFIGURATION test_cfg(COPY='simple');

=# ALTER TEXT SEARCH CONFIGURATION test_cfg
 ALTER MAPPING FOR asciiword, asciihword, hword_asciipart,
   word, hword, hword_part
 WITH test_trn, english_stem;
```

You can test this dictionary using this table:

```sql
=# CREATE TABLE test (t text);

=# INSERT INTO test VALUES ('homes'), ('home'), ('forest'), ('haus');
```

Query examples:

```sql
=# SELECT * FROM test WHERE to_tsvector('test_cfg', t) @@ to_tsquery('test_cfg', 'forests');
   t    
--------
forest
(1 row)

=# SELECT * FROM test WHERE to_tsvector('test_cfg', t) @@ to_tsquery('test_cfg', 'home');
   t   
-------
 homes
 home
 haus
(3 rows)
```
