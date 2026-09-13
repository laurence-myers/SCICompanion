;;; Sierra Script 1.0 - (do not remove this comment)
(script# 915)
(include sci.sh)

; Family 4: a while whose test is an and. The second "bnt" to the loop exit
; becomes an else-break, which the loop cleanup folds back into the test.
; Sierra: (while (and temp0 temp1) (= temp2 1)).
(public
	f4WhileAnd 0
)

(procedure (f4WhileAnd &tmp temp0 temp1 temp2)
	(asm
	loopHead:
		lat temp0
		bnt loopExit
		lat temp1
		bnt loopExit
		ldi 1
		sat temp2
		jmp loopHead
	loopExit:
		ret
	)
)
