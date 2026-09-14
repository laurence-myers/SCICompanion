;;; Sierra Script 1.0 - (do not remove this comment)
(script# 904)
(include sci.sh)

; Family 4: two "bnt" to the loop exit inside the body, next to a break. Each
; becomes an if with a synthesized else-break; the loop cleanup passes factor
; the breaks out. Sierra:
;   (repeat (if (or temp0 temp1) (if (and temp0 temp1) (= temp2 1)) (break)) (= temp2 0))
(public
	f4BreakElseEdge 0
)

(procedure (f4BreakElseEdge &tmp temp0 temp1 temp2)
	(asm
	loopHead:
		lat temp0
		bt inner
		lat temp1
		bnt frameOut
	inner:
		lat temp0
		bnt loopExit
		lat temp1
		bnt loopExit
		ldi 1
		sat temp2
		jmp loopExit
	frameOut:
		ldi 0
		sat temp2
		jmp loopHead
	loopExit:
		ret
	)
)
