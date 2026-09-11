;;; Sierra Script 1.0 - (do not remove this comment)
(script# 904)
(include sci.sh)

; Family 4: a compound condition whose else edge is the loop exit, next to a
; break. The break is reconnected, but the compound else edge still points at
; the dead exit node, so the if gets a follow with one predecessor.
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
