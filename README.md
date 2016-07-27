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
