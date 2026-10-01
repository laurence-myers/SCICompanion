;;; Sierra Script 1.0 - (do not remove this comment)
(script# 965)
(include sci.sh)

; The operands of a mul in the other order: the call result stays in the
; accumulator, and "pushi 6" comes after it (Sierra's compiler gives
; "push; ldi 6; mul"). Shape of the SQ4 copy in a "patch" folder, script
; 381 (roboClerkWelcome::changeState).
(public
	v2SwappedOperands 0
)

(procedure (v2SwappedOperands &tmp temp0)
	(asm
		push2
		pushi 8
		pushi 15
		callk Random, 4
		pushi 6
		mul
		sat temp0
		ret
	)
)
