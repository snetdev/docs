SNet technical documentation
============================

This repository contains documentation and small examples / unit tests
for the SNet technology.

To view the documentation after formatting:
http://readthedocs.org/docs/snetdev/

To generate the documentation on your own system:

1. clone this repo.
2. use ``make``, eg. ``make html`` or ``make latexpdf``.

We use:

- to format the text, reStructuredText_, a handy markup language for
  technical documentation. Version 0.9 or later of Python Docutils_ is
  required for code syntax coloring.

  .. _reStructuredText: http://docutils.sourceforge.net/rst.html

  .. _Docutils: http://docutils.sourceforge.net/

- to bundle the individual files into one documentation, Sphinx_, a
  handy documentation generator able to emit multiple formats (TeX,
  HTML, ePUB, info, manual pages) from reStructuredText_.

  .. _Sphinx: http://sphinx.pocoo.org/

