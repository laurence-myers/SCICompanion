;;; Sierra Script 1.0 - (do not remove this comment)
(script# 968)
(include sci.sh)
(use System)

; An instance whose name is the name of a property of an object of the
; script: in a method, the compiler reads the name as the property (Rm::init
; of many SCI0 games: (= controls controls) stores the property). The text
; gives the instance another name, and keeps its name string.
(instance clientObj of Code
	(properties
		name "client"
	)
)

(instance aScript of Script
	(properties)

	(method (doit)
		(= client clientObj)
	)
)
