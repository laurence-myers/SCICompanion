.. foreach

.. include:: /includes/standard.rst

===========
 foreach
===========

This loop iterates over an array or a Node-based collection (anything that uses the Node kernel calls and exposes ``elements``), binding each value in turn. The iteration variable need not be declared beforehand; a temporary is created for you. It expands into an ordinary ``for`` or ``while`` loop and compiles to standard bytecode.

``foreach`` is a reserved word, so it cannot be used as an identifier.

Example::

	; No need to predeclare n, a &tmp variable is made for you.
	(foreach n anArray
		; (do something with n)
	)

	; Works on anything based on the List/Node kernel calls.
	(foreach item aCollection
		; (do something with item)
	)
