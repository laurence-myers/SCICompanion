;;; Sierra Script 1.0 - (do not remove this comment)
(script# 928)
(include sci.sh)
(use System)

; Sierra's compiler reuses the accumulator: after a store, a send whose
; target (or a pushed argument) is that variable has no load of its own,
; because the pushes before it leave the accumulator alone. The store is a
; statement; the send reads the variable. A send evaluates its target after
; its arguments, so a store before the pushes can never be the target.
; Sierra:
;   (= temp0 5) (temp0 init:)
;   (= temp0 param1) (temp1 perform: temp0)
;   (= x param1) (temp1 perform: x)
;   ((= x temp0) init: self &rest)   the store target reuses temp0 across the pushes and the &rest
(class A1Reuse of Code
	(properties
		x 0
	)

	(method (doit param1 &tmp temp0 temp1)
		(asm
			ldi 5
			sat temp0
			pushi #init
			pushi 0
			send 4
			lap param1
			sat temp0
			pushi #perform
			pushi 1
			push
			lat temp1
			send 6
			lap param1
			aTop x
			pushi #perform
			pushi 1
			push
			lat temp1
			send 6
			lap param1
			sat temp0
			pushi #init
			pushi 1
			pushSelf
			&rest 2
			aTop x
			send 6
			ret
		)
	)
)
