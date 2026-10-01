;;; Sierra Script 1.0 - (do not remove this comment)
(script# 953)
(include sci.sh)
(use System)

; Values that Sierra's optimiser reuses across a branch. The scope engine
; reads them from the facts of the optimiser (sc OPTIMIZE.CPP).
; Space Quest 3 rm017::init: the argument count of the call in the
; then-part is a dup of the count that the outer call pushed before the if.
; Sierra: (Abs (if param1 (Abs param2) else 7))
; Jones in the Fast Lane rentLowCost::doit: the third term of the and is
; the variable of the store; the optimiser deleted its load, so its bnt
; repeats the second one, and does nothing. The send in the then-part has
; no load of its target either.
; Sierra: (if (and param1 (= temp0 param2) temp0) (temp0 init:))
; The then-part reads the tested value: the push is a copy of it.
; Sierra: (if param1 (Abs param1))
(public
	v1DupBeforeIf 0
	v1RepeatedTest 1
	v1ReuseAfterTest 2
)

(procedure (v1DupBeforeIf param1 param2)
	(asm
		push1
		lap param1
		bnt else1
		dup
		lap param2
		push
		callk Abs, 2
		jmp done1
	else1:
		ldi 7
	done1:
		push
		callk Abs, 2
		ret
	)
)

(procedure (v1RepeatedTest param1 param2 &tmp temp0)
	(asm
		lap param1
		bnt done2
		lap param2
		sat temp0
		bnt done2
		bnt done2
		pushi #init
		push0
		send 4
	done2:
		ret
	)
)

(procedure (v1ReuseAfterTest param1)
	(asm
		lap param1
		bnt done3
		push1
		push
		callk Abs, 2
	done3:
		ret
	)
)
