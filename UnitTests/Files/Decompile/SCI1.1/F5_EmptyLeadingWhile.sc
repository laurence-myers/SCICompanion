;;; Sierra Script 1.0 - (do not remove this comment)
(script# 905)
(include sci.sh)

; Family 5: an empty while loop is the first statement.
; Its head dominates every node. Its follow node is the head of the next loop.
; Child collection then pulls the follow node into the loop's own children.
(public
	f5EmptyLeadingWhile 0
)

(procedure (f5EmptyLeadingWhile &tmp temp0 temp1 temp2)
	(asm
	w1:
		lat temp0
		bnt w2
		jmp w1
	w2:
		lat temp1
		bnt w2end
		lst temp2
		ldi 1
		add
		sat temp2
		jmp w2
	w2end:
		ret
	)
)
