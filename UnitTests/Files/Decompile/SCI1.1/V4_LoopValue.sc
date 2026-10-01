;;; Sierra Script 1.0 - (do not remove this comment)
(script# 967)
(include sci.sh)

; The value of a loop is the accumulator at its exit: 0 at the exit of its
; test, the value of a breakif. The test of the outer loop is the inner
; loop (ICEMAN script 385, localproc_02bc; Snuffer gives a for whose test
; is the inner for).
(public
	v4LoopValue 0
)

(procedure (v4LoopValue param1 &tmp temp0 temp1)
	(asm
		push2
		push1
		pushi 15
		callk Random, 4
		sat temp0
	outer:
		ldi 0
		sat temp1
	inner:
		lst temp1
		ldi 12
		lt?
		bnt end
		lst temp0
		lap param1
		eq?
		bt after
		+at temp1
		jmp inner
	after:
		bnt end
		push2
		push1
		pushi 15
		callk Random, 4
		sat temp0
		jmp outer
	end:
		lat temp0
		ret
	)
)
