.. verbs

.. include:: /includes/standard.rst

=========
 verbs
=========

Inspired by a more complicated version in Phil Fortier's private build, this block lets you simplify your `(method (doVerb theVerb)` blocks. It compiles into an ordinary `doVerb`, with a free default case.

``verbs`` is a reserved word, so it cannot be used as an identifier.

Example::

	(verbs
		(V_DO
			; special actions here
			(super doVerb: theVerb)
		)
		(V_TALK
			; actions here
		)
		(V_COIN
			; using coin item here
		)
	)
