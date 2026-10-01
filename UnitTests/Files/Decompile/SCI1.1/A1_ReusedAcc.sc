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
;   (= x param1) (temp1 perform: param1)
;   ((= x temp0) init: self &rest)   the store target reuses temp0 across the pushes and the &rest
;   6 (temp1 perform: 6)   a reused number stays a statement; the argument is a copy
; After "aTop x", Sierra's optimiser still knows the accumulator as param1
; (a store to a property does not change what it knows), so the push in
; place of a load is a push of param1: "pTos x" stays a load. The decompiler
; gives the parameter: (temp1 perform: theX).
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
			ldi 6
			pushi #perform
			pushi 1
			push
			lat temp1
			send 6
			ret
		)
	)
)
