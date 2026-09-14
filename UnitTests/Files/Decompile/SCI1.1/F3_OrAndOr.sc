;;; Sierra Script 1.0 - (do not remove this comment)
(script# 911)
(include sci.sh)

; Family 3: an and inside an or. Sierra's optimizer sends the inner "bnt"
; straight to the failure label, so it bypasses the join "bnt". The branch
; deoptimizer restores the join. Sierra: (if (or temp0 (and temp1 temp2)) (= temp3 1)).
(public
	f3OrAndOr 0
)

(procedure (f3OrAndOr &tmp temp0 temp1 temp2 temp3)
	(asm
		lat temp0
		bt join
		lat temp1
		bnt done
		lat temp2
	join:
		bnt done
		ldi 1
		sat temp3
	done:
		ret
	)
)
