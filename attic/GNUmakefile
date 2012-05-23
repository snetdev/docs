# Requirements to use this Makefile:
# - Docutils 0.9+: http://docutils.sourceforge.net/
# - Pygments: http://pygments.org/
# - Rubber: http://launchpad.net/rubber/

RST2HTML = rst2html
RST2LATEX = rst2latex
PYGMENTIZE = pygmentize
PYGMENTIZE_HTML_STYLE = fruity
PYGMENTIZE_LATEX_STYLE = default
PYTHON = python
RUBBER = rubber


SOURCES := $(wildcard *.rst)

all: $(SOURCES:.rst=.html) $(SOURCES:.rst=.pdf)

clean:
	rm -f *.log *.out *.aux *.toc *\~

.SUFFIXES: .html .tex .pdf .rst .sty .css

%.html: %.rst tools/colorful-$(PYGMENTIZE_HTML_STYLE).css
	$(RST2HTML) $< \
	   --stylesheet=tools/html4css1.css,tools/colorful-$(PYGMENTIZE_HTML_STYLE).css >$@

%.pdf: %.tex tools/colorful-$(PYGMENTIZE_LATEX_STYLE).sty
	$(RUBBER) -d $<

%.tex: %.rst
	$(RST2LATEX) $< \
	   --latex-preamble='\usepackage[margin=2cm]{geometry}' \
	   --use-latex-abstract \
	   --stylesheet=tools/colorful-$(PYGMENTIZE_LATEX_STYLE).sty >$@

tools/colorful-%.css:
	$(PYGMENTIZE) -f html -S $* >$@ || (rm -f $@; false)

tools/colorful-%.sty: tools/colorful-%.css
	$(PYTHON) tools/makesty.py <$< >$@ || (rm -f $@; false)

