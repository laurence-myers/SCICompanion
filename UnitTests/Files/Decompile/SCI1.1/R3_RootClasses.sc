;;; Sierra Script 1.0 - (do not remove this comment)
(script# 972)
(include sci.sh)
(use System)

; Classes with no superclass. One that declares properties and no name has
; no name slot (Castle of Dr. Brain script 943, Class_943_3); one that
; declares name has it, after --info-- when name is the first property, else
; in the order of the text (QfG3 script 47, Class_47_1). The decompiled text
; keeps each layout, and each object gets the name of its name slot. A class
; with no name slot gets a made-up name, also when its first property has a
; string. A class with a made-up name (Class_972_9: the text of a decompile,
; compiled) is keyed by its species in the meaning check.
(class r3Root
	(properties
		x 5
		y 7
	)

	(method (doit)
		(return y)
	)
)

(class r3NamedRoot
	(properties
		name "r3NamedRoot"
		x 0
	)

	(method (doit)
		(return x)
	)
)

(class r3NameSecond
	(properties
		x 0
		name "r3NameSecond"
		y 4
	)

	(method (doit)
		(return y)
	)
)

(class Class_972_9 of Code
	(properties
		x 3
	)
	
	(method (doit)
		(return x)
	)
)

(class r3TextFirst
	(properties
		x "r3Text"
	)
)

(instance r3SecondObj of r3NameSecond
	(properties
		y 9
	)
)

; Objects that get another name in the text (two objects with one name): the
; name slot after another property keeps the original string, once.
(instance r3DupA of r3NameSecond
	(properties
		name "r3Dup"
		y 1
	)
)

(instance r3DupB of r3NameSecond
	(properties
		name "r3Dup"
		y 2
	)
)

(class r3Twin of r3NameSecond
	(properties
		name "r3Twin"
		y 1
	)
)

(class r3TwinB of r3NameSecond
	(properties
		name "r3Twin"
		y 2
	)
)
