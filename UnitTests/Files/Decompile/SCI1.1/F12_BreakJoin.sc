;;; Sierra Script 1.0 - (do not remove this comment)
(script# 925)
(include sci.sh)

; A break at the end of an if's else, inside a loop body, followed by a
; statement that another branch also reaches. The break resolver sends the
; break on to that next statement, so the if that holds the break would
; gather a statement it does not own. The if-builder must move the break
; edge to the if's follow instead.
(public
	f12BreakJoin 0
)

(procedure (f12BreakJoin param1 &tmp temp0 temp1)
	(asm
		ldi 0
		sat temp0
		ldi 0
		sat temp1
	loopHead:
		lst temp0
		lap param1
		lt?
		bnt loopExit
		+at temp0
		lst temp0
		ldi 1
		eq?
		bnt next
		lst temp0
		ldi 2
		eq?
		bnt elseB
		ldi 1
		sat temp1
		jmp loopHead
	elseB:
		ldi 2
		sat temp1
		jmp loopExit
	next:
		lst temp0
		ldi 3
		eq?
		bnt last
		ldi 3
		sat temp1
	last:
		jmp loopHead
	loopExit:
		lat temp1
		ret
	)
)
