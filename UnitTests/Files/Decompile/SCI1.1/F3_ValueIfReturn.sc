;;; Sierra Script 1.0 - (do not remove this comment)
(script# 909)
(include sci.sh)

; Family 3: a short-circuit and whose value a "ret" consumes at the join.
; Sierra: (return (and temp0 temp1)).
(public
	f3ValueIfReturn 0
)

(procedure (f3ValueIfReturn &tmp temp0 temp1)
	(asm
		lat temp0
		bnt join
		lat temp1
	join:
		ret
	)
)
