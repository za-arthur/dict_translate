CREATE EXTENSION dict_translate;

CREATE TEXT SEARCH DICTIONARY test_trn (
		Template = translate,
		DictFile = translate_sample,
		InputDict = english_stem);

SELECT ts_lexize('test_trn', 'forest');
SELECT ts_lexize('test_trn', 'forests');
SELECT ts_lexize('test_trn', 'home');
SELECT ts_lexize('test_trn', 'homeless');
SELECT ts_lexize('test_trn', 'haus');
