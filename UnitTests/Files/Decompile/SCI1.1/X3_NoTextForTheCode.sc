;;; Sierra Script 1.0 - (do not remove this comment)
(script# 971)
(include sci.sh)
(use System)

; Code that the text cannot have: a super in a procedure (an export at the
; code of a method, PQ4 CD script 10), and a property past the end of the
; object (Act::canBeHere of LSL3 reads one). Each function falls back to
; asm, which compiles to the same code.
(public
	x3SuperInAProcedure 0
)

(procedure (x3SuperInAProcedure)
	(asm
		pushi #init
		push0
		super Script, 4
		ret
	)
)

(instance x3Script of Script
	(properties)

	(method (doit)
		(asm
			pToa 400
			aTop 402
			ret
		)
	)
)
