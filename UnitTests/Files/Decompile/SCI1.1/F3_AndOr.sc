;;; Sierra Script 1.0 - (do not remove this comment)
(script# 912)
(include sci.sh)

; Family 3: an or inside an and. Sierra: (if (and temp0 (or temp1 temp2)) (= temp3 1)).
(public
	f3AndOr 0
)

(procedure (f3AndOr &tmp temp0 temp1 temp2 temp3)
	(asm
		lat temp0
		bnt done
		lat temp1
		bt join
		lat temp2
	join:
		bnt done
		ldi 1
		sat temp3
	done:
		ret
	)
)
