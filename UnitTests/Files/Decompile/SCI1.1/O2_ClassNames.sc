;;; Sierra Script 1.0 - (do not remove this comment)
(script# 973)
(include sci.sh)
(use System)

; Two classes of the class table with one name (Hoyle 1 has a Deck class in
; scripts 1 and 5): the second class gets another name in the text,
; which keeps its name string, so an instance of each class compiles to the
; same superclass. An instance with the name of a class of the table (Pepper
; script 110 has an Actor named twisty, the name of the game class) gets
; another name, so a send to it is not a send to the class.
(class o2First of Code
	(properties
		name "o2Same"
		x 1
	)
)

(class o2Second of Code
	(properties
		name "o2Same"
		y 2
	)
)

(instance o2OfFirst of o2First
	(properties)
)

(instance o2OfSecond of o2Second
	(properties)
)

(instance scriptObj of Code
	(properties
		name "Script"
	)
)

(instance o2Script of Script
	(properties)

	(method (doit)
		(o2OfFirst doit:)
		(o2OfSecond doit:)
		(scriptObj doit:)
	)
)
