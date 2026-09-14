;;; Sierra Script 1.0 - (do not remove this comment)
(script# 914)
(include sci.sh)

; Family 3: a short-circuit and pushed as a call argument. The join "push"
; consumes the value. Sierra: (Abs (and temp0 temp1)).
(public
	f3AndAsArgument 0
)

(procedure (f3AndAsArgument &tmp temp0 temp1 temp2)
	(asm
		pushi 1
		lat temp0
		bnt join
		lat temp1
	join:
		push
		callk Abs, 2
		sat temp2
		ret
	)
)
