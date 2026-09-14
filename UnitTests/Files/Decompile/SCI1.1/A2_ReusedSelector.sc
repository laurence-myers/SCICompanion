;;; Sierra Script 1.0 - (do not remove this comment)
(script# 930)
(include sci.sh)

; Sierra's optimizer turns "pushi n" into "push" when the accumulator holds
; n, and into "dup" when the stack top holds n. A selector pushed right
; after a stray "ldi" of its own number (QfG4 sClimbWall::changeState, the
; case whose value is the cel selector) came out as selector 0. A repeated
; literal argument is a dup of the first.
; Sierra: (param1 init:) (param1 posn: 3 3). An ldi right before its push folds
; into the selector; in QfG4 the pushes of earlier messages sit between the
; two, and the value comes from a clone of the ldi.
(public
	a2ReusedSelector 0
)

(procedure (a2ReusedSelector param1)
	(asm
		ldi #init
		push
		push0
		lap param1
		send 4
		pushi #posn
		push2
		pushi 3
		dup
		lap param1
		send 8
		ret
	)
)
