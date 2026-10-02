;;; Sierra Script 1.0 - (do not remove this comment)
(script# 974)
(include sci.sh)
(use System)

; A class with &layout has the properties of its text right after --info--,
; in the order of the text, and no other slot of its superclass (Castle of
; Dr. Brain script 947: DelayedEvent of Event has slots that Event does not
; have). The decompiled text keeps &layout. A class with &layout and no name
; property has no name slot. A subclass and an instance of a class with
; &layout have its layout, so their text has no &layout.
(class o3Base of Code
	(properties
		x 0
		y 0
	)
)

(class o3Layout of o3Base
	(properties &layout
		name "o3Layout"
		client 0
		y 5
		cycles 0
	)

	(method (doit)
		(if cycles
			(= y client)
		)
	)
)

(class o3NoName of o3Base
	(properties &layout
		y 2
		x 1
	)

	(method (doit)
		(return x)
	)
)

(class o3Sub of o3Layout
	(properties
		x 9
	)

	(method (doit)
		(return (+ x y))
	)
)

(instance o3Inst of o3Layout
	(properties
		cycles 3
	)
)

; Two classes with &layout and one name string: the second gets another
; name in the text, and its name slot keeps the string.
(class o3Dup of o3Base
	(properties &layout
		name "o3Dup"
		y 1
	)

	(method (doit)
		(return y)
	)
)

(class o3DupToo of o3Base
	(properties &layout
		name "o3Dup"
		x 1
	)

	(method (doit)
		(return x)
	)
)

; A class with &layout and no name whose first property is a string: the
; string is not its name (it has no name slot), so it gets a made-up name,
; and the decompiler adds no name slot.
(class o3StringFirst of o3Base
	(properties &layout
		y "a string"
		x 0
	)

	(method (doit)
		(return x)
	)
)

; A class with &layout and no slot after --info--, and its subclass, whose
; name slot is a new property.
(class o3Empty of o3Base
	(properties &layout)

	(method (doit)
		(return 0)
	)
)

(class o3EmptySub of o3Empty
	(properties
		name "o3EmptySub"
		client 1
	)

	(method (doit)
		(return client)
	)
)

; A class with no superclass and no slot after --info--.
(class o3EmptyRoot
	(properties &layout)

	(method (doit)
		(return 1)
	)
)
