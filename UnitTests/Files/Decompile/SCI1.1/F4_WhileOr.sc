;;; Sierra Script 1.0 - (do not remove this comment)
(script# 916)
(include sci.sh)

; Family 4: a while whose test is an or. The "bt" lands on the join "bnt", as
; Sierra's compiler emits it. Sierra: (while (or temp0 temp1) (= temp2 1)).
(public
	f4WhileOr 0
)

(procedure (f4WhileOr &tmp temp0 temp1 temp2)
	(asm
	loopHead:
		lat temp0
		bt join
		lat temp1
	join:
		bnt loopExit
		ldi 1
		sat temp2
		jmp loopHead
	loopExit:
		ret
	)
)
